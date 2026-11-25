#!/usr/bin/env python3
"""Generates runtime/gen/ui_stagemap.h: the map of each stage for the port's map screen (M key / Y button).
The rooms are read from YOUR copy of the game (orig/files), so the result is not in the repository; without it the
map screen says it is not available.

  python3 scripts/make_map.py [files_folder]       (default: orig/files, or the extracted ModernGekko files)

The game has no map, so the layout is worked out here, in pixels: from each stage's start room (setting.csv
stage_start) it follows where the game goes (maplink.csv columns 1-4: right, left, bottom and top edge; a room named
there goes beside that edge, level with it; "auto" follows the room's "link" objects on that side, the new room put so
that the link it is entered by meets the link it is left by) and the room changes of the room scripts (opcode 20:
the new room goes below). A room that would cover most of a placed one is another version of the same place (secret or
trap variants) and is drawn as that one; a smaller overlap moves the new room away from it, along the way it was
entered first."""
import os, struct, sys, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_uifont import PSB, ROOT

OUT = os.path.join(ROOT, "runtime", "gen", "ui_stagemap.h")
EDGES = "rldu"                                   # maplink.csv columns 1-4


def unpack(path):
    d = open(path, "rb").read()
    return zlib.decompress(d[8:]) if d[:4] == b"mdf\0" else d


def read_text(gf, name):
    return unpack(os.path.join(gf, name + ".l")).decode("shift_jis", "replace")


def stage_starts(gf):
    for line in read_text(gf, "setting.csv").splitlines():
        if line.strip().startswith("stage_start"):
            return [s.split(":")[0] for s in line.split("=", 1)[1].strip().strip('"').split(",")]
    return []


def maplink_rows(gf):
    """room -> (boss room kind (2 = boss), [right, left, bottom, top])"""
    out = {}
    for line in read_text(gf, "maplink.csv").splitlines()[1:]:
        c = [x.strip() for x in line.split(",")]
        if len(c) >= 8 and c[0]:
            out[c[0]] = (int(c[7]) if c[7].isdigit() else 0, c[1:5])
    return out


def room_info(gf, prefix):
    """rooms named prefix*: name -> (w, h, links {label: (to, x, y)})"""
    out = {}
    for fn in sorted(os.listdir(gf)):
        if not (fn.startswith(prefix) and fn.endswith(".psb.l")):
            continue
        p = PSB(unpack(os.path.join(gf, fn)))
        root = p.val(p.o_root)
        links = {}
        for layer in root.get("layer", []):
            if layer.get("className") != "ObjLayerItem":
                continue
            for o in layer.get("objList") or []:
                a = o.get("attr") or {}
                if o.get("label") == "link" and a.get("label") and a["label"] not in links:
                    links[a["label"]] = (a.get("to", "") or "", o["x"], o["y"])
        out[fn[:-6]] = (root["fieldWidth"], root["fieldHeight"], links)
    return out


def script_rooms(gf, room):
    """Rooms a room's script goes to (opcode 20: string index a0 = "room:link")."""
    path = os.path.join(gf, "script", room + ".bin.l")
    if not os.path.isfile(path):
        return []
    b = unpack(path)
    size = struct.unpack_from("<i", b, 24)[0]
    if size < 0 or 28 + size + 4 > len(b):
        return []
    base = 28 + size
    n = struct.unpack_from("<i", b, base)[0]
    if not 0 < n < 1000:
        return []
    offs = struct.unpack_from("<%di" % n, b, base + 4)
    text = base + 4 + 4 * n
    out = []
    for i in range(size // 28):
        op, a0 = struct.unpack_from("<2i", b, 28 + 28 * i)
        if op == 20 and 0 <= a0 < n:
            o = text + offs[a0]
            out.append(b[o:b.index(b"\0", o)].decode("latin1").split(":")[0])
    return out


def nearest_side(w, h, x, y):
    d = {"l": x, "r": w - x, "u": y, "d": h - y}
    return min(d, key=d.get)


def inter(a, b):
    w = min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0])
    h = min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1])
    return w * h if w > 4 and h > 4 else 0


