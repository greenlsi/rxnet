#pragma once

/*
 * Compile-time limits for fixed-size C runtime buffers.
 * Override with -D flags in your build system if needed.
 */
#ifndef RXNET_MAX_INPUT_SIZE
#define RXNET_MAX_INPUT_SIZE 256u
#endif

#ifndef RXNET_MAX_DEFERRED_ACTIONS
#define RXNET_MAX_DEFERRED_ACTIONS 16u
#endif

#ifndef RXNET_MAX_RUNTIME_NODES
#define RXNET_MAX_RUNTIME_NODES 16u
#endif

#if RXNET_MAX_INPUT_SIZE < 1u
#error "RXNET_MAX_INPUT_SIZE must be >= 1"
#endif

#if RXNET_MAX_DEFERRED_ACTIONS < 1u
#error "RXNET_MAX_DEFERRED_ACTIONS must be >= 1"
#endif

#if RXNET_MAX_RUNTIME_NODES < 1u
#error "RXNET_MAX_RUNTIME_NODES must be >= 1"
#endif
