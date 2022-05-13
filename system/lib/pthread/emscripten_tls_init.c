/*
 * Copyright 2019 The Emscripten Authors.  All rights reserved.
 * Emscripten is available under two separate licenses, the MIT license and the
 * University of Illinois/NCSA Open Source License.  Both these licenses can be
 * found in the LICENSE file.
 */

#include "pthread_impl.h"

// Uncomment to trace TLS allocations.
#define DEBUG_TLS
#ifdef DEBUG_TLS
#include <stdio.h>
#endif

// linker-generated symbol that loads static TLS data at the given location.
extern void __wasm_init_tls(void *memory);

extern int __dso_handle;

void* emscripten_tls_init(void) {
  if (!__builtin_wasm_tls_size()) {
    return NULL;
  }
  void *tls_block = __pthread_self()->tls_base;
#ifdef DEBUG_TLS
  printf("tls init: thread[%p] dso[%p] tls_base[%p]\n", __pthread_self(), &__dso_handle, tls_block);
#endif
  __wasm_init_tls(tls_block);
  return tls_block;
}