def layout(rooms, exits, scripts, start):
    """-> pos {room: (x, y)}, order, alias {room: drawn room}"""
    pos, order, alias, q = {start: (0, 0)}, [start], {}, [start]
    while q:
        a = q.pop(0)
        ax, ay = pos[a]
        aw, ah, alinks = rooms[a]
        aex = exits.get(a)
        steps = []                               # (room, side, entry link or None, link x, y)
        if aex:
            for side, cell in zip(EDGES, aex):
                if cell and cell != "auto":
                    steps.append((cell, side, None, 0, 0))
        for to, lx, ly in alinks.values():
            if not to:
                continue
            s = nearest_side(aw, ah, lx, ly)
            if aex and aex[EDGES.index(s)] not in ("auto", ""):
                continue
            b, _, bl = to.partition(":")
            steps.append((b, s, bl, lx, ly))
        for b in scripts.get(a, []):
            steps.append((b, "d", "", aw // 2, ah))
        for b, s, bl, lx, ly in steps:
            if b not in rooms or b in pos or b in alias:
                continue
            bw, bh, blinks = rooms[b]
            if bl == "":                         # a script's room change: centred below
                p = (ax + (aw - bw) // 2, ay + ah)
            elif bl is None or bl not in blinks:
                p = {"r": (ax + aw, ay), "l": (ax - bw, ay), "d": (ax, ay + ah), "u": (ax, ay - bh)}[s]
            else:
                bx, by = blinks[bl][1:]
                p = {"r": (ax + aw, ay + ly - by), "l": (ax - bw, ay + ly - by),
                     "u": (ax + lx - bx, ay - bh), "d": (ax + lx - bx, ay + ah)}[s]
            placed = [(o, (pos[o][0], pos[o][1], rooms[o][0], rooms[o][1])) for o in order]
            hits = [(o, inter((p[0], p[1], bw, bh), box)) for o, box in placed]
            hits = [h for h in hits if h[1]]
            if hits:
                o, area = max(hits, key=lambda t: t[1])
                if area >= min(bw * bh, rooms[o][0] * rooms[o][1]) // 2:
                    alias[b] = o
                    continue
                dx, dy = {"r": (1, 0), "l": (-1, 0), "d": (0, 1), "u": (0, -1)}[s]
                for d in range(8, 8000, 8):
                    cand = [(p[0] + ux * d, p[1] + uy * d) for ux, uy in ((dx, dy), (dy, dx), (-dy, -dx))]
                    free = [c for c in cand if not any(inter((c[0], c[1], bw, bh), box) for _, box in placed)]
                    if free:
                        p = free[0]
                        break
            pos[b] = p
            order.append(b)
            q.append(b)
    return pos, order, alias


def main():
    cands = [sys.argv[1]] if len(sys.argv) > 1 else [os.path.join(ROOT, "orig", "files"),
             os.path.join(ROOT, "tools", "ModernGekko-Template", "extracted", "castlevania-rebirth", "files")]
    gf = next((c for c in cands if os.path.isfile(os.path.join(c, "maplink.csv.l"))), None)
    if not gf:
        print("make_map: maplink.csv.l not found (the map screen will not be available)")
        return 0
    ml = maplink_rows(gf)
    exits = {n: v[1] for n, v in ml.items()}
    out = ["// Generated by scripts/make_map.py from the rooms of YOUR copy of the game: do not commit it.",
           "#pragma once",
           "// Rooms in pixels (x, y from the stage's top left corner); alias >= 0: drawn as that room (another version",
           "// of the same place). boss = 1: the boss room.",
           "struct MapRoom { const char* name; int x, y, w, h; int boss; int alias; };",
           "struct MapStage { const MapRoom* rooms; int n; int w, h; };"]
    stages, notes = [], []
    for s, start in enumerate(stage_starts(gf)[:6]):
        rooms = room_info(gf, "st%d_" % (s + 1))
        if start not in rooms:
            stages.append(None)
            continue
        scripts = {n: script_rooms(gf, n) for n in rooms}
        pos, order, alias = layout(rooms, exits, scripts, start)
        x0 = min(pos[n][0] for n in order)
        y0 = min(pos[n][1] for n in order)
        w = max(pos[n][0] + rooms[n][0] for n in order) - x0
        h = max(pos[n][1] + rooms[n][1] for n in order) - y0
        idx = {n: i for i, n in enumerate(order)}
        rows = []
        for n in order:
            boss = 1 if ml.get(n, (0,))[0] == 2 else 0
            rows.append('    { "%s", %d, %d, %d, %d, %d, -1 },' % (n, pos[n][0] - x0, pos[n][1] - y0, rooms[n][0], rooms[n][1], boss))
        for n, t in sorted(alias.items()):
            while t in alias:
                t = alias[t]
            rows.append('    { "%s", 0, 0, 0, 0, 0, %d },' % (n, idx[t]))
        out.append("static const MapRoom MAP_ST%d[] = {" % (s + 1))
        out += rows
        out.append("};")
        stages.append((s + 1, len(rows), w, h))
        notes.append("st%d %d rooms" % (s + 1, len(order)))
    out.append("static const MapStage MAP_STAGES[6] = {")
    for st in stages + [None] * (6 - len(stages)):
        out.append("    { MAP_ST%d, %d, %d, %d }," % st if st else "    { nullptr, 0, 0, 0 },")
    out.append("};")
    text = "\n".join(out) + "\n"
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    if not (os.path.isfile(OUT) and open(OUT).read() == text):
        open(OUT, "w").write(text)
    print("make_map: %s (%s)" % (os.path.relpath(OUT, ROOT), ", ".join(notes)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
