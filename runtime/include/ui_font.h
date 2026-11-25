#pragma once
// Texto e interfaz sencilla dibujados con OpenGL sobre la ventana (solo desde el hilo de render).
void ui_begin(int w, int h);                                                                     // prepara el estado de OpenGL para dibujar la interfaz
void ui_rect(float x, float y, float w, float h, float r, float g, float b, float a);
void ui_text(float x, float y, int sc, const char* text, float r, float g, float b);             // sc = factor de escala de los glifos
int ui_char_w();                                                                                 // avance de un caracter a escala 1 (8 la fuente del juego, 6 la 5x7)
int ui_char_h();                                                                                 // alto de un caracter a escala 1 (8 / 7)
float ui_text_width(const char* t, int sc);
void ui_image(unsigned tex, float x, float y, float w, float h);                                  // textura RGB(A) en un rectangulo
