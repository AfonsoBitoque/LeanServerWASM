/**
 * LeanServer WASM — JavaScript API
 *
 * Verified cryptography compiled from Lean 4 to WebAssembly.
 * 914 machine-checked theorems. Zero sorry. Zero axioms.
 *
 * @example
 *   import { LeanServerCrypto } from './lean_server_wasm.js';
 *
 *   const crypto = await LeanServerCrypto.init();
 *   const hash = crypto.sha256(new TextEncoder().encode('hello'));
 *   console.log(crypto.bytesToHex(hash));
 */

/**
 * Read a 4-byte LE length prefix from the given buffer, then extract
 * the payload bytes.
 */
function unpack(module, ptr, totalLen) {
  if (totalLen < 4) return new Uint8Array(0);
  const heap = module.HEAPU8;
  const payloadLen =
    heap[ptr] |
    (heap[ptr + 1] << 8) |
    (heap[ptr + 2] << 16) |
    (heap[ptr + 3] << 24);
  if (payloadLen <= 0 || payloadLen + 4 > totalLen) return new Uint8Array(0);
  return new Uint8Array(heap.buffer.slice(ptr + 4, ptr + 4 + payloadLen));
}

/**
 * Allocate WASM memory, copy data in, return pointer.
 */
function toWasm(module, data) {
  const ptr = module._malloc(data.length);
  module.HEAPU8.set(data, ptr);
  return ptr;
}

/**
 * Call a WASM function that takes (ptr, len) and returns a length-prefixed
 * result via an out-pointer.
 */
function callUnary(module, fn, data) {
  const dataPtr = toWasm(module, data);
  const outLenPtr = module._malloc(4);

  const resultPtr = fn(dataPtr, data.length, outLenPtr);
  const totalLen = module.HEAPU32[outLenPtr >> 2];

  const result = unpack(module, resultPtr, totalLen);

  module._js_free(resultPtr);
  module._free(dataPtr);
  module._free(outLenPtr);

  return result;
}

/**
 * Call a WASM function that takes (ptr1, len1, ptr2, len2) and returns
 * a length-prefixed result.
 */
function callBinary(module, fn, a, b) {
  const aPtr = toWasm(module, a);
  const bPtr = toWasm(module, b);
  const outLenPtr = module._malloc(4);

  const resultPtr = fn(aPtr, a.length, bPtr, b.length, outLenPtr);
  const totalLen = module.HEAPU32[outLenPtr >> 2];

  const result = unpack(module, resultPtr, totalLen);

  module._js_free(resultPtr);
  module._free(aPtr);
  module._free(bPtr);
  module._free(outLenPtr);

  return result;
}

/**
 * Call a WASM function with 4 byte-array arguments.
 */
function callQuad(module, fn, a, b, c, d) {
  const aPtr = toWasm(module, a);
  const bPtr = toWasm(module, b);
  const cPtr = toWasm(module, c);
  const dPtr = toWasm(module, d);
  const outLenPtr = module._malloc(4);

  const resultPtr = fn(
    aPtr, a.length,
    bPtr, b.length,
    cPtr, c.length,
    dPtr, d.length,
    outLenPtr
  );
  const totalLen = module.HEAPU32[outLenPtr >> 2];

  const result = unpack(module, resultPtr, totalLen);

  module._js_free(resultPtr);
  module._free(aPtr);
  module._free(bPtr);
  module._free(cPtr);
  module._free(dPtr);
  module._free(outLenPtr);

  return result;
}

export class LeanServerCrypto {
  constructor(module) {
    this._mod = module;
  }

