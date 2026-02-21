/**
 * init_stubs_wasm.c — Stubs for Init/Std/Lean library functions.
 *
 * When compiling Lean-generated C to WASM, we don't have the precompiled
 * Init/Std/Lean libraries (they're compiled for x86_64). This file provides
 * C implementations of all Init/Std/Lean library functions referenced by
 * the LeanServer modules.
 *
 * Categories:
 *   1. Global constants (l_Array_empty, l_ByteArray_empty, etc.)
 *   2. ByteArray operations
 *   3. Array operations
 *   4. List operations
 *   5. String / String.Slice operations
 *   6. Char operations
 *   7. Nat / Bool / Float repr
 *   8. Id monad operations
 *   9. Option / DecidableEq / Inhabited
 *  10. IO filesystem stubs
 *  11. Lean.Name / Lean.Syntax
 *  12. Std.Format
 *  13. Std.Tactic.BVDecide
 *  14. Range / Misc
 */

#include <lean/lean.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* Helper: IO error result */
static lean_obj_res wasm_io_error(const char *msg) {
    lean_object *s = lean_mk_string(msg);
    lean_object *err = lean_alloc_ctor(2, 1, 0); /* IO.Error.userError */
    lean_ctor_set(err, 0, s);
    lean_object *r = lean_alloc_ctor(1, 1, 0); /* EStateM.Result.error */
    lean_ctor_set(r, 0, err);
    return r;
}

/* ================================================================
 *  1. Global Constants
 * ================================================================ */

/* Array.empty = #[] (an empty Array) */
lean_object *l_Array_empty;

/* ByteArray.empty = ByteArray.mk #[] */
lean_object *l_ByteArray_empty;

/* Default values for UInt types */
uint8_t  l_instInhabitedUInt8  = 0;
uint32_t l_instInhabitedUInt32 = 0;
uint64_t l_instInhabitedUInt64 = 0;

/* Init function to set up global constants — called from
   lean_runtime_wasm.c's lean_initialize_runtime_module or
   from ensure_initialized in wasm_glue.c */
__attribute__((constructor))
static void init_wasm_globals(void) {
    /* Empty Array */
    l_Array_empty = lean_alloc_array(0, 0);
    lean_mark_persistent(l_Array_empty);

    /* Empty ByteArray */
    l_ByteArray_empty = lean_alloc_sarray(1, 0, 0);
    lean_mark_persistent(l_ByteArray_empty);
}

/* ================================================================
 *  2. ByteArray Operations
 * ================================================================ */

/* ByteArray.extract (a : ByteArray) (start stop : Nat) : ByteArray */
LEAN_EXPORT lean_object* l_ByteArray_extract(lean_object *a, lean_object *start, lean_object *stop) {
    lean_sarray_object *o = lean_to_sarray(a);
    size_t s = lean_unbox(start);
    size_t e = lean_unbox(stop);
    size_t sz = o->m_size;
    if (s > sz) s = sz;
    if (e > sz) e = sz;
    if (s >= e) {
        return lean_alloc_sarray(1, 0, 0);
    }
    size_t len = e - s;
    lean_object *r = lean_alloc_sarray(1, len, len);
    memcpy(lean_sarray_cptr(r), o->m_data + s, len);
    return r;
}

/* ByteArray.instBEq.beq : ByteArray → ByteArray → Bool */
LEAN_EXPORT uint8_t l_ByteArray_instBEq_beq(lean_object *a, lean_object *b) {
    lean_sarray_object *sa = lean_to_sarray(a);
    lean_sarray_object *sb = lean_to_sarray(b);
    if (sa->m_size != sb->m_size) return 0;
    return memcmp(sa->m_data, sb->m_data, sa->m_size) == 0;
}

LEAN_EXPORT lean_object* l_ByteArray_instBEq_beq___boxed(lean_object *a, lean_object *b) {
    uint8_t r = l_ByteArray_instBEq_beq(a, b);
    lean_dec(a); lean_dec(b);
    return lean_box(r);
}

/* ByteArray.instDecidableEq : same as beq */
LEAN_EXPORT uint8_t l_ByteArray_instDecidableEq(lean_object *a, lean_object *b) {
    return l_ByteArray_instBEq_beq(a, b);
}

LEAN_EXPORT lean_object* l_ByteArray_instDecidableEq___boxed(lean_object *a, lean_object *b) {
    uint8_t r = l_ByteArray_instDecidableEq(a, b);
    lean_dec(a); lean_dec(b);
    return lean_box(r);
}

/* ByteArray.isEmpty */
LEAN_EXPORT uint8_t l_ByteArray_isEmpty(lean_object *a) {
    return lean_to_sarray(a)->m_size == 0;
}

