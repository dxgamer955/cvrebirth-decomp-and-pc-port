// IOS (el coprocesador ARM de la Wii) emulado a nivel de API: IOS_Open/Close/Read/Write/Seek/Ioctl/Ioctlv.
// Dispositivos: /dev/fs (NAND de usuario, respaldado por una carpeta del host), /dev/es (titulo) y /dev/stm/*.
// Las variantes asincronas ejecutan la operacion al instante y entregan el callback en la siguiente interrupcion.
#include "hle.h"
#include <cstdio>
#include <deque>
#include <dirent.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir(p, m) _mkdir(p)
// rename de POSIX sustituye el destino; en Windows hay que pedirlo
static int rt_rename(const char* a, const char* b) { return MoveFileExA(a, b, MOVEFILE_REPLACE_EXISTING) ? 0 : -1; }
#else
static int rt_rename(const char* a, const char* b) { return rename(a, b); }
#endif

std::string g_nand_root = "nand";

enum { DEV_FS = 1, DEV_ES, DEV_STM, DEV_FILE };
struct IosFd { int type; FILE* f = nullptr; std::string path; u32 size = 0; int mode = 0; };
static std::map<s32, IosFd> g_ifd;
static s32 g_next_fd = 3;

struct PendingCb { u32 cb, arg; s32 result; };
static std::deque<PendingCb> g_cbs;

