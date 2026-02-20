# LeanServer WASM — Verified Cryptography in the Browser

**Formally verified cryptography compiled from [Lean 4](https://lean-lang.org) to WebAssembly.**

This project takes the pure-Lean subset of [LeanServer](https://github.com/AfonsoBitoque/LeanServer)
(914 machine-checked theorems, zero `sorry`, zero axioms) and compiles it to
WebAssembly via the **Lean 4 → C → Emscripten → WASM** pipeline.

The result is a JavaScript library that provides TLS 1.3 crypto primitives
with **formal correctness guarantees** — the first verified crypto library
running in the browser.

---

## What's Included

| Module | Functions | Verified Properties |
|--------|-----------|-------------------|
| **SHA-256** | `sha256`, `hmacSha256`, `hkdfExtract` | Determinism, output length = 32 |
| **AES-128-GCM** | `aesGcmEncrypt`, `aesGcmDecrypt` | Encrypt/decrypt inverse, tag length |
| **X25519** | `x25519PublicKey`, `x25519SharedSecret` | DH commutativity, determinism |
| **TLS 1.3** | `tlsDeriveHandshake`, `tlsDeriveApplication` | Key uniqueness, schedule correctness |
| **HPACK** | `hpackDecode`, `huffmanEncode/Decode` | Encode/decode inverse |
| **HTTP/2** | `http2ParseFrame` | Frame structure invariants |

All functions are **pure** (no I/O, no side effects, no FFI) — they run
entirely in WebAssembly memory.

---

## Quick Start

### Use Pre-built WASM

```html
<script src="lean_crypto.js"></script>
<script>
  LeanCrypto().then(mod => {
    // SHA-256
    const data = new TextEncoder().encode('hello');
    const ptr = mod._malloc(data.length);
    mod.HEAPU8.set(data, ptr);
    const outLen = mod._malloc(4);
    const result = mod._js_sha256(ptr, data.length, outLen);
    // ... read length-prefixed result
    mod._js_free(result);
    mod._free(ptr);
    mod._free(outLen);
  });
</script>
```

### Use the JS Wrapper (Recommended)

```javascript
import { LeanServerCrypto } from './lean_server_wasm.js';

const crypto = await LeanServerCrypto.init();

// SHA-256
const hash = crypto.sha256(new TextEncoder().encode('hello'));
console.log(crypto.bytesToHex(hash));

// AES-128-GCM encrypt/decrypt
const key = new Uint8Array(16);  // 16-byte key
const iv  = new Uint8Array(12);  // 12-byte nonce
crypto.getRandomValues(key);
crypto.getRandomValues(iv);

const ct = crypto.aesGcmEncrypt(key, iv, new Uint8Array(0), plaintext);
const pt = crypto.aesGcmDecrypt(key, iv, new Uint8Array(0), ct);

// X25519 Diffie-Hellman
const alice = crypto.x25519KeyPair();
const bob   = crypto.x25519KeyPair();
const sharedA = crypto.x25519SharedSecret(alice.privateKey, bob.publicKey);
const sharedB = crypto.x25519SharedSecret(bob.privateKey, alice.publicKey);
// sharedA === sharedB ✅

// TLS 1.3 Key Derivation
const keys = crypto.tlsDeriveHandshake(sharedSecret, helloHash);
// keys.serverKey, keys.serverIV, keys.clientKey, keys.clientIV
```

---

## Build from Source

### Prerequisites

- [Lean 4 v4.27.0](https://leanprover-community.github.io/get_started.html) (elan)
- [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (emcc in PATH)
- LeanServer cloned alongside this project:
  ```
  parent/
  ├── LeanServer6/    # https://github.com/AfonsoBitoque/LeanServer
  └── LeanServerWASM/ # this project
  ```

### Build Steps

```bash
# 1. Build the Lean project (generates C files)
lake build

# 2. Compile to WebAssembly
./build_wasm.sh

# 3. Open the demo
# (use any HTTP server — file:// won't work due to WASM MIME type)
cd dist
python3 -m http.server 8080
# Open http://localhost:8080
```

### Output

```
dist/
├── lean_crypto.js        # Emscripten JS loader
├── lean_crypto.wasm      # WebAssembly binary
├── lean_server_wasm.js   # High-level JS API wrapper
└── index.html            # Interactive demo page
```

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│              Browser / Node.js                   │
│  lean_server_wasm.js  (JS API)                   │
├─────────────────────────────────────────────────┤
│              Emscripten Glue                     │
│  lean_crypto.js  (module loader)                 │
├─────────────────────────────────────────────────┤
│              WebAssembly                         │
│  lean_crypto.wasm  (~2 MB)                       │
├─────────────────────────────────────────────────┤
│              wasm_glue.c                         │
│  C bridge: WASM memory ↔ Lean ByteArray          │
├─────────────────────────────────────────────────┤
│              WasmAPI.lean                         │
│  @[export] wrappers for Lean functions           │
├─────────────────────────────────────────────────┤
│           LeanServerPure (43 modules)            │
│  SHA-256, AES-GCM, X25519, TLS 1.3, HPACK, ...  │
│  914 theorems │ 0 sorry │ 0 axioms               │
└─────────────────────────────────────────────────┘
```

### Pipeline

```
Lean 4 (.lean)
  │  lake build
  ▼
Generated C (.c)
  │  emcc -O2
  ▼
WebAssembly (.wasm) + JS loader (.js)
```

---

## Security Model

- **Formal guarantees**: All crypto functions backed by 914 machine-checked theorems
- **No `sorry`**: Zero proof gaps — every theorem is fully proven
- **Pure functions**: No I/O, no mutable state, no undefined behavior
- **Memory safety**: Lean's type system prevents buffer overflows by construction
- **No native crypto deps**: Doesn't use Web Crypto API — all algorithms are
  implemented in pure Lean and verified

### Limitations

- **Performance**: Pure-Lean crypto is ~100× slower than native (Web Crypto API).
  Use this for verification/testing, not for bulk encryption.
- **Side channels**: WASM doesn't guarantee constant-time execution.
  The Lean implementation models constant-time operations, but the WASM
  compiler may introduce timing variations.

---

## Project Structure

```
LeanServerWASM/
├── WasmAPI.lean           # Lean → C export wrappers
├── lakefile.toml           # Lake build config (depends on LeanServer)
├── lean-toolchain          # Lean 4 v4.27.0
├── build_wasm.sh           # Lean → C → WASM build script
├── wasm/
│   └── wasm_glue.c         # C bridge for Emscripten
└── dist/
    ├── index.html           # Interactive demo
    ├── lean_server_wasm.js  # High-level JS API
    ├── lean_crypto.js       # (generated) Emscripten loader
    └── lean_crypto.wasm     # (generated) WebAssembly binary
```

---

## License

Apache 2.0 — same as [LeanServer](https://github.com/AfonsoBitoque/LeanServer).
