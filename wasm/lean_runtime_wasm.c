/**
 * lean_runtime_wasm.c — Minimal Lean 4 runtime for WebAssembly
 *
 * Provides implementations of the non-inline Lean runtime functions needed
 * when compiling Lean-generated C code to WASM via Emscripten.
 *
 * Design decisions:
 *   • Memory: plain malloc/free with size prefix (matching lean.h's
 *     non-LEAN_SMALL_ALLOCATOR, non-LEAN_MIMALLOC path)
 *   • Single-threaded: no atomic ops (WASM is single-threaded)
 *   • Big Nats: abort (crypto code uses only small nats)
 *   • IO/filesystem: stubbed (pure computation only)
 *   • GMP: not required
 */

#include <lean/lean.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 *  1. Panic / Assertions
 * ================================================================ */

LEAN_EXPORT void lean_notify_assert(const char *fileName, int line, const char *condition) {
    fprintf(stderr, "LEAN ASSERT FAILED: %s:%d: %s\n", fileName, line, condition);
    abort();
}

LEAN_EXPORT void lean_internal_panic(const char *msg) {
    fprintf(stderr, "LEAN PANIC: %s\n", msg);
    abort();
}

LEAN_EXPORT void lean_internal_panic_out_of_memory(void) {
    fprintf(stderr, "LEAN PANIC: out of memory\n");
    abort();
}

LEAN_EXPORT void lean_internal_panic_unreachable(void) {
    fprintf(stderr, "LEAN PANIC: unreachable\n");
    abort();
}

LEAN_EXPORT void lean_internal_panic_rc_overflow(void) {
    fprintf(stderr, "LEAN PANIC: rc overflow\n");
    abort();
}

LEAN_EXPORT lean_object *lean_panic_fn(lean_object *default_val, lean_object *msg) {
    lean_dec(msg);
    return default_val;
}

LEAN_EXPORT void lean_set_exit_on_panic(bool flag) { (void)flag; }
LEAN_EXPORT void lean_set_panic_messages(bool flag) { (void)flag; }
LEAN_EXPORT void lean_panic(const char *msg, bool force_stderr) {
    (void)force_stderr;
    fprintf(stderr, "LEAN PANIC: %s\n", msg);
}

/* ================================================================
 *  2. Memory Management
 * ================================================================ */

/*
 * Allocation scheme (matching lean.h non-LEAN_SMALL_ALLOCATOR path):
 *   [size_t: sz] [lean_object ...]
 *                ^-- returned pointer
 *
 * lean_small_object_size(o) = *((size_t*)o - 1) is already inline in lean.h.
 */

LEAN_EXPORT void lean_inc_heartbeat(void) {
    /* no-op in WASM */
}

LEAN_EXPORT lean_object *lean_alloc_object(size_t sz) {
    lean_inc_heartbeat();
    void *mem = malloc(sizeof(size_t) + sz);
    if (!mem) lean_internal_panic_out_of_memory();
    *(size_t *)mem = sz;
    return (lean_object *)((size_t *)mem + 1);
}

LEAN_EXPORT void lean_free_object(lean_object *o) {
    size_t *ptr = (size_t *)o - 1;
    free(ptr);
}

/* Called from lean_alloc_ctor_memory when LEAN_SMALL_ALLOCATOR is defined.
   In WASM it is NOT called (the header inlines lean_alloc_small_object instead),
   but we provide a stub for link-time safety. */
LEAN_EXPORT void *lean_alloc_small(unsigned sz, unsigned slot_idx) {
    (void)slot_idx;
    lean_inc_heartbeat();
    void *mem = malloc(sizeof(size_t) + sz);
    if (!mem) lean_internal_panic_out_of_memory();
    *(size_t *)mem = sz;
    return (size_t *)mem + 1;
}

LEAN_EXPORT void lean_free_small(void *p) {
    size_t *ptr = (size_t *)p - 1;
    free(ptr);
}

LEAN_EXPORT unsigned lean_small_mem_size(void *p) {
    return (unsigned)(*((size_t *)p - 1));
}

/* C23 free_sized — needed by lean_free_small_object inline in lean.h */
#if !defined(__STDC_VERSION_STDLIB_H__) || __STDC_VERSION_STDLIB_H__ < 202311L
void free_sized(void *ptr, size_t sz) {
    (void)sz;
    free(ptr);
}
#endif

/* ================================================================
 *  3. Object Byte Size / Deallocation
 * ================================================================ */

LEAN_EXPORT size_t lean_object_byte_size(lean_object *o) {
    uint8_t tag = o->m_tag;
    if (tag == LeanScalarArray) {
        unsigned elem_sz = o->m_other;
        return sizeof(lean_sarray_object) + elem_sz * ((lean_sarray_object *)o)->m_capacity;
    } else if (tag == LeanString) {
        return sizeof(lean_string_object) + ((lean_string_object *)o)->m_capacity;
    } else {
        /* Constructors, closures, arrays, refs: stored size */
        return *((size_t *)o - 1);
    }
}

LEAN_EXPORT size_t lean_object_data_byte_size(lean_object *o) {
    uint8_t tag = o->m_tag;
    if (tag == LeanScalarArray) {
        unsigned elem_sz = o->m_other;
        return sizeof(lean_sarray_object) + elem_sz * ((lean_sarray_object *)o)->m_size;
    } else if (tag == LeanString) {
        return sizeof(lean_string_object) + ((lean_string_object *)o)->m_size;
    } else {
        return lean_object_byte_size(o);
    }
}

/* Recursive deallocation when m_rc reaches 1 */
LEAN_EXPORT void lean_dec_ref_cold(lean_object *o) {
    if (o->m_rc == 1) {
        uint8_t tag = o->m_tag;
        if (tag <= LeanMaxCtorTag) {
            unsigned n = o->m_other;
            lean_ctor_object *c = (lean_ctor_object *)o;
            for (unsigned i = 0; i < n; i++)
                lean_dec(c->m_objs[i]);
        } else if (tag == LeanClosure) {
            lean_closure_object *c = (lean_closure_object *)o;
            for (unsigned i = 0; i < c->m_num_fixed; i++)
                lean_dec(c->m_objs[i]);
        } else if (tag == LeanArray) {
            lean_array_object *a = (lean_array_object *)o;
            for (size_t i = 0; i < a->m_size; i++)
                lean_dec(a->m_data[i]);
        } else if (tag == LeanRef) {
            lean_ref_object *r = (lean_ref_object *)o;
            if (r->m_value) lean_dec(r->m_value);
        }
        /* ScalarArray, String, MPZ: no children */
        lean_free_object(o);
    }
    /* m_rc < 0 (multi-threaded): ignore in WASM */
}

LEAN_EXPORT void lean_mark_persistent(lean_object *o) {
    if (!lean_is_scalar(o))
        o->m_rc = 0;
}