  /**
   * Initialize the WASM module. Must be called once before any other method.
   * @param {string} [wasmPath] - Optional path to lean_crypto.js
   * @returns {Promise<LeanServerCrypto>}
   */
  static async init(wasmPath) {
    // Load the Emscripten module
    const factory = wasmPath
      ? (await import(wasmPath)).default
      : (typeof LeanCrypto !== 'undefined' ? LeanCrypto : null);

    if (!factory) {
      throw new Error(
        'LeanCrypto not found. Include lean_crypto.js or pass wasmPath.'
      );
    }

    const mod = await factory();
    return new LeanServerCrypto(mod);
  }

  // ── SHA-256 ──────────────────────────────────────────────

  /**
   * SHA-256 hash.
   * @param {Uint8Array} data - Input data
   * @returns {Uint8Array} 32-byte digest
   */
  sha256(data) {
    return callUnary(this._mod, this._mod._js_sha256, data);
  }

  /**
   * HMAC-SHA-256.
   * @param {Uint8Array} key - HMAC key
   * @param {Uint8Array} msg - Message to authenticate
   * @returns {Uint8Array} 32-byte MAC
   */
  hmacSha256(key, msg) {
    return callBinary(this._mod, this._mod._js_hmac_sha256, key, msg);
  }

  /**
   * HKDF-Extract (TLS 1.3).
   * @param {Uint8Array} salt
   * @param {Uint8Array} ikm - Input key material
   * @returns {Uint8Array} 32-byte PRK
   */
  hkdfExtract(salt, ikm) {
    return callBinary(this._mod, this._mod._js_hkdf_extract, salt, ikm);
  }

  // ── AES-128-GCM ─────────────────────────────────────────

  /**
   * AES-128-GCM encrypt.
   * @param {Uint8Array} key - 16-byte key
   * @param {Uint8Array} iv  - 12-byte IV/nonce
   * @param {Uint8Array} aad - Additional authenticated data
   * @param {Uint8Array} plaintext
   * @returns {Uint8Array} ciphertext + 16-byte authentication tag
   */
  aesGcmEncrypt(key, iv, aad, plaintext) {
    return callQuad(this._mod, this._mod._js_aes_gcm_encrypt,
                    key, iv, aad, plaintext);
  }

  /**
   * AES-128-GCM decrypt.
   * @param {Uint8Array} key - 16-byte key
   * @param {Uint8Array} iv  - 12-byte IV/nonce
   * @param {Uint8Array} aad - Additional authenticated data
   * @param {Uint8Array} ciphertextWithTag - ciphertext + 16-byte tag
   * @returns {Uint8Array|null} Plaintext, or null if authentication fails
   */
  aesGcmDecrypt(key, iv, aad, ciphertextWithTag) {
    const result = callQuad(this._mod, this._mod._js_aes_gcm_decrypt,
                            key, iv, aad, ciphertextWithTag);
    return result.length > 0 ? result : null;
  }

  // ── X25519 Key Exchange ──────────────────────────────────

  /**
   * Generate X25519 public key from private key.
   * @param {Uint8Array} privateKey - 32-byte private key
   * @returns {Uint8Array} 32-byte public key
   */
  x25519PublicKey(privateKey) {
    return callUnary(this._mod, this._mod._js_x25519_base, privateKey);
  }

  /**
   * X25519 Diffie-Hellman key exchange.
   * @param {Uint8Array} privateKey - 32-byte private key
   * @param {Uint8Array} publicKey  - 32-byte peer's public key
   * @returns {Uint8Array} 32-byte shared secret
   */
  x25519SharedSecret(privateKey, publicKey) {
    return callBinary(this._mod, this._mod._js_x25519_scalarmult,
                      privateKey, publicKey);
  }

  /**
   * Generate a fresh X25519 key pair using Web Crypto for randomness.
   * @returns {{ privateKey: Uint8Array, publicKey: Uint8Array }}
   */
  x25519KeyPair() {
    const privateKey = new Uint8Array(32);
    crypto.getRandomValues(privateKey);
    const publicKey = this.x25519PublicKey(privateKey);
    return { privateKey, publicKey };
  }

  // ── Hex Encoding ─────────────────────────────────────────

