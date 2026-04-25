// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include "rxnet/port/zephyr.h"

K_THREAD_STACK_ARRAY_DEFINE(_rxnet_zephyr_stacks,
                             RXNET_ZEPHYR_MAX_THREADS,
                             RXNET_ZEPHYR_STACK_SIZE);

unsigned int _rxnet_zephyr_thread_idx = 0u;