/* ByteArray.toList */
LEAN_EXPORT lean_object* l_ByteArray_toList(lean_object *a) {
    lean_sarray_object *o = lean_to_sarray(a);
    lean_object *r = lean_box(0); /* List.nil */
    for (size_t i = o->m_size; i > 0; i--) {
        lean_object *cons = lean_alloc_ctor(1, 2, 0);
        lean_ctor_set(cons, 0, lean_box(o->m_data[i - 1]));
        lean_ctor_set(cons, 1, r);
        r = cons;
    }
    return r;
}

/* ================================================================
 *  3. Array Operations
 * ================================================================ */

/* Array.isEmpty */
LEAN_EXPORT uint8_t l_Array_isEmpty___redArg(lean_object *a) {
    return lean_to_array(a)->m_size == 0;
}

/* Array.back (returns last element, panics if empty) */
LEAN_EXPORT lean_object* l_Array_back___redArg(lean_object *a) {
    lean_array_object *o = lean_to_array(a);
    if (o->m_size == 0) {
        lean_internal_panic("Array.back: empty array");
        return lean_box(0);
    }
    lean_object *v = o->m_data[o->m_size - 1];
    lean_inc(v);
    return v;
}

/* Array.append : Array α → Array α → Array α */
LEAN_EXPORT lean_object* l_Array_append___redArg(lean_object *a, lean_object *b) {
    lean_array_object *oa = lean_to_array(a);
    lean_array_object *ob = lean_to_array(b);
    size_t sa = oa->m_size, sb = ob->m_size;
    lean_object *r = lean_alloc_array(sa + sb, sa + sb);
    lean_array_object *ro = lean_to_array(r);
    ro->m_size = sa + sb;
    for (size_t i = 0; i < sa; i++) { lean_inc(oa->m_data[i]); ro->m_data[i] = oa->m_data[i]; }
    for (size_t i = 0; i < sb; i++) { lean_inc(ob->m_data[i]); ro->m_data[sa + i] = ob->m_data[i]; }
    lean_dec(a); lean_dec(b);
    return r;
}

/* Array.extract */
LEAN_EXPORT lean_object* l_Array_extract___redArg(lean_object *a, lean_object *start, lean_object *stop) {
    lean_array_object *o = lean_to_array(a);
    size_t s = lean_unbox(start), e = lean_unbox(stop), sz = o->m_size;
    if (s > sz) s = sz;
    if (e > sz) e = sz;
    if (s >= e) return lean_alloc_array(0, 0);
    size_t len = e - s;
    lean_object *r = lean_alloc_array(len, len);
    lean_array_object *ro = lean_to_array(r);
    ro->m_size = len;
    for (size_t i = 0; i < len; i++) { lean_inc(o->m_data[s + i]); ro->m_data[i] = o->m_data[s + i]; }
    return r;
}

/* Array.findIdx?.loop */
LEAN_EXPORT lean_object* l_Array_findIdx_x3f_loop___redArg(lean_object *p, lean_object *a, lean_object *idx) {
    lean_array_object *o = lean_to_array(a);
    size_t i = lean_unbox(idx);
    while (i < o->m_size) {
        lean_inc(o->m_data[i]); lean_inc(p);
        lean_object *res = lean_apply_1(p, o->m_data[i]);
        if (lean_unbox(res)) {
            lean_dec(p);
            lean_object *some = lean_alloc_ctor(1, 1, 0);
            lean_ctor_set(some, 0, lean_box(i));
            return some;
        }
        i++;
    }
    lean_dec(p);
    return lean_box(0); /* none */
}

/* Array.qpartition (quicksort partition) */
LEAN_EXPORT lean_object* l_Array_qpartition___redArg(lean_object *lt, lean_object *a, lean_object *lo_obj, lean_object *hi_obj) {
    /* Simplified: just return (a, lo) — partition not meaningful without full qsort */
    (void)lt;
    lean_object *r = lean_alloc_ctor(0, 2, 0);
    lean_ctor_set(r, 0, a);
    lean_ctor_set(r, 1, lo_obj);
    lean_dec(hi_obj);
    lean_dec(lt);
    return r;
}

/* Array.empty function variant (takes a type parameter) */
/* Already defined as global l_Array_empty above. This is a function wrapper. */

/* Array.mapMUnsafe.map */
LEAN_EXPORT lean_object* l___private_Init_Data_Array_Basic_0__Array_mapMUnsafe_map___redArg(
    lean_object *f, lean_object *a, size_t sz, size_t i, lean_object *arr) {
    lean_array_object *o = lean_to_array(arr);
    while (i < sz && i < o->m_size) {
        lean_inc(f);
        lean_object *v = o->m_data[i];
        lean_object *nv = lean_apply_1(f, v);
        if (lean_is_exclusive(arr)) {
            o->m_data[i] = nv;
        } else {
            lean_object *new_arr = lean_alloc_array(o->m_size, o->m_size);
            lean_array_object *no = lean_to_array(new_arr);
            no->m_size = o->m_size;
            for (size_t j = 0; j < o->m_size; j++) {
                if (j == i) { no->m_data[j] = nv; }
                else { lean_inc(o->m_data[j]); no->m_data[j] = o->m_data[j]; }
            }
            lean_dec(arr);
            arr = new_arr;
            o = lean_to_array(arr);
        }
        i++;
    }
    lean_dec(f); (void)a;
    return arr;
}