LEAN_EXPORT void lean_mark_mt(lean_object *o) {
    (void)o;
}

/* ================================================================
 *  4. Closure Application
 * ================================================================ */

typedef lean_object *(*lean_cfun1)(lean_object *);
typedef lean_object *(*lean_cfun2)(lean_object *, lean_object *);
typedef lean_object *(*lean_cfun3)(lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun4)(lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun5)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun6)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun7)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun8)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun9)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun10)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun11)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun12)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun13)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun14)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun15)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);
typedef lean_object *(*lean_cfun16)(lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *, lean_object *);

static lean_object *lean_call_with_args(void *fn, unsigned arity, lean_object **a) {
    switch (arity) {
    case 1:  return ((lean_cfun1)fn)(a[0]);
    case 2:  return ((lean_cfun2)fn)(a[0], a[1]);
    case 3:  return ((lean_cfun3)fn)(a[0], a[1], a[2]);
    case 4:  return ((lean_cfun4)fn)(a[0], a[1], a[2], a[3]);
    case 5:  return ((lean_cfun5)fn)(a[0], a[1], a[2], a[3], a[4]);
    case 6:  return ((lean_cfun6)fn)(a[0], a[1], a[2], a[3], a[4], a[5]);
    case 7:  return ((lean_cfun7)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
    case 8:  return ((lean_cfun8)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    case 9:  return ((lean_cfun9)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
    case 10: return ((lean_cfun10)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
    case 11: return ((lean_cfun11)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10]);
    case 12: return ((lean_cfun12)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11]);
    case 13: return ((lean_cfun13)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12]);
    case 14: return ((lean_cfun14)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13]);
    case 15: return ((lean_cfun15)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14]);
    case 16: return ((lean_cfun16)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13], a[14], a[15]);
    default: lean_internal_panic("lean_call_with_args: arity > 16"); return NULL;
    }
}

LEAN_EXPORT lean_object *lean_apply_m(lean_object *f, unsigned n, lean_object **args) {
    lean_closure_object *c = lean_to_closure(f);
    unsigned remaining = c->m_arity - c->m_num_fixed;

    if (n < remaining) {
        /* Under-application: add args to closure */
        lean_object *nc = lean_alloc_closure(c->m_fun, c->m_arity, c->m_num_fixed + n);
        lean_closure_object *nco = lean_to_closure(nc);
        for (unsigned i = 0; i < c->m_num_fixed; i++) {
            lean_inc(c->m_objs[i]);
            nco->m_objs[i] = c->m_objs[i];
        }
        for (unsigned i = 0; i < n; i++)
            nco->m_objs[c->m_num_fixed + i] = args[i];
        lean_dec(f);
        return nc;
    }

    /* Exact or over-application: collect all args for the call */
    lean_object *all_args[16];
    for (unsigned i = 0; i < c->m_num_fixed; i++) {
        lean_inc(c->m_objs[i]);
        all_args[i] = c->m_objs[i];
    }
    for (unsigned i = 0; i < remaining; i++)
        all_args[c->m_num_fixed + i] = args[i];

    lean_object *res = lean_call_with_args(c->m_fun, c->m_arity, all_args);
    lean_dec(f);

    if (n > remaining) {
        /* Over-application: apply result to remaining args */
        return lean_apply_m(res, n - remaining, args + remaining);
    }
    return res;
}

LEAN_EXPORT lean_object *lean_apply_n(lean_object *f, unsigned n, lean_object **args) {
    return lean_apply_m(f, n, args);
}

LEAN_EXPORT lean_object *lean_apply_1(lean_object *f, lean_object *a1) {
    lean_object *args[1] = {a1};
    return lean_apply_m(f, 1, args);
}

LEAN_EXPORT lean_object *lean_apply_2(lean_object *f, lean_object *a1, lean_object *a2) {
    lean_object *args[2] = {a1, a2};
    return lean_apply_m(f, 2, args);
}

LEAN_EXPORT lean_object *lean_apply_3(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3) {
    lean_object *args[3] = {a1, a2, a3};
    return lean_apply_m(f, 3, args);
}

LEAN_EXPORT lean_object *lean_apply_4(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4) {
    lean_object *args[4] = {a1, a2, a3, a4};
    return lean_apply_m(f, 4, args);
}

LEAN_EXPORT lean_object *lean_apply_5(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5) {
    lean_object *args[5] = {a1, a2, a3, a4, a5};
    return lean_apply_m(f, 5, args);
}

LEAN_EXPORT lean_object *lean_apply_6(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6) {
    lean_object *args[6] = {a1, a2, a3, a4, a5, a6};
    return lean_apply_m(f, 6, args);
}

LEAN_EXPORT lean_object *lean_apply_7(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6, lean_object *a7) {
    lean_object *args[7] = {a1, a2, a3, a4, a5, a6, a7};
    return lean_apply_m(f, 7, args);
}

LEAN_EXPORT lean_object *lean_apply_8(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6, lean_object *a7, lean_object *a8) {
    lean_object *args[8] = {a1, a2, a3, a4, a5, a6, a7, a8};
    return lean_apply_m(f, 8, args);
}

LEAN_EXPORT lean_object *lean_apply_9(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6, lean_object *a7, lean_object *a8, lean_object *a9) {
    lean_object *args[9] = {a1, a2, a3, a4, a5, a6, a7, a8, a9};
    return lean_apply_m(f, 9, args);
}

LEAN_EXPORT lean_object *lean_apply_10(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6, lean_object *a7, lean_object *a8, lean_object *a9, lean_object *a10) {
    lean_object *args[10] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10};
    return lean_apply_m(f, 10, args);
}

LEAN_EXPORT lean_object *lean_apply_11(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6, lean_object *a7, lean_object *a8, lean_object *a9, lean_object *a10, lean_object *a11) {
    lean_object *args[11] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11};
    return lean_apply_m(f, 11, args);
}

LEAN_EXPORT lean_object *lean_apply_12(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6, lean_object *a7, lean_object *a8, lean_object *a9, lean_object *a10, lean_object *a11, lean_object *a12) {
    lean_object *args[12] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12};
    return lean_apply_m(f, 12, args);
}

LEAN_EXPORT lean_object *lean_apply_13(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6, lean_object *a7, lean_object *a8, lean_object *a9, lean_object *a10, lean_object *a11, lean_object *a12, lean_object *a13) {
    lean_object *args[13] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13};
    return lean_apply_m(f, 13, args);
}

LEAN_EXPORT lean_object *lean_apply_14(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6, lean_object *a7, lean_object *a8, lean_object *a9, lean_object *a10, lean_object *a11, lean_object *a12, lean_object *a13, lean_object *a14) {
    lean_object *args[14] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14};
    return lean_apply_m(f, 14, args);
}

