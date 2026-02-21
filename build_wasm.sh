#!/usr/bin/env bash
# ── build_wasm.sh ─────────────────────────────────────────────
# Compiles LeanServerWASM → WebAssembly via Lean 4 → C → Emscripten.
#
# Prerequisites:
#   • Lean 4 v4.27.0 (elan)
#   • Emscripten SDK (emcc in PATH)
#   • LeanServer (fetched by Lake as git dependency, or local at ../LeanServer6)
#
# Output: dist/lean_crypto.{js,wasm}
# ──────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LEAN_PREFIX="$(lean --print-prefix)"
LEAN_INCLUDE="${LEAN_PREFIX}/include"
LEAN_LIB="${LEAN_PREFIX}/lib/lean"

# ── Paths ────────────────────────────────────────────────────
# Auto-detect LeanServer IR: git dep (.lake/packages) or local path
if [ -d ".lake/packages/LeanServer/.lake/build/ir" ]; then
  LEANSERVER_IR=".lake/packages/LeanServer/.lake/build/ir"
elif [ -d "../LeanServer6/.lake/build/ir" ]; then
  LEANSERVER_IR="../LeanServer6/.lake/build/ir"
else
  echo "❌ Cannot find LeanServer IR files. Run 'lake build' first."
  exit 1
fi
WASM_IR=".lake/build/ir"
OUT_DIR="dist"

echo "═══════════════════════════════════════════════════════════"
echo "  LeanServerWASM → WebAssembly Build"
echo "═══════════════════════════════════════════════════════════"
echo ""
echo "  Lean prefix:  ${LEAN_PREFIX}"
echo "  Include dir:  ${LEAN_INCLUDE}"
echo "  Output:       ${OUT_DIR}/lean_crypto.{js,wasm}"
echo ""

# ── Step 1: Build Lean → C ──────────────────────────────────
echo "▶ Step 1: Building Lean sources..."
lake build 2>&1 | tail -5
echo "  ✅ Lean → C compilation done"
echo ""

# ── Step 2: Collect all generated C files ────────────────────
echo "▶ Step 2: Collecting C sources..."

# Pure modules from LeanServer (no FFI, no Server/HTTPServer, no Db)
PURE_C_FILES=""
for dir in Core Crypto Protocol Spec; do
    for f in "${LEANSERVER_IR}/LeanServer/${dir}/"*.c; do
        base="$(basename "$f")"
        # Skip FFI.c (has @[extern] to OpenSSL) and server modules
        if [[ "$base" == "FFI.c" ]]; then
            echo "  ⊘ Skipping ${dir}/${base} (OpenSSL FFI)"
            continue
        fi
        PURE_C_FILES="${PURE_C_FILES} $f"
    done
done

# Server/Concurrency.lean (pure)
PURE_C_FILES="${PURE_C_FILES} ${LEANSERVER_IR}/LeanServer/Server/Concurrency.c"

# Proofs module
PURE_C_FILES="${PURE_C_FILES} ${LEANSERVER_IR}/LeanServer/Proofs.c"

# WasmAPI wrapper
PURE_C_FILES="${PURE_C_FILES} ${WASM_IR}/WasmAPI.c"

echo "  📦 $(echo ${PURE_C_FILES} | wc -w | tr -d ' ') C source files"
echo ""

# ── Step 3: Compile with Emscripten ──────────────────────────
echo "▶ Step 3: Compiling to WebAssembly with Emscripten..."
mkdir -p "${OUT_DIR}"

EXPORTED_FUNCTIONS="[
  '_js_sha256',
  '_js_hmac_sha256',
  '_js_hkdf_extract',
  '_js_aes_gcm_encrypt',
  '_js_aes_gcm_decrypt',
  '_js_x25519_base',
  '_js_x25519_scalarmult',
  '_js_bytes_to_hex',
  '_js_hpack_decode',
  '_js_huffman_encode',
  '_js_huffman_decode',
  '_js_tls_derive_handshake',
  '_js_tls_derive_application',
  '_js_http2_parse_frame',
  '_js_free',
  '_malloc',
  '_free'
]"

EXPORTED_RUNTIME="[
  'ccall',
  'cwrap',
  'setValue',
  'getValue',
  'writeArrayToMemory',
  'HEAPU8',
  'HEAPU32'
]"

emcc \
  -O2 \
  -s WASM=1 \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=67108864 \
  -s MAXIMUM_MEMORY=536870912 \
  -s EXPORTED_FUNCTIONS="${EXPORTED_FUNCTIONS}" \
  -s EXPORTED_RUNTIME_METHODS="${EXPORTED_RUNTIME}" \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="LeanCrypto" \
  -s NO_EXIT_RUNTIME=1 \
  -s FILESYSTEM=0 \
  -s ASSERTIONS=0 \
  -s ENVIRONMENT='web,worker' \
  -I wasm \
  -I "${LEAN_INCLUDE}" \
  -DLEAN_EMSCRIPTEN \
  wasm/lean_runtime_wasm.c \
  wasm/init_stubs_wasm.c \
  wasm/wasm_glue.c \
  ${PURE_C_FILES} \
  -o "${OUT_DIR}/lean_crypto.js"

echo ""
echo "  ✅ WebAssembly compilation done"

# ── Step 4: Report sizes ─────────────────────────────────────
JS_SIZE=$(du -h "${OUT_DIR}/lean_crypto.js" | cut -f1)
WASM_SIZE=$(du -h "${OUT_DIR}/lean_crypto.wasm" | cut -f1)

echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  Build Complete!"
echo ""
echo "  📄 ${OUT_DIR}/lean_crypto.js     (${JS_SIZE})"
echo "  📦 ${OUT_DIR}/lean_crypto.wasm   (${WASM_SIZE})"
echo ""
echo "  Usage:"
echo "    <script src=\"lean_crypto.js\"></script>"
echo "    <script>"
echo "      LeanCrypto().then(mod => {"
echo "        const hash = mod.sha256(new Uint8Array([0x61,0x62,0x63]));"
echo "        console.log(hash);  // SHA-256 of 'abc'"
echo "      });"
echo "    </script>"
echo ""
echo "  Or open:  dist/index.html"
echo "═══════════════════════════════════════════════════════════"
