/*
 * Shadow config.h for WASM builds.
 *
 * Lean's config.h unconditionally defines LEAN_MIMALLOC, which causes
 * inline functions in lean.h to call mi_malloc_small (not available in
 * Emscripten). This wrapper includes the real config.h via #include_next
 * then undefines LEAN_MIMALLOC so the plain malloc path is used instead.
 */
#pragma once
#include_next <lean/config.h>
#undef LEAN_MIMALLOC
