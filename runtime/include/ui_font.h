#pragma once
// Texto e interfaz sencilla dibujados con OpenGL sobre la ventana (solo desde el hilo de render).
void ui_begin(int w, int h);                                                                     // prepara el estado de OpenGL para dibujar la interfaz
void ui_rect(float x, float y, float w, float h, float r, float g, float b, float a);
void ui_text(float x, float y, int sc, const char* text, float r, float g, float b);             // sc = factor de escala de los glifos 5x7
float ui_text_width(const char* t, int sc);