LEAN_EXPORT lean_object *lean_apply_15(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6, lean_object *a7, lean_object *a8, lean_object *a9, lean_object *a10, lean_object *a11, lean_object *a12, lean_object *a13, lean_object *a14, lean_object *a15) {
    lean_object *args[15] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15};
    return lean_apply_m(f, 15, args);
}

LEAN_EXPORT lean_object *lean_apply_16(lean_object *f, lean_object *a1, lean_object *a2, lean_object *a3, lean_object *a4, lean_object *a5, lean_object *a6, lean_object *a7, lean_object *a8, lean_object *a9, lean_object *a10, lean_object *a11, lean_object *a12, lean_object *a13, lean_object *a14, lean_object *a15, lean_object *a16) {
    lean_object *args[16] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16};
    return lean_apply_m(f, 16, args);
}

/* ================================================================
 *  5. Array Operations
 * ================================================================ */

LEAN_EXPORT lean_object *lean_array_mk(lean_obj_arg l) {
    /* Convert a List to an Array */
    size_t len = 0;
    lean_object *p = l;
    while (!lean_is_scalar(p)) { len++; p = lean_ctor_get(p, 1); }

    lean_object *arr = lean_alloc_array(len, len);
    lean_array_object *a = lean_to_array(arr);
    a->m_size = len;
    p = l;
    for (size_t i = 0; i < len; i++) {
        lean_object *hd = lean_ctor_get(p, 0);
        lean_inc(hd);
        a->m_data[i] = hd;
        p = lean_ctor_get(p, 1);
    }
    lean_dec(l);
    return arr;
}

LEAN_EXPORT lean_object *lean_array_to_list(lean_obj_arg a) {
    lean_array_object *arr = lean_to_array(a);
    lean_object *r = lean_box(0); /* List.nil */
    for (size_t i = arr->m_size; i > 0; i--) {
        lean_object *hd = arr->m_data[i - 1];
        lean_inc(hd);
        lean_object *cons = lean_alloc_ctor(1, 2, 0);
        lean_ctor_set(cons, 0, hd);
        lean_ctor_set(cons, 1, r);
        r = cons;
    }
    lean_dec(a);
    return r;
}

LEAN_EXPORT lean_object *lean_mk_array(lean_obj_arg n, lean_obj_arg v) {
    size_t sz = lean_unbox(n);
    lean_object *arr = lean_alloc_array(sz, sz);
    lean_array_object *a = lean_to_array(arr);
    a->m_size = sz;
    for (size_t i = 0; i < sz; i++) {
        lean_inc(v);
        a->m_data[i] = v;
    }
    lean_dec(v);
    return arr;
}

LEAN_EXPORT lean_obj_res lean_copy_expand_array(lean_obj_arg a, bool expand) {
    lean_array_object *src = lean_to_array(a);
    size_t sz = src->m_size;
    size_t cap = expand ? (sz < 4 ? 4 : sz * 2) : sz;
    lean_object *dst = lean_alloc_array(sz, cap);
    lean_array_object *d = lean_to_array(dst);
    d->m_size = sz;
    for (size_t i = 0; i < sz; i++) {
        lean_inc(src->m_data[i]);
        d->m_data[i] = src->m_data[i];
    }
    lean_dec(a);
    return dst;
}

LEAN_EXPORT lean_object *lean_array_push(lean_obj_arg a, lean_obj_arg v) {
    lean_array_object *o = lean_to_array(a);
    if (lean_is_exclusive(a) && o->m_size < o->m_capacity) {
        o->m_data[o->m_size++] = v;
        return a;
    }
    lean_object *r = lean_copy_expand_array(a, 1);
    lean_array_object *ro = lean_to_array(r);
    if (ro->m_size >= ro->m_capacity) {
        r = lean_copy_expand_array(r, 1);
        ro = lean_to_array(r);
    }
    ro->m_data[ro->m_size++] = v;
    return r;
}

LEAN_EXPORT lean_obj_res lean_array_get_panic(lean_obj_arg def_val) {
    return def_val;
}

LEAN_EXPORT lean_obj_res lean_array_set_panic(lean_obj_arg a, lean_obj_arg v) {
    lean_dec(v);
    return a;
}

/* ================================================================
 *  6. ByteArray Operations
 * ================================================================ */

LEAN_EXPORT lean_obj_res lean_byte_array_mk(lean_obj_arg a) {
    /* Convert a List Nat to ByteArray */
    size_t len = 0;
    lean_object *p = a;
    while (!lean_is_scalar(p)) { len++; p = lean_ctor_get(p, 1); }

    lean_object *arr = lean_alloc_sarray(1, len, len);
    lean_sarray_object *o = lean_to_sarray(arr);
    p = a;
    for (size_t i = 0; i < len; i++) {
        lean_object *hd = lean_ctor_get(p, 0);
        o->m_data[i] = (uint8_t)lean_unbox(hd);
        p = lean_ctor_get(p, 1);
    }
    lean_dec(a);
    return arr;
}

LEAN_EXPORT lean_obj_res lean_byte_array_data(lean_obj_arg a) {
    /* Convert ByteArray to List Nat */
    lean_sarray_object *o = lean_to_sarray(a);
    lean_object *r = lean_box(0);
    for (size_t i = o->m_size; i > 0; i--) {
        lean_object *cons = lean_alloc_ctor(1, 2, 0);
        lean_ctor_set(cons, 0, lean_box(o->m_data[i - 1]));
        lean_ctor_set(cons, 1, r);
        r = cons;
    }
    lean_dec(a);
    return r;
}

LEAN_EXPORT lean_obj_res lean_copy_byte_array(lean_obj_arg a) {
    lean_sarray_object *src = lean_to_sarray(a);
    size_t sz = src->m_size;
    lean_object *dst = lean_alloc_sarray(1, sz, sz);
    memcpy(lean_sarray_cptr(dst), src->m_data, sz);
    lean_dec(a);
    return dst;
}

LEAN_EXPORT lean_obj_res lean_byte_array_push(lean_obj_arg a, uint8_t b) {
    lean_sarray_object *o = lean_to_sarray(a);
    if (lean_is_exclusive(a) && o->m_size < o->m_capacity) {
        o->m_data[o->m_size++] = b;
        return a;
    }
    size_t sz = o->m_size;
    size_t cap = sz < 4 ? 8 : sz * 2;
    lean_object *dst = lean_alloc_sarray(1, sz + 1, cap);
    lean_sarray_object *d = lean_to_sarray(dst);
    memcpy(d->m_data, o->m_data, sz);
    d->m_data[sz] = b;
    lean_dec(a);
    return dst;
}

