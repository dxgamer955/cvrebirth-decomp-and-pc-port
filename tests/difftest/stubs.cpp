// Funciones senuelo que se enlazan en la imagen de prueba: registran el orden de las
// llamadas virtuales que hace CTEngine::Update (ambas implementaciones las usan igual).
extern "C" {
unsigned g_log[4096];
unsigned g_log_n;

static void put(unsigned kind, void* a, void* b) {
    if (g_log_n + 3 <= 4096) {
        g_log[g_log_n++] = kind;
        g_log[g_log_n++] = (unsigned)a;
        g_log[g_log_n++] = (unsigned)b;
    }
}
void stub_exec(void* self) { put(1, self, 0); }
void stub_on_hit(void* self) { put(2, self, 0); }
int stub_hit(void* self, void* target) {
    put(3, self, target);
    return ((unsigned char*)self)[0x18];   // resultado configurable por objeto
}
}