  /**
   * Convert bytes to hex string.
   * @param {Uint8Array} data
   * @returns {string}
   */
  bytesToHex(data) {
    const result = callUnary(this._mod, this._mod._js_bytes_to_hex, data);
    return new TextDecoder().decode(result);
  }

  // ── HPACK (HTTP/2 Header Compression) ────────────────────

  /**
   * Decode HPACK-encoded headers.
   * @param {Uint8Array} data - HPACK-encoded header block
   * @returns {Array<{name: string, value: string}>}
   */
  hpackDecode(data) {
    const buf = callUnary(this._mod, this._mod._js_hpack_decode, data);
    if (buf.length < 4) return [];

    const view = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
    const count = view.getUint32(0, true);
    const headers = [];
    let offset = 4;

    for (let i = 0; i < count && offset < buf.length; i++) {
      const nameLen = view.getUint32(offset, true); offset += 4;
      const name = new TextDecoder().decode(buf.slice(offset, offset + nameLen));
      offset += nameLen;

      const valLen = view.getUint32(offset, true); offset += 4;
      const value = new TextDecoder().decode(buf.slice(offset, offset + valLen));
      offset += valLen;

      headers.push({ name, value });
    }
    return headers;
  }

  // ── Huffman (HPACK sub-codec) ────────────────────────────

  /**
   * Huffman-encode bytes (HPACK RFC 7541 Appendix B).
   * @param {Uint8Array} data
   * @returns {Uint8Array}
   */
  huffmanEncode(data) {
    return callUnary(this._mod, this._mod._js_huffman_encode, data);
  }

  /**
   * Huffman-decode bytes.
   * @param {Uint8Array} data
   * @returns {Uint8Array|null}
   */
  huffmanDecode(data) {
    const result = callUnary(this._mod, this._mod._js_huffman_decode, data);
    return result.length > 0 ? result : null;
  }

  // ── TLS 1.3 Key Derivation ──────────────────────────────

  /**
   * Derive TLS 1.3 handshake keys.
   * @param {Uint8Array} sharedSecret - 32-byte X25519 shared secret
   * @param {Uint8Array} helloHash    - SHA-256 of ClientHello + ServerHello
   * @returns {{ serverKey: Uint8Array, serverIV: Uint8Array,
   *             clientKey: Uint8Array, clientIV: Uint8Array }}
   */
  tlsDeriveHandshake(sharedSecret, helloHash) {
    const buf = callBinary(this._mod, this._mod._js_tls_derive_handshake,
                           sharedSecret, helloHash);
    return {
      serverKey: buf.slice(0, 16),
      serverIV:  buf.slice(16, 28),
      clientKey: buf.slice(28, 44),
      clientIV:  buf.slice(44, 56),
    };
  }

  /**
   * Derive TLS 1.3 application keys.
   * @param {Uint8Array} handshakeSecret
   * @param {Uint8Array} helloHash
   * @returns {{ serverKey: Uint8Array, serverIV: Uint8Array,
   *             clientKey: Uint8Array, clientIV: Uint8Array }}
   */
  tlsDeriveApplication(handshakeSecret, helloHash) {
    const buf = callBinary(this._mod, this._mod._js_tls_derive_application,
                           handshakeSecret, helloHash);
    return {
      serverKey: buf.slice(0, 16),
      serverIV:  buf.slice(16, 28),
      clientKey: buf.slice(28, 44),
      clientIV:  buf.slice(44, 56),
    };
  }

  // ── HTTP/2 Frame Parsing ─────────────────────────────────

  /**
   * Parse an HTTP/2 frame from binary data.
   * @param {Uint8Array} data - Raw frame bytes (≥9 bytes header)
   * @returns {Uint8Array} Re-serialized frame, or empty on failure
   */
  http2ParseFrame(data) {
    return callUnary(this._mod, this._mod._js_http2_parse_frame, data);
  }
}