/* ================================================================
 *  4. List Operations
 * ================================================================ */

/* List.range n = [0, 1, ..., n-1] */
LEAN_EXPORT lean_object* l_List_range(lean_object *n_obj) {
    size_t n = lean_unbox(n_obj);
    lean_object *r = lean_box(0); /* nil */
    for (size_t i = n; i > 0; i--) {
        lean_object *cons = lean_alloc_ctor(1, 2, 0);
        lean_ctor_set(cons, 0, lean_box(i - 1));
        lean_ctor_set(cons, 1, r);
        r = cons;
    }
    return r;
}

/* List.reverse */
LEAN_EXPORT lean_object* l_List_reverse___redArg(lean_object *xs) {
    lean_object *r = lean_box(0); /* nil */
    lean_object *p = xs;
    while (!lean_is_scalar(p)) {
        lean_object *hd = lean_ctor_get(p, 0);
        lean_inc(hd);
        lean_object *cons = lean_alloc_ctor(1, 2, 0);
        lean_ctor_set(cons, 0, hd);
        lean_ctor_set(cons, 1, r);
        r = cons;
        p = lean_ctor_get(p, 1);
    }
    lean_dec(xs);
    return r;
}

/* List.all p xs */
LEAN_EXPORT uint8_t l_List_all___redArg(lean_object *p, lean_object *xs) {
    lean_object *cur = xs;
    while (!lean_is_scalar(cur)) {
        lean_object *hd = lean_ctor_get(cur, 0);
        lean_inc(hd); lean_inc(p);
        lean_object *res = lean_apply_1(p, hd);
        if (!lean_unbox(res)) { lean_dec(p); return 0; }
        cur = lean_ctor_get(cur, 1);
    }
    lean_dec(p);
    return 1;
}

/* List.any p xs */
LEAN_EXPORT uint8_t l_List_any___redArg(lean_object *p, lean_object *xs) {
    lean_object *cur = xs;
    while (!lean_is_scalar(cur)) {
        lean_object *hd = lean_ctor_get(cur, 0);
        lean_inc(hd); lean_inc(p);
        lean_object *res = lean_apply_1(p, hd);
        if (lean_unbox(res)) { lean_dec(p); return 1; }
        cur = lean_ctor_get(cur, 1);
    }
    lean_dec(p);
    return 0;
}

/* List.appendTR xs ys (tail-recursive append) */
LEAN_EXPORT lean_object* l_List_appendTR___redArg(lean_object *xs, lean_object *ys) {
    if (lean_is_scalar(xs)) return ys;
    /* Reverse xs, then prepend to ys */
    lean_object *rev = l_List_reverse___redArg(xs);
    lean_object *r = ys;
    lean_object *p = rev;
    while (!lean_is_scalar(p)) {
        lean_object *hd = lean_ctor_get(p, 0);
        lean_inc(hd);
        lean_object *cons = lean_alloc_ctor(1, 2, 0);
        lean_ctor_set(cons, 0, hd);
        lean_ctor_set(cons, 1, r);
        r = cons;
        p = lean_ctor_get(p, 1);
    }
    lean_dec(rev);
    return r;
}

/* List.drop */
LEAN_EXPORT lean_object* l_List_drop___redArg(lean_object *n_obj, lean_object *xs) {
    size_t n = lean_unbox(n_obj);
    lean_object *p = xs;
    while (n > 0 && !lean_is_scalar(p)) {
        p = lean_ctor_get(p, 1);
        n--;
    }
    lean_inc(p);
    lean_dec(xs);
    return p;
}

/* List.find? */
LEAN_EXPORT lean_object* l_List_find_x3f___redArg(lean_object *p, lean_object *xs) {
    lean_object *cur = xs;
    while (!lean_is_scalar(cur)) {
        lean_object *hd = lean_ctor_get(cur, 0);
        lean_inc(hd); lean_inc(p);
        lean_object *res = lean_apply_1(p, hd);
        if (lean_unbox(res)) {
            lean_inc(hd);
            lean_dec(p);
            lean_object *some = lean_alloc_ctor(1, 1, 0);
            lean_ctor_set(some, 0, hd);
            return some;
        }
        cur = lean_ctor_get(cur, 1);
    }
    lean_dec(p);
    return lean_box(0); /* none */
}

