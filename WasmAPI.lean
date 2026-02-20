import LeanServerPure

/-!
  # LeanServer WASM API

  This module re-exports the pure-Lean crypto, codec, and protocol functions
  from LeanServerPure and marks them with `@[export]` so they become
  C-callable symbols.  The Lean → C → Emscripten → WASM pipeline turns
  these into JavaScript-callable functions via `Module.ccall`.

  ## Exported Functions (C ABI)

  ### Hashing & KDF
  - `wasm_sha256(data, len) → ptr`
  - `wasm_hmac_sha256(key, klen, msg, mlen) → ptr`
  - `wasm_hkdf_extract(salt, slen, ikm, ilen) → ptr`

  ### AES-128-GCM
  - `wasm_aes_gcm_encrypt(key, iv, aad, alen, pt, ptlen) → ptr`
  - `wasm_aes_gcm_decrypt(key, iv, aad, alen, ct, ctlen) → ptr`

  ### X25519
  - `wasm_x25519_scalarmult_base(scalar) → ptr`
  - `wasm_x25519_scalarmult(scalar, point) → ptr`

  ### HPACK
  - `wasm_hpack_encode(headers_json) → ptr`
  - `wasm_hpack_decode(data, len) → ptr`

  ### HTTP/2
  - `wasm_http2_parse_frame(data, len) → ptr`
  - `wasm_http2_create_frame(type, flags, streamId, payload, plen) → ptr`

  ### Hex Encoding
  - `wasm_bytes_to_hex(data, len) → ptr`
  - `wasm_hex_to_bytes(hex_str) → ptr`

  All returned pointers point to length-prefixed byte buffers:
    [4 bytes LE length][payload...]
  The JS glue reads the length, copies the payload, then frees.
-/

namespace LeanServerWASM

-- ═══════════════════════════════════════════════════════════
-- Helpers: ByteArray ↔ C buffer interop
-- ═══════════════════════════════════════════════════════════

/-- Pack a ByteArray into a length-prefixed buffer for JS consumption. -/
def packResult (b : ByteArray) : ByteArray :=
  let len := b.size
  let header := ByteArray.mk #[
    (len &&& 0xFF).toUInt8,
    ((len >>> 8) &&& 0xFF).toUInt8,
    ((len >>> 16) &&& 0xFF).toUInt8,
    ((len >>> 24) &&& 0xFF).toUInt8
  ]
  header ++ b

/-- Pack a String result (UTF-8) into a length-prefixed buffer. -/
def packString (s : String) : ByteArray :=
  packResult s.toUTF8

/-- Pack an Option ByteArray — returns empty (len=0) on None. -/
def packOption (o : Option ByteArray) : ByteArray :=
  match o with
  | some b => packResult b
  | none   => packResult ByteArray.empty

-- ═══════════════════════════════════════════════════════════
-- SHA-256 & HMAC & HKDF
-- ═══════════════════════════════════════════════════════════

/-- SHA-256 hash. Returns 32-byte digest. -/
@[export wasm_sha256]
def wasm_sha256 (data : ByteArray) : ByteArray :=
  packResult (LeanServer.sha256 data)

/-- HMAC-SHA-256. Returns 32-byte MAC. -/
@[export wasm_hmac_sha256]
def wasm_hmac_sha256 (key : ByteArray) (msg : ByteArray) : ByteArray :=
  packResult (LeanServer.hmac_sha256 key msg)

/-- HKDF-Extract (TLS 1.3 key extraction). Returns 32-byte PRK. -/
@[export wasm_hkdf_extract]
def wasm_hkdf_extract (salt : ByteArray) (ikm : ByteArray) : ByteArray :=
  packResult (LeanServer.hkdf_extract salt ikm)

/-- HKDF-Expand-Label (TLS 1.3). Returns derived key material. -/
@[export wasm_hkdf_expand_label]
def wasm_hkdf_expand_label (secret : ByteArray) (label : String)
    (context : ByteArray) (length : UInt16) : ByteArray :=
  packResult (LeanServer.hkdfExpandLabel secret label context length)

