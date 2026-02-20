/**
 * wasm_glue.c — Minimal C bridge between Emscripten and Lean 4 runtime.
 *
 * Provides:
 *   1. Stubs for @[extern] functions that have no WASM implementation
 *      (secureZero, epoll, sockets, etc.)
 *   2. Thin wrappers that convert between WASM linear memory (uint8_t*)
 *      and Lean's ByteArray/String objects.
 *
 * The exported WASM functions follow the naming convention:
 *   js_<operation>(ptr, len, ...) → ptr
 * where returned ptr points to a length-prefixed buffer in WASM memory.
 */

#include <lean/lean.h>
#include <emscripten/emscripten.h>
#include <string.h>
#include <stdlib.h>

/* ── Stubs for @[extern] functions not available in WASM ──────── */

/* SideChannel.lean: secureZero (opaque, IO-only) */
LEAN_EXPORT lean_obj_res lean_secure_zero(lean_obj_arg arr, lean_obj_arg w) {
    /* In WASM we can't guarantee constant-time, just zero the bytes. */
    if (lean_is_exclusive(arr)) {
        lean_sarray_object *o = lean_to_sarray(arr);
        memset(o->m_data, 0, o->m_size);
    }
    return lean_io_result_mk_ok(arr);
}

/* ── ByteArray conversion helpers ──────────────────────────────── */

/**
 * Create a Lean ByteArray from a raw pointer + length.
 */
static lean_obj_res mk_byte_array(const uint8_t *data, size_t len) {
    lean_obj_res arr = lean_alloc_sarray(1, len, len);
    if (data && len > 0) {
        memcpy(lean_sarray_cptr(arr), data, len);
    }
    return arr;
}

/**
 * Extract raw pointer and length from a Lean ByteArray.
 * The returned pointer is valid only while `arr` is alive.
 */
static const uint8_t *byte_array_data(lean_obj_arg arr, size_t *out_len) {
    *out_len = lean_sarray_size(arr);
    return lean_sarray_cptr(arr);
}

/**
 * Copy a Lean ByteArray result into a freshly malloc'd buffer,
 * prefixed with 4-byte LE length. Caller must free().
 * Note: Our WasmAPI already packs results, so this extracts and re-exports.
 */
static uint8_t *export_byte_array(lean_obj_arg arr, size_t *total_len) {
    size_t len;
    const uint8_t *data = byte_array_data(arr, &len);
    uint8_t *buf = (uint8_t *)malloc(len);
    if (buf && len > 0) {
        memcpy(buf, data, len);
    }
    *total_len = len;
    return buf;
}

/* ── Exported WASM functions (called from JavaScript) ──────────── */

/* Forward declarations of Lean @[export] functions */
extern lean_obj_res wasm_sha256(lean_obj_arg data);
extern lean_obj_res wasm_hmac_sha256(lean_obj_arg key, lean_obj_arg msg);
extern lean_obj_res wasm_hkdf_extract(lean_obj_arg salt, lean_obj_arg ikm);
extern lean_obj_res wasm_aes_gcm_encrypt(lean_obj_arg key, lean_obj_arg iv,
                                          lean_obj_arg aad, lean_obj_arg pt);
extern lean_obj_res wasm_aes_gcm_decrypt(lean_obj_arg key, lean_obj_arg iv,
                                          lean_obj_arg aad, lean_obj_arg ct);
extern lean_obj_res wasm_x25519_base(lean_obj_arg privateKey);
extern lean_obj_res wasm_x25519_scalarmult(lean_obj_arg scalar, lean_obj_arg point);
extern lean_obj_res wasm_bytes_to_hex(lean_obj_arg data);
extern lean_obj_res wasm_hex_to_bytes(lean_obj_arg hexStr);
extern lean_obj_res wasm_hpack_encode(lean_obj_arg headers);
extern lean_obj_res wasm_hpack_decode(lean_obj_arg data);
extern lean_obj_res wasm_http2_parse_frame(lean_obj_arg data);
extern lean_obj_res wasm_huffman_encode(lean_obj_arg data);
extern lean_obj_res wasm_huffman_decode(lean_obj_arg data);
extern lean_obj_res wasm_tls_derive_handshake(lean_obj_arg ss, lean_obj_arg hh);
extern lean_obj_res wasm_tls_derive_application(lean_obj_arg hs, lean_obj_arg hh);