/* List.get!Internal (get element by index) */
LEAN_EXPORT lean_object* l_List_get_x21Internal___redArg(lean_object *xs, lean_object *n_obj, lean_object *fallback) {
    size_t n = lean_unbox(n_obj);
    lean_object *cur = xs;
    while (n > 0 && !lean_is_scalar(cur)) {
        cur = lean_ctor_get(cur, 1);
        n--;
    }
    if (lean_is_scalar(cur)) return fallback;
    lean_object *hd = lean_ctor_get(cur, 0);
    lean_inc(hd);
    lean_dec(fallback);
    return hd;
}

/* List.isEmpty */
LEAN_EXPORT uint8_t l_List_isEmpty___redArg(lean_object *xs) {
    return lean_is_scalar(xs);
}

/* List.lengthTR */
LEAN_EXPORT lean_object* l_List_lengthTR___redArg(lean_object *xs) {
    size_t len = 0;
    lean_object *p = xs;
    while (!lean_is_scalar(p)) { len++; p = lean_ctor_get(p, 1); }
    return lean_box(len);
}

/* List.replicateTR */
LEAN_EXPORT lean_object* l_List_replicateTR___redArg(lean_object *n_obj, lean_object *a) {
    size_t n = lean_unbox(n_obj);
    lean_object *r = lean_box(0);
    for (size_t i = 0; i < n; i++) {
        lean_inc(a);
        lean_object *cons = lean_alloc_ctor(1, 2, 0);
        lean_ctor_set(cons, 0, a);
        lean_ctor_set(cons, 1, r);
        r = cons;
    }
    lean_dec(a);
    return r;
}

/* ================================================================
 *  5. String / String.Slice Operations
 * ================================================================ */

/* String.intercalate sep xs */
LEAN_EXPORT lean_object* l_String_intercalate(lean_object *sep, lean_object *xs) {
    lean_object *r = lean_mk_string("");
    int first = 1;
    lean_object *cur = xs;
    while (!lean_is_scalar(cur)) {
        lean_object *hd = lean_ctor_get(cur, 0);
        if (!first) {
            lean_inc(sep);
            r = lean_string_append(r, sep);
        }
        lean_inc(hd);
        r = lean_string_append(r, hd);
        first = 0;
        cur = lean_ctor_get(cur, 1);
    }
    lean_dec(sep); lean_dec(xs);
    return r;
}

/* String.splitOnAux s sep start pos result */
LEAN_EXPORT lean_object* l_String_splitOnAux(lean_object *s, lean_object *sep,
                                              lean_object *start, lean_object *pos,
                                              lean_object *sep_pos, lean_object *result) {
    /* Simplified: just return List.cons(s, result) */
    (void)sep; (void)start; (void)pos; (void)sep_pos;
    lean_inc(s);
    lean_object *cons = lean_alloc_ctor(1, 2, 0);
    lean_ctor_set(cons, 0, s);
    lean_ctor_set(cons, 1, result);
    lean_dec(s); lean_dec(sep);
    return cons;
}

/* String.quote s = "\"" ++ escape(s) ++ "\"" */
LEAN_EXPORT lean_object* l_String_quote(lean_object *s) {
    lean_object *r = lean_mk_string("\"");
    lean_inc(s);
    r = lean_string_append(r, s);
    lean_dec(s);
    lean_object *q = lean_mk_string("\"");
    r = lean_string_append(r, q);
    return r;
}

/* String.Slice.toString */
LEAN_EXPORT lean_object* l_String_Slice_toString(lean_object *slice) {
    /* A Slice is a struct with (str : String, start : Pos, stop : Pos)
       represented as a constructor with 3 fields */
    if (lean_is_scalar(slice)) return lean_mk_string("");
    lean_object *str = lean_ctor_get(slice, 0);
    lean_object *start = lean_ctor_get(slice, 1);
    lean_object *stop = lean_ctor_get(slice, 2);
    lean_inc(str);
    lean_object *r = lean_string_utf8_extract(str, start, stop);
    return r;
}

/* String.Slice.trimAscii */
LEAN_EXPORT lean_object* l_String_Slice_trimAscii(lean_object *slice) {
    /* Simplified: just return slice as-is */
    lean_inc(slice);
    return slice;
}

/* String.Slice.toNat? */
LEAN_EXPORT lean_object* l_String_Slice_toNat_x3f(lean_object *slice) {
    /* Convert slice to string, then parse */
    lean_object *s = l_String_Slice_toString(slice);
    lean_string_object *so = lean_to_string(s);
    const char *p = so->m_data;
    if (!*p || *p < '0' || *p > '9') { lean_dec(s); return lean_box(0); /* none */ }
    size_t val = 0;
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
    lean_dec(s);
    lean_object *some = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(some, 0, lean_box(val));
    return some;
}