LEAN_EXPORT lean_obj_res lean_byte_array_copy_slice(b_lean_obj_arg src, b_lean_obj_arg srcOff,
                                                      lean_obj_arg dst, b_lean_obj_arg dstOff,
                                                      b_lean_obj_arg len, uint8_t exact) {
    lean_sarray_object *s = lean_to_sarray(src);
    size_t ss = lean_unbox(srcOff);
    size_t ds = lean_unbox(dstOff);
    size_t n = lean_unbox(len);
    size_t src_sz = s->m_size;

    if (ss > src_sz) ss = src_sz;
    if (ss + n > src_sz) n = src_sz - ss;

    lean_sarray_object *d = lean_to_sarray(dst);
    size_t dst_sz = d->m_size;
    if (ds > dst_sz) ds = dst_sz;

    size_t new_sz = ds + n;
    if (new_sz < dst_sz) new_sz = dst_sz;

    if (!lean_is_exclusive(dst) || new_sz > d->m_capacity) {
        size_t cap = exact ? new_sz : (new_sz < 8 ? 8 : new_sz * 2);
        lean_object *new_dst = lean_alloc_sarray(1, new_sz, cap);
        lean_sarray_object *nd = lean_to_sarray(new_dst);
        memcpy(nd->m_data, d->m_data, ds);
        memcpy(nd->m_data + ds, s->m_data + ss, n);
        if (new_sz > ds + n)
            memcpy(nd->m_data + ds + n, d->m_data + ds + n, new_sz - ds - n);
        lean_dec(dst);
        return new_dst;
    }

    memmove(d->m_data + ds, s->m_data + ss, n);
    d->m_size = new_sz;
    return dst;
}

LEAN_EXPORT uint64_t lean_byte_array_hash(b_lean_obj_arg a) {
    lean_sarray_object *o = lean_to_sarray(a);
    uint64_t h = 7;
    for (size_t i = 0; i < o->m_size; i++)
        h = h * 31 + o->m_data[i];
    return h;
}

/* ================================================================
 *  7. String Operations
 * ================================================================ */

LEAN_EXPORT size_t lean_utf8_strlen(const char *str) {
    size_t len = 0;
    while (*str) {
        unsigned char c = (unsigned char)*str;
        if (c < 0x80)      str += 1;
        else if (c < 0xE0) str += 2;
        else if (c < 0xF0) str += 3;
        else                str += 4;
        len++;
    }
    return len;
}

LEAN_EXPORT size_t lean_utf8_n_strlen(const char *str, size_t n) {
    size_t len = 0;
    const char *end = str + n;
    while (str < end) {
        unsigned char c = (unsigned char)*str;
        if (c < 0x80)      str += 1;
        else if (c < 0xE0) str += 2;
        else if (c < 0xF0) str += 3;
        else                str += 4;
        len++;
    }
    return len;
}

LEAN_EXPORT lean_obj_res lean_mk_string_unchecked(const char *s, size_t sz, size_t len) {
    size_t rsz = sz + 1;
    lean_object *o = lean_alloc_object(sizeof(lean_string_object) + rsz);
    lean_set_st_header(o, LeanString, 0);
    lean_string_object *so = lean_to_string(o);
    so->m_size = rsz;
    so->m_capacity = rsz;
    so->m_length = len;
    memcpy(so->m_data, s, sz);
    so->m_data[sz] = '\0';
    return o;
}

LEAN_EXPORT lean_obj_res lean_mk_string_from_bytes(const char *s, size_t sz) {
    return lean_mk_string_unchecked(s, sz, lean_utf8_n_strlen(s, sz));
}

LEAN_EXPORT lean_obj_res lean_mk_string(const char *s) {
    size_t sz = strlen(s);
    return lean_mk_string_unchecked(s, sz, lean_utf8_strlen(s));
}

LEAN_EXPORT lean_obj_res lean_mk_ascii_string_unchecked(const char *s) {
    size_t sz = strlen(s);
    return lean_mk_string_unchecked(s, sz, sz);
}

LEAN_EXPORT lean_obj_res lean_mk_string_from_bytes_unchecked(const char *s, size_t sz) {
    return lean_mk_string_unchecked(s, sz, lean_utf8_n_strlen(s, sz));
}

LEAN_EXPORT lean_obj_res lean_string_push(lean_obj_arg s, uint32_t c) {
    lean_string_object *so = lean_to_string(s);
    char buf[4];
    unsigned char_sz;
    if (c < 0x80) { buf[0] = (char)c; char_sz = 1; }
    else if (c < 0x800) {
        buf[0] = (char)(0xC0 | (c >> 6));
        buf[1] = (char)(0x80 | (c & 0x3F));
        char_sz = 2;
    } else if (c < 0x10000) {
        buf[0] = (char)(0xE0 | (c >> 12));
        buf[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (c & 0x3F));
        char_sz = 3;
    } else {
        buf[0] = (char)(0xF0 | (c >> 18));
        buf[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (c & 0x3F));
        char_sz = 4;
    }

    size_t old_bsz = so->m_size; /* includes NUL */
    size_t new_bsz = old_bsz + char_sz;

    if (lean_is_exclusive(s) && new_bsz <= so->m_capacity) {
        memcpy(so->m_data + old_bsz - 1, buf, char_sz);
        so->m_data[new_bsz - 1] = '\0';
        so->m_size = new_bsz;
        so->m_length++;
        return s;
    }

    size_t new_cap = new_bsz < 16 ? 16 : new_bsz * 2;
    lean_object *r = lean_alloc_object(sizeof(lean_string_object) + new_cap);
    lean_set_st_header(r, LeanString, 0);
    lean_string_object *ro = lean_to_string(r);
    memcpy(ro->m_data, so->m_data, old_bsz - 1);
    memcpy(ro->m_data + old_bsz - 1, buf, char_sz);
    ro->m_data[new_bsz - 1] = '\0';
    ro->m_size = new_bsz;
    ro->m_capacity = new_cap;
    ro->m_length = so->m_length + 1;
    lean_dec(s);
    return r;
}

LEAN_EXPORT lean_obj_res lean_string_append(lean_obj_arg s1, b_lean_obj_arg s2) {
    lean_string_object *o1 = lean_to_string(s1);
    lean_string_object *o2 = lean_to_string(s2);
    size_t sz1 = o1->m_size - 1; /* without NUL */
    size_t sz2 = o2->m_size - 1;
    size_t new_bsz = sz1 + sz2 + 1;

    if (lean_is_exclusive(s1) && new_bsz <= o1->m_capacity) {
        memcpy(o1->m_data + sz1, o2->m_data, sz2 + 1);
        o1->m_size = new_bsz;
        o1->m_length += o2->m_length;
        return s1;
    }

    size_t cap = new_bsz < 16 ? 16 : new_bsz * 2;
    lean_object *r = lean_alloc_object(sizeof(lean_string_object) + cap);
    lean_set_st_header(r, LeanString, 0);
    lean_string_object *ro = lean_to_string(r);
    memcpy(ro->m_data, o1->m_data, sz1);
    memcpy(ro->m_data + sz1, o2->m_data, sz2 + 1);
    ro->m_size = new_bsz;
    ro->m_capacity = cap;
    ro->m_length = o1->m_length + o2->m_length;
    lean_dec(s1);
    return r;
}

