/* Generado por nyxc desde hello.nyx  (transpile-to-C backend).
 * NOTA: los buffers de interpolación se HOISTEAN al scope de la función
 * (no dentro de un statement-expression) para que el nyx_str no cuelgue. */
#include "nyxrt.h"

static inline nyx_i64 write(nyx_i32 a0, nyx_u8* a1, nyx_isize a2) {
    return (nyx_i64)__nyx_syscall6(1, (nyx_i64)a0, (nyx_i64)a1, (nyx_i64)a2, 0, 0, 0);
}
static inline nyx_i64 getpid(void) {
    return (nyx_i64)__nyx_syscall6(6, 0, 0, 0, 0, 0, 0);
}

nyx_i64 main(void) {
    nyx_i64 pid = getpid();

    /* msg := "hola desde nyx c! pid={pid}\n"  → buffer hoisted, luego se llena */
    char __b0[256];
    nyx_str msg = __nyx_fmt_begin(__b0, 256);
    __nyx_fmt_str(&msg, __b0, 256, (nyx_str){ "hola desde nyx c! pid=", 22 });
    __nyx_fmt_i64(&msg, __b0, 256, (nyx_i64)(pid));
    __nyx_fmt_str(&msg, __b0, 256, (nyx_str){ "\n", 1 });

    write(1, (nyx_u8*)(msg.ptr), (nyx_isize)(msg.len));
    return 0;
}