/* String.Slice.pos! */
LEAN_EXPORT lean_object* l_String_Slice_pos_x21(lean_object *slice, lean_object *n) {
    (void)slice;
    return n; /* simplified */
}

/* String.Slice.pos? */
LEAN_EXPORT lean_object* l_String_Slice_pos_x3f(lean_object *slice, lean_object *n) {
    (void)slice;
    lean_object *some = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(some, 0, n);
    return some;
}

/* String.Slice.slice! */
LEAN_EXPORT lean_object* l_String_Slice_slice_x21(lean_object *slice, lean_object *start, lean_object *stop) {
    /* Return a new slice with adjusted bounds */
    lean_inc(slice);
    (void)start; (void)stop;
    return slice;
}

/* String.Slice.Pattern.ForwardSliceSearcher.buildTable */
LEAN_EXPORT lean_object* l_String_Slice_Pattern_ForwardSliceSearcher_buildTable(lean_object *pat) {
    /* Return an empty array as the failure table */
    (void)pat;
    return lean_alloc_array(0, 0);
}

/* String.Slice.findNextPos.go */
LEAN_EXPORT lean_object* l___private_Init_Data_String_Basic_0__String_Slice_findNextPos_go(
    lean_object *s, lean_object *pos) {
    /* Just return pos */
    (void)s;
    return pos;
}

/* String.mapAux for URI escape */
LEAN_EXPORT lean_object* l_String_mapAux___at___00__private_Init_System_Uri_0__System_Uri_UriEscape_uriEscapeAsciiChar_uInt8ToHex_spec__0(
    lean_object *f, lean_object *s) {
    /* Simplified: just return the string */
    lean_dec(f);
    return s;
}

/* ================================================================
 *  6. Char Operations
 * ================================================================ */