/-- Derive secret (TLS 1.3 convenience wrapper). -/
@[export wasm_derive_secret]
def wasm_derive_secret (secret : ByteArray) (label : String)
    (context : ByteArray) : ByteArray :=
  packResult (LeanServer.deriveSecret secret label context)

-- ═══════════════════════════════════════════════════════════
-- AES-128-GCM
-- ═══════════════════════════════════════════════════════════

/-- AES-128-GCM encrypt. Returns ciphertext ++ 16-byte tag. -/
@[export wasm_aes_gcm_encrypt]
def wasm_aes_gcm_encrypt (key : ByteArray) (iv : ByteArray)
    (aad : ByteArray) (plaintext : ByteArray) : ByteArray :=
  let (ct, tag) := LeanServer.aes128_gcm_encrypt key iv aad plaintext
  packResult (ct ++ tag)

/-- AES-128-GCM decrypt. Returns plaintext or empty on auth failure. -/
@[export wasm_aes_gcm_decrypt]
def wasm_aes_gcm_decrypt (key : ByteArray) (iv : ByteArray)
    (aad : ByteArray) (ciphertextWithTag : ByteArray) : ByteArray :=
  packOption (LeanServer.aes128_gcm_decrypt key iv aad ciphertextWithTag)

-- ═══════════════════════════════════════════════════════════
-- X25519 Key Exchange
-- ═══════════════════════════════════════════════════════════

/-- Generate X25519 public key from 32-byte private key.
    Returns 32-byte public key. -/
@[export wasm_x25519_base]
def wasm_x25519_base (privateKey : ByteArray) : ByteArray :=
  packResult (LeanServer.X25519.scalarmult_base privateKey)

/-- X25519 Diffie-Hellman. Returns 32-byte shared secret. -/
@[export wasm_x25519_scalarmult]
def wasm_x25519_scalarmult (scalar : ByteArray) (point : ByteArray) : ByteArray :=
  packResult (LeanServer.X25519.scalarmult scalar point)

-- ═══════════════════════════════════════════════════════════
-- Hex Encoding
-- ═══════════════════════════════════════════════════════════

/-- Encode bytes to hex string. -/
@[export wasm_bytes_to_hex]
def wasm_bytes_to_hex (data : ByteArray) : ByteArray :=
  packString (LeanServer.bytesToHex data)

/-- Decode hex string to bytes. -/
@[export wasm_hex_to_bytes]
def wasm_hex_to_bytes (hexStr : String) : ByteArray :=
  packResult (LeanServer.hexToBytes hexStr)

-- ═══════════════════════════════════════════════════════════
-- HPACK (HTTP/2 Header Compression)
-- ═══════════════════════════════════════════════════════════

/-- Encode headers (stateless). Input: array of (name, value) pairs. -/
@[export wasm_hpack_encode]
def wasm_hpack_encode (headers : Array (String × String)) : ByteArray :=
  packResult (LeanServer.encodeHeadersPublic headers)

/-- Decode HPACK-encoded header block.
    Returns serialized header list or empty on failure. -/
@[export wasm_hpack_decode]
def wasm_hpack_decode (data : ByteArray) : ByteArray :=
  let decoder := LeanServer.initHPACKDecoder
  match LeanServer.decodeHeaderList decoder data with
  | some (headers, _) =>
    let encodeLen (n : Nat) : ByteArray := ByteArray.mk #[
      (n &&& 0xFF).toUInt8,
      ((n >>> 8) &&& 0xFF).toUInt8,
      ((n >>> 16) &&& 0xFF).toUInt8,
      ((n >>> 24) &&& 0xFF).toUInt8
    ]
    let count := headers.size
    let buf := headers.foldl (init := encodeLen count) fun acc h =>
      let nameBytes := h.name.toUTF8
      let valBytes := h.value.toUTF8
      acc ++ encodeLen nameBytes.size ++ nameBytes ++ encodeLen valBytes.size ++ valBytes
    packResult buf
  | none => packResult ByteArray.empty

