# Runtime nativo de Castlevania: The Adventure ReBirth (codigo elevado de game.dol + sustitutos del SDK).
# Linux:   make -j$(nproc)                (antes: python3 ../decomp/tools/lift_native.py)   -> build/castlevania
# Windows: en una terminal MSYS2 UCRT64: make -j8    (paquetes: gcc make pkgconf SDL2 glew)    -> build-win/castlevania.exe
CXX ?= clang++
SRC := $(wildcard src/*.cpp) $(wildcard gen/*.cpp)

ifeq ($(OS),Windows_NT)
BUILD := build-win
EXE := castlevania.exe
CXX := g++
# sdl2.pc anade -Dmain=SDL_main (exige SDL2main): se quita, el runtime usa su propio main (SDL_MAIN_HANDLED)
OPT ?= -O2
CXXFLAGS ?= -std=c++17 $(OPT) -g0 -w -Iinclude -Igen -fno-strict-aliasing -ffp-contract=off -DSDL_MAIN_HANDLED -DNOMINMAX -DWIN32_LEAN_AND_MEAN \
            $(filter-out -Dmain=SDL_main,$(shell pkg-config --cflags sdl2 glew))
LDLIBS := -lSDL2 -lglew32 -lopengl32 -lwinmm -lm
LDFLAGS := -static-libgcc -static-libstdc++
else
BUILD := build
EXE := castlevania
OPT ?= -O1                                    # Linux: compilacion rapida (OPT=-O2 para el maximo rendimiento)
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