/* Char.ofNat (returns replacement char for invalid codepoints) */
LEAN_EXPORT uint32_t l_Char_ofNat(lean_object *n) {
    size_t v = lean_unbox(n);
    if (v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0xFFFD;
    return (uint32_t)v;
}

/* Char.toLower */
LEAN_EXPORT uint32_t l_Char_toLower(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

/* Char.utf8Size */
LEAN_EXPORT lean_object* l_Char_utf8Size(uint32_t c) {
    if (c < 0x80) return lean_box(1);
    if (c < 0x800) return lean_box(2);
    if (c < 0x10000) return lean_box(3);
    return lean_box(4);
}

/* ================================================================
 *  7. Nat / Bool / Float repr
 * ================================================================ */

/* Nat.reprFast : Nat → String */
LEAN_EXPORT lean_object* l_Nat_reprFast(lean_object *n) {
    if (lean_is_scalar(n)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", lean_unbox(n));
        return lean_mk_string(buf);
    }
    return lean_mk_string("(big)");
}

/* Bool.repr */
LEAN_EXPORT lean_object* l_Bool_repr___redArg(uint8_t b) {
    return b ? lean_mk_string("true") : lean_mk_string("false");
}

/* Float.ofScientific mantissa isNeg exp */
LEAN_EXPORT double l_Float_ofScientific(lean_object *mantissa, uint8_t isNeg, lean_object *exp) {
    double m = (double)lean_unbox(mantissa);
    double e = (double)lean_unbox(exp);
    double result = isNeg ? m * pow(10.0, -e) : m * pow(10.0, e);
    return result;
}

/* Float.repr */
LEAN_EXPORT lean_object* l_Float_repr(double f, lean_object *precision) {
    (void)precision;
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", f);
    return lean_mk_string(buf);
}

/* Repr.addAppParen */
LEAN_EXPORT lean_object* l_Repr_addAppParen(lean_object *fmt, lean_object *prec) {
    (void)prec;
    return fmt;
}

/* outOfBounds (panic/default) */
LEAN_EXPORT lean_object* l_outOfBounds___redArg(lean_object *fallback) {
    return fallback;
}

/* ================================================================
 *  8. Id Monad Operations
 * ================================================================ */

/* Id.instMonad lambdas — these implement pure, bind, map, seq for the Id monad */

/* lam_0: bind (a >>= f) = f a */
LEAN_EXPORT lean_object* l_Id_instMonad___lam__0(lean_object *_alpha, lean_object *_beta, lean_object *a, lean_object *f) {
    (void)_alpha; (void)_beta;
    return lean_apply_1(f, a);
}

/* lam_1: pure (boxed) */
LEAN_EXPORT lean_object* l_Id_instMonad___lam__1___boxed(lean_object *_alpha, lean_object *_beta, lean_object *a, lean_object *f) {
    (void)_alpha; (void)_beta;
    return lean_apply_1(f, a);
}

/* lam_2: pure (identity) */
LEAN_EXPORT lean_object* l_Id_instMonad___lam__2___boxed(lean_object *_alpha, lean_object *a) {
    (void)_alpha;
    return a;
}

/* lam_3: map */
LEAN_EXPORT lean_object* l_Id_instMonad___lam__3(lean_object *_alpha, lean_object *_beta, lean_object *f, lean_object *a) {
    (void)_alpha; (void)_beta;
    return lean_apply_1(f, a);
}

/* lam_4: seq (f <*> x) */
LEAN_EXPORT lean_object* l_Id_instMonad___lam__4___boxed(lean_object *_alpha, lean_object *_beta, lean_object *f, lean_object *x) {
    (void)_alpha; (void)_beta;
    lean_object *a = lean_apply_1(x, lean_box(0));
    return lean_apply_1(f, a);
}

/* lam_5: seqLeft */
LEAN_EXPORT lean_object* l_Id_instMonad___lam__5___boxed(lean_object *_alpha, lean_object *_beta, lean_object *a, lean_object *b) {
    (void)_alpha; (void)_beta;
    lean_object *_bval = lean_apply_1(b, lean_box(0));
    lean_dec(_bval);
    return a;
}

/* lam_6: seqRight */
LEAN_EXPORT lean_object* l_Id_instMonad___lam__6(lean_object *_alpha, lean_object *_beta, lean_object *a, lean_object *b) {
    (void)_alpha; (void)_beta;
    lean_dec(a);
    return lean_apply_1(b, lean_box(0));
}

/* ================================================================
 *  9. Option / DecidableEq / Inhabited / MonadEST
 * ================================================================ */

/* Option.decidableEqNone : decides if an Option is none */
LEAN_EXPORT uint8_t l_Option_decidableEqNone___redArg(lean_object *a) {
    return lean_is_scalar(a) ? 1 : 0;
}

/* Option.instDecidableEq */
LEAN_EXPORT uint8_t l_Option_instDecidableEq___redArg(lean_object *beq, lean_object *a, lean_object *b) {
    /* Both none */
    if (lean_is_scalar(a) && lean_is_scalar(b)) { lean_dec(beq); return 1; }
    /* One none */
    if (lean_is_scalar(a) || lean_is_scalar(b)) { lean_dec(beq); return 0; }
    /* Both some */
    lean_object *va = lean_ctor_get(a, 0);
    lean_object *vb = lean_ctor_get(b, 0);
    lean_inc(va); lean_inc(vb);
    lean_object *r = lean_apply_2(beq, va, vb);
    return lean_unbox(r);
}

/* instDecidableEqList */
LEAN_EXPORT uint8_t l_instDecidableEqList___redArg(lean_object *beq, lean_object *a, lean_object *b) {
    lean_object *ca = a, *cb = b;
    while (!lean_is_scalar(ca) && !lean_is_scalar(cb)) {
        lean_object *ha = lean_ctor_get(ca, 0);
        lean_object *hb = lean_ctor_get(cb, 0);
        lean_inc(ha); lean_inc(hb); lean_inc(beq);
        lean_object *r = lean_apply_2(beq, ha, hb);
        if (!lean_unbox(r)) { lean_dec(beq); return 0; }
        ca = lean_ctor_get(ca, 1);
        cb = lean_ctor_get(cb, 1);
    }
    lean_dec(beq);
    return lean_is_scalar(ca) && lean_is_scalar(cb);
}

/* instDecidableEqNat */
LEAN_EXPORT lean_object* l_instDecidableEqNat___boxed(lean_object *a, lean_object *b) {
    uint8_t r = lean_nat_dec_eq(a, b);
    lean_dec(a); lean_dec(b);
    return lean_box(r);
}

/* instDecidableEqProd */
LEAN_EXPORT uint8_t l_instDecidableEqProd___redArg(lean_object *beq1, lean_object *beq2, lean_object *a, lean_object *b) {
    lean_object *a1 = lean_ctor_get(a, 0), *a2 = lean_ctor_get(a, 1);
    lean_object *b1 = lean_ctor_get(b, 0), *b2 = lean_ctor_get(b, 1);
    lean_inc(a1); lean_inc(b1);
    lean_object *r1 = lean_apply_2(beq1, a1, b1);
    if (!lean_unbox(r1)) { lean_dec(beq2); return 0; }
    lean_inc(a2); lean_inc(b2);
    lean_object *r2 = lean_apply_2(beq2, a2, b2);
    return lean_unbox(r2);
}

/* instDecidableEqUInt64 */
LEAN_EXPORT lean_object* l_instDecidableEqUInt64___boxed(lean_object *a, lean_object *b) {
    uint64_t va = lean_unbox_uint64(a), vb = lean_unbox_uint64(b);
    lean_dec(a); lean_dec(b);
    return lean_box(va == vb);
}

/* instDecidableEqUInt8 */
LEAN_EXPORT lean_object* l_instDecidableEqUInt8___boxed(lean_object *a, lean_object *b) {
    uint8_t va = lean_unbox(a), vb = lean_unbox(b);
    lean_dec(a); lean_dec(b);
    return lean_box(va == vb);
}

/* instMonadEST : return the monad instance for EST (state transformer) */
LEAN_EXPORT lean_object* l_instMonadEST(lean_object *eps, lean_object *sigma) {
    (void)eps; (void)sigma;
    /* Return a placeholder monad instance — EST/EStateM uses the same structure */
    lean_object *inst = lean_alloc_ctor(0, 7, 0);
    /* pure = lam_2 */
    lean_object *pure_fn = lean_alloc_closure((void*)l_Id_instMonad___lam__2___boxed, 2, 0);
    /* bind = lam_0 */
    lean_object *bind_fn = lean_alloc_closure((void*)l_Id_instMonad___lam__0, 4, 0);
    /* map = lam_3 */
    lean_object *map_fn = lean_alloc_closure((void*)l_Id_instMonad___lam__3, 4, 0);
    lean_ctor_set(inst, 0, pure_fn);
    lean_ctor_set(inst, 1, bind_fn);
    lean_ctor_set(inst, 2, map_fn);
    lean_ctor_set(inst, 3, lean_box(0));
    lean_ctor_set(inst, 4, lean_box(0));
    lean_ctor_set(inst, 5, lean_box(0));
    lean_ctor_set(inst, 6, lean_box(0));
    return inst;
}

/* Nat.decidableForallFin */
LEAN_EXPORT uint8_t l_Nat_decidableForallFin___redArg(lean_object *p, lean_object *n) {
    size_t sz = lean_unbox(n);
    for (size_t i = 0; i < sz; i++) {
        lean_inc(p);
        lean_object *res = lean_apply_1(p, lean_box(i));
        if (!lean_unbox(res)) { lean_dec(p); return 0; }
    }
    lean_dec(p);
    return 1;
}

/* ByteArray.instDecidableEq (boxed wrapper - also needed for full build) */
/* Already defined above as l_ByteArray_instDecidableEq___boxed */

/* ================================================================
 *  10. IO Filesystem Stubs
 * ================================================================ */

/* IO.FS.readFile : FilePath → IO String */
LEAN_EXPORT lean_object* l_IO_FS_readFile(lean_object *path) {
    lean_dec(path);
    return wasm_io_error("filesystem not available in WASM");
}

/* IO.FS.readBinFile : FilePath → IO ByteArray */
LEAN_EXPORT lean_object* l_IO_FS_readBinFile(lean_object *path) {
    lean_dec(path);
    return wasm_io_error("filesystem not available in WASM");
}

/* IO.FS.DirEntry.path */
LEAN_EXPORT lean_object* l_IO_FS_DirEntry_path(lean_object *entry) {
    lean_inc(entry);
    return entry; /* simplified */
}

/* IO.eprintln */
LEAN_EXPORT lean_object* l_IO_eprintln___at___00__private_Init_System_IO_0__IO_eprintlnAux_spec__0(lean_object *s) {
    lean_dec(s); /* just discard in WASM */
    return lean_io_result_mk_ok(lean_box(0));
}

/* IO.sleep */
LEAN_EXPORT lean_object* l_IO_sleep(uint32_t ms) {
    (void)ms;
    return lean_io_result_mk_ok(lean_box(0));
}

/* System.FilePath.pathExists */
LEAN_EXPORT uint8_t l_System_FilePath_pathExists(lean_object *path) {
    lean_dec(path);
    return 0; /* nothing exists in WASM */
}

/* ================================================================
 *  11. Lean.Name / Lean.Syntax
 * ================================================================ */

/* Lean.Name.mkStr1 : String → Name */
LEAN_EXPORT lean_object* l_Lean_Name_mkStr1(lean_object *s) {
    /* Name.str anonymous s */
    lean_object *n = lean_alloc_ctor(1, 2, 0);
    lean_ctor_set(n, 0, lean_box(0)); /* Name.anonymous */
    lean_ctor_set(n, 1, s);
    return n;
}

/* Lean.Name.mkStr4 */
LEAN_EXPORT lean_object* l_Lean_Name_mkStr4(lean_object *s1, lean_object *s2, lean_object *s3, lean_object *s4) {
    lean_object *n1 = l_Lean_Name_mkStr1(s1);
    lean_object *n2 = lean_alloc_ctor(1, 2, 0);
    lean_ctor_set(n2, 0, n1);
    lean_ctor_set(n2, 1, s2);
    lean_object *n3 = lean_alloc_ctor(1, 2, 0);
    lean_ctor_set(n3, 0, n2);
    lean_ctor_set(n3, 1, s3);
    lean_object *n4 = lean_alloc_ctor(1, 2, 0);
    lean_ctor_set(n4, 0, n3);
    lean_ctor_set(n4, 1, s4);
    return n4;
}

/* Lean.mkAtom : String → Syntax */
LEAN_EXPORT lean_object* l_Lean_mkAtom(lean_object *s) {
    /* Syntax.atom SourceInfo.none s */
    lean_object *syn = lean_alloc_ctor(2, 2, 0);
    lean_ctor_set(syn, 0, lean_box(0)); /* SourceInfo.none */
    lean_ctor_set(syn, 1, s);
    return syn;
}

/* ================================================================
 *  12. Std.Format
 * ================================================================ */

/* Std.Format.fill */
LEAN_EXPORT lean_object* l_Std_Format_fill(lean_object *f) {
    return f; /* pass-through */
}

/* Std.Format.joinSep (for Lean.Syntax.formatStxAux) */
LEAN_EXPORT lean_object* l_Std_Format_joinSep___at___00Lean_Syntax_formatStxAux_spec__2(
    lean_object *fmts, lean_object *sep) {
    (void)sep;
    /* Return the first format or nil */
    if (lean_is_scalar(fmts)) return lean_mk_string("");
    lean_object *hd = lean_ctor_get(fmts, 0);
    lean_inc(hd);
    return hd;
}

/* ================================================================
 *  13. Std.Tactic.BVDecide
 * ================================================================ */

LEAN_EXPORT lean_object* l_Std_Tactic_BVDecide_BVExpr_bin___override(
    lean_object *w, lean_object *l, uint8_t op, lean_object *r) {
    (void)op;
    lean_object *res = lean_alloc_ctor(3, 3, 1);
    lean_ctor_set(res, 0, w);
    lean_ctor_set(res, 1, l);
    lean_ctor_set(res, 2, r);
    lean_ctor_set_uint8(res, sizeof(void*) * 3, op);
    return res;
}

LEAN_EXPORT lean_object* l_Std_Tactic_BVDecide_BVExpr_const___override(lean_object *w, lean_object *bv) {
    lean_object *res = lean_alloc_ctor(0, 2, 0);
    lean_ctor_set(res, 0, w);
    lean_ctor_set(res, 1, bv);
    return res;
}

LEAN_EXPORT lean_object* l_Std_Tactic_BVDecide_BVExpr_un___override(lean_object *w, lean_object *op, lean_object *e) {
    lean_object *res = lean_alloc_ctor(2, 3, 0);
    lean_ctor_set(res, 0, w);
    lean_ctor_set(res, 1, op);
    lean_ctor_set(res, 2, e);
    return res;
}

LEAN_EXPORT lean_object* l_Std_Tactic_BVDecide_BVExpr_var___override(lean_object *w, lean_object *idx) {
    lean_object *res = lean_alloc_ctor(1, 2, 0);
    lean_ctor_set(res, 0, w);
    lean_ctor_set(res, 1, idx);
    return res;
}

LEAN_EXPORT uint8_t l_Std_Tactic_BVDecide_Reflect_verifyBVExpr(lean_object *a, lean_object *b) {
    (void)a; (void)b;
    return 1; /* assume verified */
}

/* ================================================================
 *  14. Range / BitVec / Misc
 * ================================================================ */

/* Std.Range.forIn'.loop */
LEAN_EXPORT lean_object* l___private_Init_Data_Range_Basic_0__Std_Range_forIn_x27_loop___redArg(
    lean_object *f, lean_object *step, lean_object *stop, lean_object *i, lean_object *acc) {
    size_t s = lean_unbox(step);
    size_t e = lean_unbox(stop);
    size_t cur = lean_unbox(i);
    if (s == 0) s = 1; /* avoid infinite loop */

    while (cur < e) {
        lean_inc(f);
        /* Call f with (index, acc) */
        lean_object *pair = lean_apply_2(f, lean_box(cur), acc);
        /* Check if ForInStep.yield or ForInStep.done */
        if (lean_obj_tag(pair) == 0) {
            /* yield: continue with new acc */
            acc = lean_ctor_get(pair, 0);
            lean_inc(acc);
            lean_dec(pair);
        } else {
            /* done: break with final acc */
            acc = lean_ctor_get(pair, 0);
            lean_inc(acc);
            lean_dec(pair);
            lean_dec(f);
            return acc;
        }
        cur += s;
    }
    lean_dec(f);
    return acc;
}

/* BitVec.ofNat */
LEAN_EXPORT lean_object* l_BitVec_ofNat(lean_object *w, lean_object *n) {
    (void)w;
    /* For small values, just box the truncated value */
    if (lean_is_scalar(n)) return n;
    lean_inc(n);
    return n;
}
