# Native runtime of Castlevania: The Adventure ReBirth (game.dol translated to C++ + replacements for the SDK).
# Easiest: run ../build.sh (Linux) or ../build.bat (Windows), which do every step. By hand:
# Linux:   make -j$(nproc)                (after: python3 ../decomp/tools/lift_native.py)   -> build/castlevania
# Windows: in an MSYS2 UCRT64 shell: make -j8    (packages: gcc make pkgconf SDL2 glew)     -> build-win/castlevania.exe
CXX ?= clang++
SRC := $(wildcard src/*.cpp) $(wildcard gen/*.cpp)

ifeq ($(OS),Windows_NT)
BUILD := build-win
EXE := castlevania.exe
CXX := g++
# sdl2.pc adds -Dmain=SDL_main (which needs SDL2main): it is removed, the runtime has its own main (SDL_MAIN_HANDLED)
OPT ?= -O2
CXXFLAGS ?= -std=c++17 $(OPT) -g0 -w -Iinclude -Igen -fno-strict-aliasing -ffp-contract=off -DSDL_MAIN_HANDLED -DNOMINMAX -DWIN32_LEAN_AND_MEAN \
            $(filter-out -Dmain=SDL_main,$(shell pkg-config --cflags sdl2 glew))
LDLIBS := -lSDL2 -lglew32 -lopengl32 -lwinmm -lm
LDFLAGS := -static-libgcc -static-libstdc++
ifeq ($(WINGUI),1)                            # WINGUI=1: no console window (package for players); output and errors go to a message box
EXE := castlevania-gui.exe
LDFLAGS += -mwindows
endif
else
BUILD := build
EXE := castlevania
OPT ?= -O2                                    # optimized for playing (OPT=-O1 compiles faster while developing)
CXXFLAGS ?= -std=c++17 $(OPT) -g0 -w -Iinclude -Igen -fno-strict-aliasing -ffp-contract=off $(shell pkg-config --cflags sdl2 glew)
LDLIBS := -rdynamic -ldl -lm $(shell pkg-config --libs sdl2 glew gl)
LDFLAGS :=
endif

OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC))

# Build data for the credits in the HOME menu (gen/build_info.h): rewritten only when it changes, so only home_menu.o is rebuilt
BUILD_ID := $(shell git -C .. describe --always --dirty --abbrev=7 2>/dev/null || echo unknown)
BUILD_DATE := $(shell date +%Y-%m-%d 2>/dev/null)
BUILD_INFO := \#define RT_BUILD_ID "$(BUILD_ID)"\n\#define RT_BUILD_DATE "$(BUILD_DATE)"
$(shell mkdir -p gen; printf '$(BUILD_INFO)\n' > gen/build_info.h.new; cmp -s gen/build_info.h.new gen/build_info.h && rm -f gen/build_info.h.new || mv gen/build_info.h.new gen/build_info.h)

# The game's own font for the interface (gen/ui_gamefont.h, from font/small1.psb.l of your copy): generated once if missing
# (build.sh also does it); without it the built-in 5x7 font is used
ifeq ($(wildcard gen/ui_gamefont.h),)
$(shell (python3 ../scripts/make_uifont.py || python ../scripts/make_uifont.py) >/dev/null 2>&1)
endif
# The stage maps of the map screen (gen/ui_stagemap.h, from the rooms of your copy): generated once if missing
# (build.sh also does it); without it the map screen says there is no map data
ifeq ($(wildcard gen/ui_stagemap.h),)
$(shell (python3 ../scripts/make_map.py || python ../scripts/make_map.py) >/dev/null 2>&1)
endif

$(BUILD)/$(EXE): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/src/home_menu.o: gen/build_info.h      # (after the main target: the first rule is make's default goal)
$(BUILD)/src/ui_font.o: $(wildcard gen/ui_gamefont.h)
$(BUILD)/src/stage_map.o: $(wildcard gen/ui_stagemap.h)

$(BUILD)/%.o: %.cpp include/*.h gen/decls.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build build-win
