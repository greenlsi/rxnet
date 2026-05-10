// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#define RXNET_VERSION       "1.0.0"
#define RXNET_VERSION_MAJOR 1
#define RXNET_VERSION_MINOR 0
#define RXNET_VERSION_PATCH 0

/*
 * Compile-time limits for fixed-size C runtime buffers.
 * Override with -D flags in your build system if needed.
 */
#ifndef RXNET_MAX_DEFERRED_ACTIONS
#define RXNET_MAX_DEFERRED_ACTIONS 16u
#endif

#ifndef RXNET_MAX_RUNTIME_NODES
#define RXNET_MAX_RUNTIME_NODES 16u
#endif

#if RXNET_MAX_DEFERRED_ACTIONS < 1u
#error "RXNET_MAX_DEFERRED_ACTIONS must be >= 1"
#endif

#if RXNET_MAX_RUNTIME_NODES < 1u
#error "RXNET_MAX_RUNTIME_NODES must be >= 1"
#endif

/* Deprecated runtime slot table limit, retained for thread executor ABI. */
#ifndef RXNET_MAX_RUNTIME_SLOTS
#define RXNET_MAX_RUNTIME_SLOTS 32u
#endif

#if RXNET_MAX_RUNTIME_SLOTS < 1u
#error "RXNET_MAX_RUNTIME_SLOTS must be >= 1"
#endif

/* Thread executor: max independent runtimes per rx_thread_exec instance. */
#ifndef RXNET_THREAD_MAX_RUNTIMES
#define RXNET_THREAD_MAX_RUNTIMES 8u
#endif

/* Thread executor: max overlapping activation instants per runtime. */
#ifndef RXNET_THREAD_MAX_ACTIVE_GROUPS
#define RXNET_THREAD_MAX_ACTIVE_GROUPS RXNET_MAX_RUNTIME_NODES
#endif

#if RXNET_THREAD_MAX_RUNTIMES < 1u
#error "RXNET_THREAD_MAX_RUNTIMES must be >= 1"
#endif

#if RXNET_THREAD_MAX_ACTIVE_GROUPS < 1u
#error "RXNET_THREAD_MAX_ACTIVE_GROUPS must be >= 1"
#endif

/* Cyclic executive limits. */
#ifndef RXNET_CE_MAX_TASKS
#define RXNET_CE_MAX_TASKS 8u
#endif

#ifndef RXNET_CE_MAX_SLOTS
#define RXNET_CE_MAX_SLOTS 64u
#endif

#if RXNET_CE_MAX_TASKS < 1u
#error "RXNET_CE_MAX_TASKS must be >= 1"
#endif

#if RXNET_CE_MAX_SLOTS < 1u
#error "RXNET_CE_MAX_SLOTS must be >= 1"
#endif