/* ── SHA-256 ──────────────────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_sha256(const uint8_t *data, size_t len, size_t *out_len) {
    lean_obj_res arr = mk_byte_array(data, len);
    lean_obj_res result = wasm_sha256(arr);
    return export_byte_array(result, out_len);
}

/* ── HMAC-SHA-256 ─────────────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_hmac_sha256(const uint8_t *key, size_t klen,
                         const uint8_t *msg, size_t mlen,
                         size_t *out_len) {
    lean_obj_res k = mk_byte_array(key, klen);
    lean_obj_res m = mk_byte_array(msg, mlen);
    lean_obj_res result = wasm_hmac_sha256(k, m);
    return export_byte_array(result, out_len);
}

/* ── HKDF-Extract ─────────────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_hkdf_extract(const uint8_t *salt, size_t slen,
                          const uint8_t *ikm, size_t ilen,
                          size_t *out_len) {
    lean_obj_res s = mk_byte_array(salt, slen);
    lean_obj_res i = mk_byte_array(ikm, ilen);
    lean_obj_res result = wasm_hkdf_extract(s, i);
    return export_byte_array(result, out_len);
}

/* ── AES-128-GCM Encrypt ─────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_aes_gcm_encrypt(const uint8_t *key, size_t klen,
                              const uint8_t *iv, size_t ivlen,
                              const uint8_t *aad, size_t alen,
                              const uint8_t *pt, size_t ptlen,
                              size_t *out_len) {
    lean_obj_res k = mk_byte_array(key, klen);
    lean_obj_res v = mk_byte_array(iv, ivlen);
    lean_obj_res a = mk_byte_array(aad, alen);
    lean_obj_res p = mk_byte_array(pt, ptlen);
    lean_obj_res result = wasm_aes_gcm_encrypt(k, v, a, p);
    return export_byte_array(result, out_len);
}

/* ── AES-128-GCM Decrypt ─────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_aes_gcm_decrypt(const uint8_t *key, size_t klen,
                              const uint8_t *iv, size_t ivlen,
                              const uint8_t *aad, size_t alen,
                              const uint8_t *ct, size_t ctlen,
                              size_t *out_len) {
    lean_obj_res k = mk_byte_array(key, klen);
    lean_obj_res v = mk_byte_array(iv, ivlen);
    lean_obj_res a = mk_byte_array(aad, alen);
    lean_obj_res c = mk_byte_array(ct, ctlen);
    lean_obj_res result = wasm_aes_gcm_decrypt(k, v, a, c);
    return export_byte_array(result, out_len);
}

/* ── X25519 ───────────────────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_x25519_base(const uint8_t *privkey, size_t len, size_t *out_len) {
    lean_obj_res pk = mk_byte_array(privkey, len);
    lean_obj_res result = wasm_x25519_base(pk);
    return export_byte_array(result, out_len);
}

EMSCRIPTEN_KEEPALIVE
uint8_t *js_x25519_scalarmult(const uint8_t *scalar, size_t slen,
                                const uint8_t *point, size_t plen,
                                size_t *out_len) {
    lean_obj_res s = mk_byte_array(scalar, slen);
    lean_obj_res p = mk_byte_array(point, plen);
    lean_obj_res result = wasm_x25519_scalarmult(s, p);
    return export_byte_array(result, out_len);
}

/* ── Hex encoding ─────────────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_bytes_to_hex(const uint8_t *data, size_t len, size_t *out_len) {
    lean_obj_res arr = mk_byte_array(data, len);
    lean_obj_res result = wasm_bytes_to_hex(arr);
    return export_byte_array(result, out_len);
}

/* ── HPACK decode ─────────────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_hpack_decode(const uint8_t *data, size_t len, size_t *out_len) {
    lean_obj_res arr = mk_byte_array(data, len);
    lean_obj_res result = wasm_hpack_decode(arr);
    return export_byte_array(result, out_len);
}

/* ── Huffman encode/decode ────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_huffman_encode(const uint8_t *data, size_t len, size_t *out_len) {
    lean_obj_res arr = mk_byte_array(data, len);
    lean_obj_res result = wasm_huffman_encode(arr);
    return export_byte_array(result, out_len);
}

EMSCRIPTEN_KEEPALIVE
uint8_t *js_huffman_decode(const uint8_t *data, size_t len, size_t *out_len) {
    lean_obj_res arr = mk_byte_array(data, len);
    lean_obj_res result = wasm_huffman_decode(arr);
    return export_byte_array(result, out_len);
}

/* ── TLS Key Derivation ───────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_tls_derive_handshake(const uint8_t *ss, size_t sslen,
                                   const uint8_t *hh, size_t hhlen,
                                   size_t *out_len) {
    lean_obj_res s = mk_byte_array(ss, sslen);
    lean_obj_res h = mk_byte_array(hh, hhlen);
    lean_obj_res result = wasm_tls_derive_handshake(s, h);
    return export_byte_array(result, out_len);
}

EMSCRIPTEN_KEEPALIVE
uint8_t *js_tls_derive_application(const uint8_t *hs, size_t hslen,
                                     const uint8_t *hh, size_t hhlen,
                                     size_t *out_len) {
    lean_obj_res s = mk_byte_array(hs, hslen);
    lean_obj_res h = mk_byte_array(hh, hhlen);
    lean_obj_res result = wasm_tls_derive_application(s, h);
    return export_byte_array(result, out_len);
}

/* ── HTTP/2 frame parse ───────────────────────────────────────── */

EMSCRIPTEN_KEEPALIVE
uint8_t *js_http2_parse_frame(const uint8_t *data, size_t len, size_t *out_len) {
    lean_obj_res arr = mk_byte_array(data, len);
    lean_obj_res result = wasm_http2_parse_frame(arr);
    return export_byte_array(result, out_len);
}

/* ── Memory management (called from JS to free returned buffers) ── */

EMSCRIPTEN_KEEPALIVE
void js_free(void *ptr) {
    free(ptr);
}
