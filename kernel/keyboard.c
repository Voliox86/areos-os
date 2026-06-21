// ============================================================
// keyboard.c - Controlador de teclado PS/2 con Set 2 directo
// ============================================================
#include "kernel.h"

// ------------------------------------------------------------
// Estados del teclado
// ------------------------------------------------------------
static int shift_pressed = 0;
static int altgr_pressed = 0;      // Alt derecho (AltGr)
static int caps_lock = 0;
static int e0_prefix = 0;          // Flag para el prefijo 0xE0

// ------------------------------------------------------------
// Variable global de layout (definida aquí)
// ------------------------------------------------------------
int keyboard_layout = 1;           // 1 = Español por defecto

// ------------------------------------------------------------
// Tablas de mapeo para scancodes Set 2 (sin conversión)
// Los índices corresponden al scancode (0x00-0x7F)
// Para las teclas alfanuméricas, Set 2 y Set 1 comparten los mismos códigos.
// ------------------------------------------------------------

// --- Español (ES) ---
static char es_normal[0x80] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','\'',0xA1,'\b',
    '\t','q','w','e','r','t','y','u','i','o','p','`','+','\n',
    0,  'a','s','d','f','g','h','j','k','l',0xF1,0xB4,0xBA, 0,
    '<','z','x','c','v','b','n','m',',','.','-', 0, '*', 0, ' ',
    // El resto relleno con 0
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static char es_shift[0x80] = {
    0,   0,  '!','"',0xB7,'$','%','&','/','(',')','=','?',0xBF,'\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','^','*','\n',
    0,  'A','S','D','F','G','H','J','K','L',0xD1,0xA8,0xAA, 0,
    '>','Z','X','C','V','B','N','M',';',':','_', 0, '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static char es_altgr[0x80] = {
    0,   0,  '|','@','#','~',0x80,0xAC, 0, 0, 0, 0, 0, 0, 0,
    0,   0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,
    0,   0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,
    0,   0,  0,  0,  0,  0,  0,  0, 0, 0, 0, 0, 0, 0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

// --- US (QWERTY) ---
static char us_normal[0x80] = {
    0,   0,  '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`', 0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static char us_shift[0x80] = {
    0,   0,  '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~', 0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

// ============================================================
// Inicialización
// ============================================================
void init_keyboard(void) {
    // Vaciar buffer de entrada
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    // No forzamos Set 1, trabajamos en Set 2 por defecto
    shift_pressed = 0;
    altgr_pressed = 0;
    caps_lock = 0;
    e0_prefix = 0;
}

// ============================================================
// Conversión de scancode a carácter
// ============================================================
char scancode_to_ascii(uint8_t sc) {
    int pressed = !(sc & 0x80);
    sc &= 0x7F;

    // --- Prefijo 0xE0 (teclas extendidas) ---
    if (sc == 0xE0) {
        e0_prefix = 1;
        return 0;
    }

    if (e0_prefix) {
        e0_prefix = 0;
        // Alt derecho (AltGr) extendido
        if (sc == 0x38) {
            altgr_pressed = pressed;
            return 0;
        }
        // Otras teclas extendidas (ignorar)
        return 0;
    }

    // --- Modificadores estándar (sin E0) ---
    if (sc == 0x2A || sc == 0x36) { // Shift izquierdo y derecho
        shift_pressed = pressed;
        return 0;
    }
    if (sc == 0x3A) { // Caps Lock
        if (pressed) caps_lock = !caps_lock;
        return 0;
    }
    if (sc == 0x38) { // Alt izquierdo (no AltGr)
        return 0;
    }

    // Si es una tecla liberada, no generar carácter
    if (!pressed) return 0;

    // --- Seleccionar tabla según layout ---
    char c = 0;
    if (keyboard_layout == 0) { // US
        if (shift_pressed || caps_lock)
            c = us_shift[sc];
        else
            c = us_normal[sc];
    } else { // Español
        if (altgr_pressed)
            c = es_altgr[sc];
        else if (shift_pressed || caps_lock)
            c = es_shift[sc];
        else
            c = es_normal[sc];
    }
    return c;
}

// ============================================================
// Funciones de lectura
// ============================================================
char getchar_poll(void) {
    if ((inb(0x64) & 0x01) == 0) return 0;
    uint8_t sc = inb(0x60);
    return scancode_to_ascii(sc);
}

char getchar(void) {
    char c;
    while (1) {
        c = getchar_poll();
        if (c) return c;
        for (volatile int i = 0; i < 10000; i++);
    }
}

// ============================================================
// Función para cambiar de layout
// ============================================================
void set_keyboard_layout(int layout) {
    if (layout == 0 || layout == 1) {
        keyboard_layout = layout;
    }
}