-- ═══════════════════════════════════════════════════════════
-- HTTP/2 Frames
-- ═══════════════════════════════════════════════════════════

/-- Parse a single HTTP/2 frame from binary data.
    Returns serialized frame or empty on parse failure. -/
@[export wasm_http2_parse_frame]
def wasm_http2_parse_frame (data : ByteArray) : ByteArray :=
  match LeanServer.parseHTTP2Frame data with
  | some frame => packResult (LeanServer.serializeHTTP2Frame frame)
  | none       => packResult ByteArray.empty

/-- Serialize an HTTP/2 frame. -/
@[export wasm_http2_serialize_frame]
def wasm_http2_serialize_frame (frameType : UInt8) (flags : UInt8)
    (streamId : UInt32) (payload : ByteArray) : ByteArray :=
  match LeanServer.FrameType.fromByte frameType with
  | some ft =>
    let frame := LeanServer.createHTTP2Frame ft flags streamId payload
    packResult (LeanServer.serializeHTTP2Frame frame)
  | none => packResult ByteArray.empty

-- ═══════════════════════════════════════════════════════════
-- TLS 1.3 Handshake
-- ═══════════════════════════════════════════════════════════

/-- Parse a TLS 1.3 ClientHello message.
    Returns serialized fields or empty on failure. -/
@[export wasm_tls_parse_client_hello]
def wasm_tls_parse_client_hello (data : ByteArray) : ByteArray :=
  match LeanServer.parseClientHello data with
  | some ch => packResult ch.clientRandom  -- return clientRandom for now
  | none    => packResult ByteArray.empty

/-- Derive TLS 1.3 handshake keys from shared secret + hello hash. -/
@[export wasm_tls_derive_handshake]
def wasm_tls_derive_handshake (sharedSecret : ByteArray)
    (helloHash : ByteArray) : ByteArray :=
  let keys : LeanServer.HandshakeKeys :=
    LeanServer.TLSKeySchedule.deriveHandshake sharedSecret helloHash
  -- Pack: serverKey(16) ++ serverIV(12) ++ clientKey(16) ++ clientIV(12)
  packResult (keys.serverKey ++ keys.serverIV ++ keys.clientKey ++ keys.clientIV)

/-- Derive TLS 1.3 application keys from handshake secret + hello hash. -/
@[export wasm_tls_derive_application]
def wasm_tls_derive_application (handshakeSecret : ByteArray)
    (helloHash : ByteArray) : ByteArray :=
  let keys : LeanServer.ApplicationKeys :=
    LeanServer.TLSKeySchedule.deriveApplication handshakeSecret helloHash
  packResult (keys.serverKey ++ keys.serverIV ++ keys.clientKey ++ keys.clientIV)

-- ═══════════════════════════════════════════════════════════
-- Huffman (HPACK sub-codec)
-- ═══════════════════════════════════════════════════════════

/-- Huffman-encode a byte array. -/
@[export wasm_huffman_encode]
def wasm_huffman_encode (data : ByteArray) : ByteArray :=
  packResult (LeanServer.huffmanEncode data)

/-- Huffman-decode a byte array. -/
@[export wasm_huffman_decode]
def wasm_huffman_decode (data : ByteArray) : ByteArray :=
  packOption (LeanServer.huffmanDecode data)

-- ═══════════════════════════════════════════════════════════
-- Base64
-- ═══════════════════════════════════════════════════════════

/-- Base64-decode a string. -/
@[export wasm_base64_decode]
def wasm_base64_decode (encoded : String) : ByteArray :=
  packOption (LeanServer.Base64.decode encoded)

end LeanServerWASM
