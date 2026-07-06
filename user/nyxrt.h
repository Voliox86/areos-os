/* nyxrt.h — Nyx C runtime for NyxOS x86_64 userspace.
 * Linked with every .nyx program. Freestanding, no libc. */
#ifndef NYXRT_H
#define NYXRT_H
#include <stdint.h>

/* --- primitive types: Nyx C -> C --- */
typedef int8_t   nyx_i8;    typedef uint8_t  nyx_u8;
typedef int16_t  nyx_i16;   typedef uint16_t nyx_u16;
typedef int32_t  nyx_i32;   typedef uint32_t nyx_u32;
typedef int64_t  nyx_i64;   typedef uint64_t nyx_u64;
typedef int64_t  nyx_isize; typedef uint64_t nyx_usize;
typedef uint64_t nyx_addr;
typedef _Bool    nyx_bool;

/* str: pointer + length (non-owning). Literals use NYX_STR. */
typedef struct { const char* ptr; nyx_u64 len; } nyx_str;
#define NYX_STR(s) ((nyx_str){ (s), sizeof(s) - 1 })

/* slice []T (type-erased in the MVP; the checker monomorphizes later). */
typedef struct { void* ptr; nyx_u64 len; } nyx_slice;

/* Universal Result: ok/err fit in a register (i64 or pointer) — enough for the
 * MVP examples. The real backend monomorphizes Result<T,E> per type. */
typedef struct { nyx_bool is_err; nyx_i64 err; nyx_i64 ok; } nyx_result;
#define NYX_OK(v)  ((nyx_result){ 0, 0, (nyx_i64)(v) })
#define NYX_ERR(e) ((nyx_result){ 1, (nyx_i64)(e), 0 })

/* --- NyxOS x86_64 syscall ABI (identical to user/syscall.h) ---
 *   RAX=no, RDI/RSI/RDX/R10/R8/R9=args, clobbers RCX/R11, returns RAX. */
static inline nyx_i64 __nyx_syscall6(nyx_i64 no, nyx_i64 a1, nyx_i64 a2,
                                     nyx_i64 a3, nyx_i64 a4, nyx_i64 a5, nyx_i64 a6) {
    nyx_i64 ret;
    register nyx_i64 r10 __asm__("r10") = a4;
    register nyx_i64 r8  __asm__("r8")  = a5;
    register nyx_i64 r9  __asm__("r9")  = a6;
    __asm__ volatile ("syscall"
        : "=a"(ret)
        : "a"(no), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}

/* --- string-interpolation helpers (lowering of EX_STR_INTERP) ---
 * The caller hoists a char buf[] into function scope so the built nyx_str
 * does not dangle. See codegen note in the accompanying message. */
nyx_str __nyx_fmt_begin(char* buf, nyx_u64 cap);
void    __nyx_fmt_str(nyx_str* dst, char* buf, nyx_u64 cap, nyx_str s);
void    __nyx_fmt_i64(nyx_str* dst, char* buf, nyx_u64 cap, nyx_i64 v);

#endif /* NYXRT_H */
