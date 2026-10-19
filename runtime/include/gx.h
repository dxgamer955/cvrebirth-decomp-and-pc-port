#pragma once
// Emulacion de la GPU (Flipper): interprete del FIFO de comandos y estado de GX.
#include "native_lift.h"
#include <vector>

struct GxVertex {
    float pos[3];
    float nrm[3];
    float col[2][4];          // color0/color1 RGBA 0..1
    float tex[8][2];
    u8 posMtxIdx;
    u8 texMtxIdx[8];
};

struct GxState {
    // CP
    u32 vcdLo = 0, vcdHi = 0;
    u32 vatA[8] = {}, vatB[8] = {}, vatC[8] = {};
    u32 arrayBase[16] = {}, arrayStride[16] = {};
    // XF: memoria de matrices (0x0000-0x0FFF) y registros de control (0x1000-0x105F)
    float xfMem[0x1000] = {};
    u32 xfReg[0x60] = {};
    // BP
    u32 bp[256] = {};
};

extern GxState g_gx;

void gx_fifo_write(const u8* data, u32 n);              // bytes escritos en 0xCC008000 (orden big-endian de memoria)
void gx_stats_print();

// Hooks del renderizador (gx_render.cpp)
void gx_draw(u32 primitive, u32 vat, const GxVertex* v, u32 count);
void gx_bp_write(u32 reg, u32 value);                   // despues de actualizar g_gx.bp
void gx_xf_write(u32 addr, u32 value);
void gx_copy_efb(u32 value);                            // BP 0x52: copia del EFB (textura o XFB)
void gx_frame_present();
void gx_video_init();                                   // ventana SDL2 + OpenGL (RT_HEADLESS=1 la desactiva)
void gx_render_stats();                                // al hacer el retrazado de VI

u32 gx_input_buttons();                                // botones del Wii Remote emulados con el teclado (mascara WPAD)
void gx_input_pointer(float* x, float* y);              // puntero emulado con el raton, -1..1