LEAN_EXPORT lean_obj_res lean_string_mk(lean_obj_arg cs) {
    /* Convert List Char to String */
    size_t cap = 64;
    lean_object *r = lean_alloc_object(sizeof(lean_string_object) + cap);
    lean_set_st_header(r, LeanString, 0);
    lean_string_object *ro = lean_to_string(r);
    ro->m_size = 1;
    ro->m_capacity = cap;
    ro->m_length = 0;
    ro->m_data[0] = '\0';

    lean_object *p = cs;
    while (!lean_is_scalar(p)) {
        uint32_t ch = lean_unbox(lean_ctor_get(p, 0));
        r = lean_string_push(r, ch);
        ro = lean_to_string(r);
        p = lean_ctor_get(p, 1);
    }
    lean_dec(cs);
    return r;
}

LEAN_EXPORT lean_obj_res lean_string_data(lean_obj_arg s) {
    /* Convert String to List Char */
    lean_string_object *so = lean_to_string(s);
    lean_object *r = lean_box(0);
    const char *p = so->m_data;
    const char *end = p + so->m_size - 1;

    /* Build in reverse, then reverse */
    size_t len = so->m_length;
    uint32_t *chars = (uint32_t *)malloc(len * sizeof(uint32_t));
    size_t idx = 0;
    while (p < end && idx < len) {
        unsigned char c = (unsigned char)*p;
        uint32_t ch;
        if (c < 0x80) { ch = c; p += 1; }
        else if (c < 0xE0) { ch = (c & 0x1F) << 6 | (p[1] & 0x3F); p += 2; }
        else if (c < 0xF0) { ch = (c & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F); p += 3; }
        else { ch = (c & 0x07) << 18 | (p[1] & 0x3F) << 12 | (p[2] & 0x3F) << 6 | (p[3] & 0x3F); p += 4; }
        chars[idx++] = ch;
    }

    for (size_t i = idx; i > 0; i--) {
        lean_object *cons = lean_alloc_ctor(1, 2, 0);
        lean_ctor_set(cons, 0, lean_box(chars[i - 1]));
        lean_ctor_set(cons, 1, r);
        r = cons;
    }
    free(chars);
    lean_dec(s);
    return r;
}

LEAN_EXPORT lean_obj_res lean_string_utf8_extract(b_lean_obj_arg s, b_lean_obj_arg b, b_lean_obj_arg e) {
    lean_string_object *so = lean_to_string(s);
    size_t bv = lean_unbox(b);
    size_t ev = lean_unbox(e);
    size_t sz = so->m_size - 1;
    if (bv > sz) bv = sz;
    if (ev > sz) ev = sz;
    if (bv >= ev) return lean_mk_string_unchecked("", 0, 0);
    return lean_mk_string_from_bytes(so->m_data + bv, ev - bv);
}

LEAN_EXPORT lean_obj_res lean_string_utf8_set(lean_obj_arg s, b_lean_obj_arg i, uint32_t c) {
    /* Simplified: just return the string unchanged for now */
    (void)i; (void)c;
    return s;
}

LEAN_EXPORT uint32_t lean_string_utf8_get(b_lean_obj_arg s, b_lean_obj_arg i) {
    lean_string_object *so = lean_to_string(s);
    size_t pos = lean_unbox(i);
    if (pos >= so->m_size - 1) return 0;
    unsigned char c = (unsigned char)so->m_data[pos];
    if (c < 0x80) return c;
    if (c < 0xE0) return (c & 0x1F) << 6 | (so->m_data[pos + 1] & 0x3F);
    if (c < 0xF0) return (c & 0x0F) << 12 | (so->m_data[pos + 1] & 0x3F) << 6 | (so->m_data[pos + 2] & 0x3F);
    return (c & 0x07) << 18 | (so->m_data[pos + 1] & 0x3F) << 12 | (so->m_data[pos + 2] & 0x3F) << 6 | (so->m_data[pos + 3] & 0x3F);
}

LEAN_EXPORT uint32_t lean_string_utf8_get_fast_cold(const char *str, size_t i, size_t size, unsigned char c) {
    (void)size;
    if (c < 0xE0) return (c & 0x1F) << 6 | (str[i + 1] & 0x3F);
    if (c < 0xF0) return (c & 0x0F) << 12 | (str[i + 1] & 0x3F) << 6 | (str[i + 2] & 0x3F);
    return (c & 0x07) << 18 | (str[i + 1] & 0x3F) << 12 | (str[i + 2] & 0x3F) << 6 | (str[i + 3] & 0x3F);
}

LEAN_EXPORT lean_obj_res lean_string_utf8_next(b_lean_obj_arg s, b_lean_obj_arg i) {
    lean_string_object *so = lean_to_string(s);
    size_t pos = lean_unbox(i);
    size_t sz = so->m_size - 1;
    if (pos >= sz) return lean_box(sz);
    unsigned char c = (unsigned char)so->m_data[pos];
    unsigned step = 1;
    if (c >= 0x80) { if (c < 0xE0) step = 2; else if (c < 0xF0) step = 3; else step = 4; }
    size_t r = pos + step;
    if (r > sz) r = sz;
    return lean_box(r);
}

LEAN_EXPORT lean_obj_res lean_string_utf8_next_fast_cold(size_t i, unsigned char c) {
    unsigned step = 2;
    if (c >= 0xE0) { step = (c < 0xF0) ? 3 : 4; }
    return lean_box(i + step);
}

LEAN_EXPORT lean_obj_res lean_string_utf8_prev(b_lean_obj_arg s, b_lean_obj_arg i) {
    lean_string_object *so = lean_to_string(s);
    size_t pos = lean_unbox(i);
    if (pos == 0) return lean_box(0);
    pos--;
    while (pos > 0 && (so->m_data[pos] & 0xC0) == 0x80) pos--;
    return lean_box(pos);
}

LEAN_EXPORT lean_obj_res lean_string_of_usize(size_t n) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%zu", n);
    return lean_mk_string(buf);
}

LEAN_EXPORT bool lean_string_eq_cold(b_lean_obj_arg s1, b_lean_obj_arg s2) {
    lean_string_object *o1 = lean_to_string(s1);
    lean_string_object *o2 = lean_to_string(s2);
    if (o1->m_size != o2->m_size) return false;
    return memcmp(o1->m_data, o2->m_data, o1->m_size) == 0;
}