// Entrega los callbacks de IOS pendientes (como la interrupcion de IPC). Las interrupciones ya estan deshabilitadas.
void rt_ios_deliver() {
    while (!g_cbs.empty()) {
        PendingCb c = g_cbs.front(); g_cbs.pop_front();
        if (c.cb) rt_call_indirect(c.cb, (u32)c.result, c.arg, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
}

static RetRegs finish(s32 result, u32 cb, u32 arg) {
    if (cb) { g_cbs.push_back({ cb, arg, result }); g_irq_pending = 1; }
    return RET_I((u32)result);
}

static std::string host_path(const std::string& nand) {
    std::string p = g_nand_root + nand;
    return p;
}

static void mkdirs(const std::string& path) {
    for (size_t i = 1; i < path.size(); i++) if (path[i] == '/') { std::string d = path.substr(0, i); mkdir(d.c_str(), 0755); }
}

// ---- apertura ----
static s32 ios_open(const std::string& path, u32 mode) {
    IosFd f;
    if (path == "/dev/fs") f.type = DEV_FS;
    else if (path == "/dev/es") f.type = DEV_ES;
    else if (path.compare(0, 8, "/dev/stm") == 0) f.type = DEV_STM;
    else if (path.compare(0, 5, "/dev/") == 0) { rt_log("[ios] dispositivo no emulado: %s\n", path.c_str()); return -6; }
    else {
        f.type = DEV_FILE; f.path = host_path(path); f.mode = mode;
        mkdirs(f.path);
        f.f = fopen(f.path.c_str(), (mode & 2) ? "r+b" : "rb");
        if (!f.f) { rt_log("[ios] no existe %s\n", path.c_str()); return -106; }       // IOS_ENOENT de ISFS
    }
    s32 fd = g_next_fd++;
    g_ifd[fd] = f;
    return fd;
}

HLE_FN(IOS_Open)      { return RET_I((u32)ios_open(rt_guest_cstr(a0), a1)); }
HLE_FN(IOS_OpenAsync) { return finish(ios_open(rt_guest_cstr(a0), a1), a2, a3); }

static s32 ios_close(s32 fd) {
    auto it = g_ifd.find(fd); if (it == g_ifd.end()) return -4;
    if (it->second.f) fclose(it->second.f);
    g_ifd.erase(it); return 0;
}
HLE_FN(IOS_Close) { return RET_I((u32)ios_close(a0)); }                       // IOS_Close
HLE_FN(IOS_CloseAsync) { return finish(ios_close(a0), a1, a2); }                   // IOS_CloseAsync

static s32 ios_read(s32 fd, u32 buf, u32 len) {
    auto it = g_ifd.find(fd); if (it == g_ifd.end() || !it->second.f) return -4;
    return (s32)fread(HP(buf), 1, len, it->second.f);
}
HLE_FN(IOS_Read) { return RET_I((u32)ios_read(a0, a1, a2)); }                // IOS_Read
HLE_FN(IOS_ReadAsync) { return finish(ios_read(a0, a1, a2), a3, a4); }            // IOS_ReadAsync

static s32 ios_write(s32 fd, u32 buf, u32 len) {
    auto it = g_ifd.find(fd); if (it == g_ifd.end() || !it->second.f) return -4;
    size_t n = fwrite(HP(buf), 1, len, it->second.f); fflush(it->second.f); return (s32)n;
}
HLE_FN(IOS_Write) { return RET_I((u32)ios_write(a0, a1, a2)); }               // IOS_Write
HLE_FN(IOS_WriteAsync) { return finish(ios_write(a0, a1, a2), a3, a4); }           // IOS_WriteAsync

static s32 ios_seek(s32 fd, s32 off, u32 whence) {
    auto it = g_ifd.find(fd); if (it == g_ifd.end() || !it->second.f) return -4;
    fseek(it->second.f, off, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
    return (s32)ftell(it->second.f);
}
HLE_FN(IOS_Seek) { return RET_I((u32)ios_seek(a0, (s32)a1, a2)); }           // IOS_Seek
HLE_FN(IOS_SeekAsync) { return finish(ios_seek(a0, (s32)a1, a2), a3, a4); }       // IOS_SeekAsync

// ---- ioctl ----
static std::string cstr_n(u32 addr, u32 n) { std::string s; for (u32 i = 0; i < n; i++) { char c = (char)LD8(addr + i); if (!c) break; s += c; } return s; }

static s32 ios_ioctl(s32 fd, u32 cmd, u32 in, u32 inlen, u32 out, u32 outlen) {
    auto it = g_ifd.find(fd); if (it == g_ifd.end()) return -4;
    IosFd& d = it->second;
    if (d.type == DEV_FS) {
        switch (cmd) {
        case 0x09: case 0x03: {                                             // CreateFile / CreateDir
            std::string p = host_path(cstr_n(in + 6, 64));
            mkdirs(p);
            if (cmd == 0x03) { mkdir(p.c_str(), 0755); return 0; }
            if (access(p.c_str(), F_OK) == 0) return -105;                  // ya existe
            FILE* f = fopen(p.c_str(), "wb"); if (!f) return -1; fclose(f); return 0;
        }
        case 0x07: { std::string p = host_path(cstr_n(in, 64)); return (remove(p.c_str()) == 0) ? 0 : -106; }   // Delete
        case 0x08: { std::string a = host_path(cstr_n(in, 64)), b = host_path(cstr_n(in + 64, 64)); mkdirs(b); return rt_rename(a.c_str(), b.c_str()) == 0 ? 0 : -106; }
        case 0x06: {                                                        // GetAttr: ownerid, groupid, path, attr, perms
            std::string np = cstr_n(in, 64), p = host_path(np);
            if (access(p.c_str(), F_OK) != 0) return -106;
            for (u32 i = 0; i < outlen; i += 4) ST32(out + i, 0);
            ST32(out, 0); ST16(out + 4, 0); for (u32 i = 0; i < np.size() && i < 63; i++) ST8(out + 6 + i, np[i]);
            ST8(out + 0x46, 0); ST8(out + 0x47, 3); ST8(out + 0x48, 3); ST8(out + 0x49, 3);
            return 0;
        }
        case 0x05: return 0;                                                // SetAttr
        case 0x01: return 0;                                                // Format
        case 0x02:                                                          // GetStats: cluster 0x4000, mucho espacio libre
            ST32(out + 0x00, 0x4000); ST32(out + 0x04, 0x2000); ST32(out + 0x08, 0x100); ST32(out + 0x0C, 0x200);
            ST32(out + 0x10, 0x2100); ST32(out + 0x14, 0x1000); ST32(out + 0x18, 0x10);
            return 0;
        }
    } else if (d.type == DEV_FILE && cmd == 0x0B) {                         // GetFileStats: longitud y posicion
        long pos = ftell(d.f); fseek(d.f, 0, SEEK_END); long len = ftell(d.f); fseek(d.f, pos, SEEK_SET);
        ST32(out, (u32)len); ST32(out + 4, (u32)pos); return 0;
    } else if (d.type == DEV_STM) {
        return 0;
    }
    rt_log("[ios] ioctl no emulado: tipo %d cmd 0x%X\n", d.type, cmd);
    return -4;
}
HLE_FN(IOS_Ioctl)   { return RET_I((u32)ios_ioctl(a0, a1, a2, a3, a4, a5)); }        // IOS_Ioctl
HLE_FN(IOS_IoctlAsync) {
    auto it = g_ifd.find((s32)a0);
    if (it != g_ifd.end() && it->second.type == DEV_STM) return RET_I(0);          // el hook de eventos de STM (apagado, reset...) queda pendiente para siempre
    return finish(ios_ioctl(a0, a1, a2, a3, a4, a5), a6, a7);
}

// ---- ioctlv: vectores {base, longitud}; primero los de entrada y despues los de salida ----
static s32 ios_ioctlv(s32 fd, u32 cmd, u32 nin, u32 nout, u32 vec) {
    auto it = g_ifd.find(fd); if (it == g_ifd.end()) return -4;
    IosFd& d = it->second;
    auto V = [&](u32 i, u32& base, u32& len) { base = LD32(vec + 8 * i); len = LD32(vec + 8 * i + 4); };
    u32 b0, l0, b1, l1;
    if (d.type == DEV_FS) {
        if (cmd == 0x04) {                                                  // ReadDir: in0 = ruta, in1 = max (opcional); out0 = nombres, out1 = cuenta
            V(0, b0, l0);
            std::string p = host_path(cstr_n(b0, 64));
            u32 outbase, outlen, cntbase, cntlen; u32 oi = nin;
            u32 maxn = 0xFFFF; if (nin > 1) { u32 mb, ml; V(1, mb, ml); maxn = LD32(mb); }
            DIR* dir = opendir(p.c_str()); if (!dir) return -106;
            if (nout == 2) { V(oi, outbase, outlen); V(oi + 1, cntbase, cntlen); } else { outbase = 0; outlen = 0; V(oi, cntbase, cntlen); }
            u32 n = 0; struct dirent* e;
            while ((e = readdir(dir)) && n < maxn) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if (outbase) { for (u32 i = 0; i < 13; i++) ST8(outbase + n * 13 + i, 0); for (u32 i = 0; e->d_name[i] && i < 12; i++) ST8(outbase + n * 13 + i, e->d_name[i]); }
                n++;
            }
            closedir(dir); ST32(cntbase, n); return 0;
        }
        if (cmd == 0x0C) {                                                  // GetUsage: bloques e inodos usados
            V(nin, b1, l1); u32 ob, ol; V(nin + 1, ob, ol); ST32(b1, 1); ST32(ob, 1); return 0;
        }
    } else if (d.type == DEV_ES) {
        if (cmd == 0x20) { V(nin, b1, l1); ST32(b1, 0x00010001); ST32(b1 + 4, 0x57443945); return 0; }        // GetTitleId
        if (cmd == 0x1D) {                                                                                      // GetDataDir(titleId) -> "/title/00010001/57443945/data"
            V(nin, b1, l1); const char* s = "/title/00010001/57443945/data";
            for (u32 i = 0; i < 30; i++) ST8(b1 + i, i < strlen(s) ? s[i] : 0); return 0;
        }
    }
    rt_log("[ios] ioctlv no emulado: tipo %d cmd 0x%X\n", d.type, cmd);
    return -4;
}
HLE_FN(IOS_Ioctlv)       { return RET_I((u32)ios_ioctlv(a0, a1, a2, a3, a4)); }
HLE_FN(IOS_IoctlvAsync)      { return finish(ios_ioctlv(a0, a1, a2, a3, a4), a5, a6); }     // IOS_IoctlvAsync
