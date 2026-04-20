// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

/*
 * rxnet/port.h — platform port auto-selector.
 *
 * Includes one of three port headers based on the build environment:
 *
 *   ESP_PLATFORM or RXNET_PORT_FREERTOS  → port/freertos.h  (ESP-IDF)
 *   CONFIG_ZEPHYR or RXNET_PORT_ZEPHYR   → port/zephyr.h    (Zephyr)
 *   (default)                            → port/posix.h     (Linux/macOS)
 *
 * The selected port header defines the following API used by all rxnet
 * schedulers (cyclic, coop, thread) and the trace subsystem:
 *
 *   Time
 *   ----
 *   typedef int64_t rx_tick_t;            // nanoseconds, monotonic
 *   rx_tick_t rx_tick_now(void);
 *   rx_tick_t rx_tick_add_us(rx_tick_t t, long us);
 *   int       rx_tick_compare(rx_tick_t a, rx_tick_t b); // -1 / 0 / +1
 *   void      rx_tick_sleep_until(rx_tick_t target);
 *
 *   Mutex
 *   -----
 *   rx_mutex_t                            // opaque type
 *   void rx_mutex_init(rx_mutex_t *m)
 *   void rx_mutex_lock(rx_mutex_t *m)
 *   void rx_mutex_unlock(rx_mutex_t *m)
 *
 *   Thread
 *   ------
 *   rx_thread_t                           // opaque type
 *   int  rx_thread_create(rx_thread_t *t, void (*fn)(void *), void *arg)
 *        // returns 0 on success, -1 on failure
 *
 *   Barrier (BSP — bulk-synchronous parallel)
 *   -----------------------------------------
 *   rx_barrier_t                          // opaque type
 *   int  rx_barrier_init(rx_barrier_t *b, unsigned int count)
 *        // returns 0 on success, -1 on failure
 *   void rx_barrier_wait(rx_barrier_t *b)
 *        // blocks until `count` threads have called wait()
 *        // generation-based: reusable across multiple rounds
 *
 * The port header also sets the defaults for the trace subsystem hooks
 * (RX_TRACE_NOW_NS, RX_TRACE_LOCK_TYPE, RX_TRACE_LOCK_INIT, …) so that
 * trace.h does not need to be included separately before trace.h.
 *
 * Forcing a specific port
 * -----------------------
 * Define one of the following before including any rxnet header (e.g. via
 * compiler -D flag or a top-level config header):
 *
 *   -DRXNET_PORT_POSIX      force POSIX even if ESP_PLATFORM is set
 *   -DRXNET_PORT_FREERTOS   force FreeRTOS/ESP-IDF port
 *   -DRXNET_PORT_ZEPHYR     force Zephyr port
 */
#pragma once

#if defined(RXNET_PORT_FREERTOS) || \
    (!defined(RXNET_PORT_POSIX) && !defined(RXNET_PORT_ZEPHYR) && defined(ESP_PLATFORM))
#  include "rxnet/port/freertos.h"

#elif defined(RXNET_PORT_ZEPHYR) || \
      (!defined(RXNET_PORT_POSIX) && !defined(RXNET_PORT_FREERTOS) && defined(CONFIG_ZEPHYR))
#  include "rxnet/port/zephyr.h"

#else
#  include "rxnet/port/posix.h"
#endif