LEAN_EXPORT bool lean_string_lt(b_lean_obj_arg s1, b_lean_obj_arg s2) {
    lean_string_object *o1 = lean_to_string(s1);
    lean_string_object *o2 = lean_to_string(s2);
    size_t m = o1->m_size < o2->m_size ? o1->m_size : o2->m_size;
    int c = memcmp(o1->m_data, o2->m_data, m);
    return c < 0 || (c == 0 && o1->m_size < o2->m_size);
}

LEAN_EXPORT uint64_t lean_string_hash(b_lean_obj_arg s) {
    lean_string_object *o = lean_to_string(s);
    uint64_t h = 7;
    for (size_t i = 0; i < o->m_size - 1; i++)
        h = h * 31 + (unsigned char)o->m_data[i];
    return h;
}

LEAN_EXPORT uint8_t lean_string_memcmp(b_lean_obj_arg s1, b_lean_obj_arg s2,
                                        b_lean_obj_arg lstart, b_lean_obj_arg rstart,
                                        b_lean_obj_arg len) {
    lean_string_object *o1 = lean_to_string(s1);
    lean_string_object *o2 = lean_to_string(s2);
    size_t ls = lean_unbox(lstart);
    size_t rs = lean_unbox(rstart);
    size_t n = lean_unbox(len);
    size_t sz1 = o1->m_size - 1;
    size_t sz2 = o2->m_size - 1;
    if (ls + n > sz1 || rs + n > sz2) return 0;
    return memcmp(o1->m_data + ls, o2->m_data + rs, n) == 0;
}

