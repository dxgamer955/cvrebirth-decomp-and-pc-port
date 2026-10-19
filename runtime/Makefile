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
else
BUILD := build
EXE := castlevania
OPT ?= -O2                                    # optimized for playing (OPT=-O1 compiles faster while developing)
CXXFLAGS ?= -std=c++17 $(OPT) -g0 -w -Iinclude -Igen -fno-strict-aliasing -ffp-contract=off $(shell pkg-config --cflags sdl2 glew)
LDLIBS := -rdynamic -ldl -lm $(shell pkg-config --libs sdl2 glew gl)
LDFLAGS :=
endif

OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(SRC))

$(BUILD)/$(EXE): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD)/%.o: %.cpp include/*.h gen/decls.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build build-win