LEAN_EXPORT uint8_t lean_string_validate_utf8(b_lean_obj_arg s) {
    lean_string_object *o = lean_to_string(s);
    const unsigned char *p = (const unsigned char *)o->m_data;
    const unsigned char *end = p + o->m_size - 1;
    while (p < end) {
        if (*p < 0x80) { p++; continue; }
        if (*p < 0xC2) return 0;
        if (*p < 0xE0) { if (end - p < 2 || (p[1] & 0xC0) != 0x80) return 0; p += 2; continue; }
        if (*p < 0xF0) { if (end - p < 3 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return 0; p += 3; continue; }
        if (*p < 0xF5) { if (end - p < 4 || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return 0; p += 4; continue; }
        return 0;
    }
    return 1;
}

LEAN_EXPORT lean_obj_res lean_string_from_utf8_unchecked(lean_obj_arg ba) {
    lean_sarray_object *o = lean_to_sarray(ba);
    lean_obj_res s = lean_mk_string_from_bytes((const char *)o->m_data, o->m_size);
    lean_dec(ba);
    return s;
}

LEAN_EXPORT lean_obj_res lean_string_to_utf8(lean_obj_arg s) {
    lean_string_object *so = lean_to_string(s);
    size_t sz = so->m_size - 1; /* without NUL */
    lean_object *ba = lean_alloc_sarray(1, sz, sz);
    memcpy(lean_sarray_cptr(ba), so->m_data, sz);
    lean_dec(s);
    return ba;
}

LEAN_EXPORT uint64_t lean_slice_hash(b_lean_obj_arg s) {
    return lean_string_hash(s); /* simplified */
}

LEAN_EXPORT uint8_t lean_slice_dec_lt(b_lean_obj_arg s1, b_lean_obj_arg s2) {
    return lean_string_lt(s1, s2);
}

/* ================================================================
 *  8. Nat / Int Big Number Stubs
 * ================================================================ */

/* Our crypto code uses only small nats. Big nat operations abort. */

static lean_object *big_nat_panic(const char *fn) {
    fprintf(stderr, "WASM: big nat operation not supported: %s\n", fn);
    abort();
    return NULL;
}

LEAN_EXPORT lean_object *lean_nat_big_succ(lean_object *a) { return big_nat_panic("lean_nat_big_succ"); (void)a; }
LEAN_EXPORT lean_object *lean_nat_big_add(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_big_add"); }
LEAN_EXPORT lean_object *lean_nat_big_sub(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_big_sub"); }
LEAN_EXPORT lean_object *lean_nat_big_mul(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_big_mul"); }
LEAN_EXPORT lean_object *lean_nat_overflow_mul(size_t a1, size_t a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_overflow_mul"); }
LEAN_EXPORT lean_object *lean_nat_big_div(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_big_div"); }
LEAN_EXPORT lean_object *lean_nat_big_div_exact(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_big_div_exact"); }
LEAN_EXPORT lean_object *lean_nat_big_mod(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_big_mod"); }
LEAN_EXPORT bool lean_nat_big_eq(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; big_nat_panic("lean_nat_big_eq"); return 0; }
LEAN_EXPORT bool lean_nat_big_le(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; big_nat_panic("lean_nat_big_le"); return 0; }
LEAN_EXPORT bool lean_nat_big_lt(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; big_nat_panic("lean_nat_big_lt"); return 0; }
LEAN_EXPORT lean_object *lean_nat_big_land(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_big_land"); }
LEAN_EXPORT lean_object *lean_nat_big_lor(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_big_lor"); }
LEAN_EXPORT lean_object *lean_nat_big_xor(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_big_xor"); }
LEAN_EXPORT lean_obj_res lean_nat_big_shiftr(b_lean_obj_arg a1, b_lean_obj_arg a2) { (void)a1; (void)a2; return big_nat_panic("lean_nat_big_shiftr"); }

LEAN_EXPORT lean_obj_res lean_big_usize_to_nat(size_t n) { (void)n; return big_nat_panic("lean_big_usize_to_nat"); }
LEAN_EXPORT lean_obj_res lean_big_uint64_to_nat(uint64_t n) { (void)n; return big_nat_panic("lean_big_uint64_to_nat"); }

LEAN_EXPORT lean_obj_res lean_nat_shiftl(b_lean_obj_arg a1, b_lean_obj_arg a2) {
    if (lean_is_scalar(a1) && lean_is_scalar(a2)) {
        size_t v = lean_unbox(a1);
        size_t s = lean_unbox(a2);
        if (s < sizeof(size_t) * 8) {
            size_t r = v << s;
            if (r <= LEAN_MAX_SMALL_NAT && (s == 0 || (r >> s) == v))
                return lean_box(r);
        }
    }
    return big_nat_panic("lean_nat_shiftl (overflow)");
}

LEAN_EXPORT lean_obj_res lean_nat_pow(b_lean_obj_arg a1, b_lean_obj_arg a2) {
    if (lean_is_scalar(a1) && lean_is_scalar(a2)) {
        size_t base = lean_unbox(a1);
        size_t exp = lean_unbox(a2);
        size_t result = 1;
        for (size_t i = 0; i < exp; i++) {
            result *= base;
            if (result > LEAN_MAX_SMALL_NAT)
                return big_nat_panic("lean_nat_pow (overflow)");
        }
        return lean_box(result);
    }
    return big_nat_panic("lean_nat_pow (big)");
}

LEAN_EXPORT lean_obj_res lean_nat_log2(b_lean_obj_arg a) {
    if (lean_is_scalar(a)) {
        size_t v = lean_unbox(a);
        if (v == 0) return lean_box(0);
        size_t r = 0;
        while (v > 1) { v >>= 1; r++; }
        return lean_box(r);
    }
    return big_nat_panic("lean_nat_log2 (big)");
}

LEAN_EXPORT lean_obj_res lean_cstr_to_nat(const char *n) {
    size_t val = 0;
    while (*n >= '0' && *n <= '9') {
        size_t d = (size_t)(*n - '0');
        val = val * 10 + d;
        n++;
    }
    return lean_box(val);
}

LEAN_EXPORT size_t lean_usize_of_big_nat(b_lean_obj_arg a) {
    (void)a;
    big_nat_panic("lean_usize_of_big_nat");
    return 0;
}

/* Int big number stubs */
LEAN_EXPORT lean_object *lean_int_big_neg(lean_object *a) { (void)a; return big_nat_panic("lean_int_big_neg"); }
LEAN_EXPORT lean_object *lean_int_big_add(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_int_big_add"); }
LEAN_EXPORT lean_object *lean_int_big_sub(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_int_big_sub"); }
LEAN_EXPORT lean_object *lean_int_big_mul(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_int_big_mul"); }
LEAN_EXPORT lean_object *lean_int_big_div(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_int_big_div"); }
LEAN_EXPORT lean_object *lean_int_big_div_exact(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_int_big_div_exact"); }
LEAN_EXPORT lean_object *lean_int_big_mod(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_int_big_mod"); }
LEAN_EXPORT lean_object *lean_int_big_ediv(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_int_big_ediv"); }
LEAN_EXPORT lean_object *lean_int_big_emod(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; return big_nat_panic("lean_int_big_emod"); }
LEAN_EXPORT bool lean_int_big_eq(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; big_nat_panic("lean_int_big_eq"); return 0; }
LEAN_EXPORT bool lean_int_big_le(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; big_nat_panic("lean_int_big_le"); return 0; }
LEAN_EXPORT bool lean_int_big_lt(lean_object *a1, lean_object *a2) { (void)a1; (void)a2; big_nat_panic("lean_int_big_lt"); return 0; }
LEAN_EXPORT bool lean_int_big_nonneg(lean_object *a) { (void)a; big_nat_panic("lean_int_big_nonneg"); return 0; }
LEAN_EXPORT lean_object *lean_big_int_to_int(int n) { (void)n; return big_nat_panic("lean_big_int_to_int"); }
LEAN_EXPORT lean_object *lean_big_size_t_to_int(size_t n) { (void)n; return big_nat_panic("lean_big_size_t_to_int"); }
LEAN_EXPORT lean_object *lean_big_int64_to_int(int64_t n) { (void)n; return big_nat_panic("lean_big_int64_to_int"); }
LEAN_EXPORT lean_obj_res lean_big_int_to_nat(lean_obj_arg a) { (void)a; return big_nat_panic("lean_big_int_to_nat"); }

LEAN_EXPORT lean_obj_res lean_nat_gcd(b_lean_obj_arg a1, b_lean_obj_arg a2) {
    if (lean_is_scalar(a1) && lean_is_scalar(a2)) {
        size_t x = lean_unbox(a1), y = lean_unbox(a2);
        while (y) { size_t t = y; y = x % y; x = t; }
        return lean_box(x);
    }
    return big_nat_panic("lean_nat_gcd (big)");
}

/* ================================================================
 *  9. ST Reference Operations
 * ================================================================ */

LEAN_EXPORT lean_obj_res lean_st_mk_ref(lean_obj_arg a) {
    lean_ref_object *r = (lean_ref_object *)lean_alloc_small_object(sizeof(lean_ref_object));
    lean_set_st_header((lean_object *)r, LeanRef, 0);
    r->m_value = a;
    return lean_io_result_mk_ok((lean_object *)r);
}

LEAN_EXPORT lean_obj_res lean_st_ref_get(b_lean_obj_arg r) {
    lean_object *v = ((lean_ref_object *)r)->m_value;
    lean_inc(v);
    return lean_io_result_mk_ok(v);
}

LEAN_EXPORT lean_obj_res lean_st_ref_set(b_lean_obj_arg r, lean_obj_arg v) {
    lean_object *old = ((lean_ref_object *)r)->m_value;
    ((lean_ref_object *)r)->m_value = v;
    lean_dec(old);
    return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res lean_st_ref_take(b_lean_obj_arg r) {
    lean_object *v = ((lean_ref_object *)r)->m_value;
    ((lean_ref_object *)r)->m_value = lean_box(0);
    return lean_io_result_mk_ok(v);
}

LEAN_EXPORT lean_obj_res lean_st_ref_swap(b_lean_obj_arg r, lean_obj_arg v) {
    lean_object *old = ((lean_ref_object *)r)->m_value;
    ((lean_ref_object *)r)->m_value = v;
    return lean_io_result_mk_ok(old);
}

LEAN_EXPORT lean_obj_res lean_st_ref_reset(b_lean_obj_arg r) {
    lean_object *old = ((lean_ref_object *)r)->m_value;
    ((lean_ref_object *)r)->m_value = lean_box(0);
    lean_dec(old);
    return lean_io_result_mk_ok(lean_box(0));
}

/* ================================================================
 *  10. IO Stubs (not available in WASM)
 * ================================================================ */

static lean_obj_res mk_io_error(const char *msg) {
    lean_object *s = lean_mk_string(msg);
    lean_object *err = lean_alloc_ctor(2, 1, 0); /* IO.Error.userError */
    lean_ctor_set(err, 0, s);
    lean_object *r = lean_alloc_ctor(1, 1, 0); /* EStateM.Result.error */
    lean_ctor_set(r, 0, err);
    return r;
}

LEAN_EXPORT lean_obj_res lean_io_as_task(lean_obj_arg closure, lean_obj_arg prio, lean_obj_arg w) {
    (void)prio; (void)w;
    /* Just execute synchronously */
    return lean_apply_1(closure, lean_box(0));
}

LEAN_EXPORT lean_obj_res lean_io_error_to_string(lean_obj_arg e, lean_obj_arg w) {
    (void)w;
    lean_dec(e);
    return lean_io_result_mk_ok(lean_mk_string("IO error (WASM)"));
}

LEAN_EXPORT lean_obj_res lean_io_get_random_bytes(lean_obj_arg n, lean_obj_arg w) {
    (void)w;
    size_t sz = lean_unbox(n);
    lean_object *ba = lean_alloc_sarray(1, sz, sz);
    memset(lean_sarray_cptr(ba), 0, sz); /* zeros — not cryptographically random */
    return lean_io_result_mk_ok(ba);
}

LEAN_EXPORT lean_obj_res lean_io_mono_ms_now(lean_obj_arg w) {
    (void)w;
    return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res lean_io_mono_nanos_now(lean_obj_arg w) {
    (void)w;
    return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res lean_io_read_dir(lean_obj_arg path, lean_obj_arg w) {
    (void)w;
    lean_dec(path);
    return mk_io_error("filesystem not available in WASM");
}

LEAN_EXPORT void lean_io_result_show_error(b_lean_obj_arg r) { (void)r; }
LEAN_EXPORT void lean_io_mark_end_initialization(void) { }
LEAN_EXPORT bool lean_io_check_canceled_core(void) { return false; }
LEAN_EXPORT void lean_io_cancel_core(b_lean_obj_arg t) { (void)t; }
LEAN_EXPORT uint8_t lean_io_get_task_state_core(b_lean_obj_arg t) { (void)t; return 2; /* finished */ }
LEAN_EXPORT b_lean_obj_res lean_io_wait_any_core(b_lean_obj_arg task_list) { (void)task_list; return lean_box(0); }

/* ================================================================
 *  11. Crypto FFI Stubs
 * ================================================================ */

/* These are the @[extern] FFI functions from LeanServer's Crypto/FFI.lean.
   The generated C code references them. In native builds they link to OpenSSL.
   In WASM we stub them — the pure Lean implementations are used instead. */

LEAN_EXPORT lean_obj_res lean_crypto_sha256(lean_obj_arg data, lean_obj_arg w) {
    (void)w; lean_dec(data);
    return mk_io_error("crypto FFI not available in WASM");
}

LEAN_EXPORT lean_obj_res lean_crypto_hmac_sha256(lean_obj_arg key, lean_obj_arg data, lean_obj_arg w) {
    (void)w; lean_dec(key); lean_dec(data);
    return mk_io_error("crypto FFI not available in WASM");
}

LEAN_EXPORT lean_obj_res lean_crypto_aes128_gcm_encrypt(lean_obj_arg key, lean_obj_arg iv,
                                                          lean_obj_arg aad, lean_obj_arg pt,
                                                          lean_obj_arg w) {
    (void)w; lean_dec(key); lean_dec(iv); lean_dec(aad); lean_dec(pt);
    return mk_io_error("crypto FFI not available in WASM");
}

LEAN_EXPORT lean_obj_res lean_crypto_aes128_gcm_decrypt(lean_obj_arg key, lean_obj_arg iv,
                                                          lean_obj_arg aad, lean_obj_arg ct,
                                                          lean_obj_arg w) {
    (void)w; lean_dec(key); lean_dec(iv); lean_dec(aad); lean_dec(ct);
    return mk_io_error("crypto FFI not available in WASM");
}

LEAN_EXPORT lean_obj_res lean_crypto_x25519_base(lean_obj_arg privkey, lean_obj_arg w) {
    (void)w; lean_dec(privkey);
    return mk_io_error("crypto FFI not available in WASM");
}

LEAN_EXPORT lean_obj_res lean_crypto_x25519(lean_obj_arg scalar, lean_obj_arg point, lean_obj_arg w) {
    (void)w; lean_dec(scalar); lean_dec(point);
    return mk_io_error("crypto FFI not available in WASM");
}

LEAN_EXPORT lean_obj_res lean_crypto_random_bytes(lean_obj_arg n, lean_obj_arg w) {
    (void)w;
    size_t sz = lean_unbox(n);
    lean_object *ba = lean_alloc_sarray(1, sz, sz);
    memset(lean_sarray_cptr(ba), 0, sz);
    return lean_io_result_mk_ok(ba);
}

/* ================================================================
 *  12. Miscellaneous
 * ================================================================ */

LEAN_EXPORT lean_external_class *lean_register_external_class(
    lean_external_finalize_proc fin, lean_external_foreach_proc fe) {
    (void)fin; (void)fe;
    /* Return a dummy pointer; external classes are rare in pure code */
    static lean_external_class dummy_class;
    return &dummy_class;
}

LEAN_EXPORT lean_obj_res lean_io_allocprof(lean_obj_arg desc, lean_obj_arg act, lean_obj_arg w) {
    (void)w;
    lean_dec(desc);
    return lean_apply_1(act, lean_box(0));
}

/* Float operations that may be needed */
LEAN_EXPORT double lean_float_of_bits(uint64_t u) {
    double d;
    memcpy(&d, &u, sizeof(d));
    return d;
}

/* Float array operations */
LEAN_EXPORT lean_obj_res lean_float_array_mk(lean_obj_arg a) { lean_dec(a); return lean_alloc_sarray(sizeof(double), 0, 0); }
LEAN_EXPORT lean_obj_res lean_float_array_data(lean_obj_arg a) { lean_dec(a); return lean_box(0); }
LEAN_EXPORT lean_obj_res lean_copy_float_array(lean_obj_arg a) { return a; }
LEAN_EXPORT lean_obj_res lean_float_array_push(lean_obj_arg a, double d) { (void)d; return a; }

/* IO promise stubs */
LEAN_EXPORT lean_obj_res lean_io_promise_new(lean_obj_arg w) {
    (void)w;
    return mk_io_error("promises not available in WASM");
}
LEAN_EXPORT lean_obj_res lean_io_promise_resolve(lean_obj_arg v, lean_obj_arg p, lean_obj_arg w) {
    (void)w; lean_dec(v); lean_dec(p);
    return lean_io_result_mk_ok(lean_box(0));
}
LEAN_EXPORT lean_obj_res lean_io_promise_result_opt(lean_obj_arg p, lean_obj_arg w) {
    (void)w; lean_dec(p);
    return lean_io_result_mk_ok(lean_box(0));
}

/* ================================================================
 *  13. Initialization
 * ================================================================ */

/* Module initialization functions referenced by generated code */
LEAN_EXPORT lean_obj_res lean_initialize_runtime_module(lean_obj_arg w) {
    (void)w;
    return lean_io_result_mk_ok(lean_box(0));
}

/* External init stubs — Init library and Std are not compiled to WASM,
   so we provide no-op stubs. The actual initialization of data is handled
   by each module's own initialize_* function. */
LEAN_EXPORT lean_obj_res initialize_Init(uint8_t builtin) {
    (void)builtin;
    return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res initialize_Init_Data_Array(uint8_t builtin) {
    (void)builtin;
    return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res initialize_Std_Tactic_BVDecide(uint8_t builtin) {
    (void)builtin;
    return lean_io_result_mk_ok(lean_box(0));
}
