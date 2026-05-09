// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * rxnet/port/freertos.h — FreeRTOS/ESP-IDF port.
 *
 * Target: ESP32 / ESP32-S3 / ESP32-C3 and any FreeRTOS + ESP-IDF project.
 *
 * Time:    esp_timer_get_time() → µs → converted to int64_t nanoseconds
 * Mutex:   SemaphoreHandle_t (mutex semaphore)
 * Thread:  TaskHandle_t + xTaskCreate / xTaskCreatePinnedToCore
 * Barrier: mutex semaphore + counting semaphore (turnstile pattern)
 *
 * Tuning macros (override via -D or a project config header):
 *
 *   RXNET_FREERTOS_STACK_SIZE     stack depth in words (default 4096)
 *   RXNET_FREERTOS_TASK_PRIORITY  FreeRTOS priority   (default 5)
 *   RXNET_FREERTOS_CORE_ID        core affinity:  0 or 1 for pinned,
 *                                  -1 for no affinity (default -1)
 *
 * Usage — ESP-IDF CMakeLists.txt:
 *
 *   # RXNET_PORT_FREERTOS is set automatically when ESP_PLATFORM is defined,
 *   # which ESP-IDF's toolchain always defines.  No explicit -D needed.
 *   idf_component_register(
 *       SRCS "main.c"
 *            "${RXNET_DIR}/c/src/runtime.c"
 *            "${RXNET_DIR}/c/src/fsm.c"
 *            "${RXNET_DIR}/c/src/pn.c"
 *            "${RXNET_DIR}/c/src/cyclic.c"
 *            "${RXNET_DIR}/c/src/coop.c"
 *            "${RXNET_DIR}/c/src/thread.c"
 *       INCLUDE_DIRS "${RXNET_DIR}/c/include"
 *   )
 *
 * Sleep precision note:
 *   FreeRTOS tick resolution is typically 1 ms (configTICK_RATE_HZ = 1000).
 *   rx_tick_sleep_until() uses vTaskDelay for the millisecond portion and
 *   a busy-wait spin for the sub-millisecond remainder.  For periods ≥ 10 ms
 *   (as in the rxnet examples) the jitter is < 1 ms, which is acceptable
 *   for reactive-synchronous applications on MCUs.
 *
 * Thread pinning note:
 *   On dual-core ESP32/ESP32-S3, set RXNET_FREERTOS_CORE_ID to 0 or 1 to
 *   pin all rxnet worker tasks to a specific core.  The application's main
 *   task (app_main) typically runs on core 1 (PRO_CPU = 0, APP_CPU = 1).
 */
#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── tuning parameters ──────────────────────────────────────────────── */

#ifndef RXNET_FREERTOS_STACK_SIZE
#  define RXNET_FREERTOS_STACK_SIZE    4096u  /* words */
#endif

#ifndef RXNET_FREERTOS_TASK_PRIORITY
#  define RXNET_FREERTOS_TASK_PRIORITY 5u
#endif

#ifndef RXNET_FREERTOS_CORE_ID
#  define RXNET_FREERTOS_CORE_ID       (-1)   /* tskNO_AFFINITY */
#endif

/* ── time ───────────────────────────────────────────────────────────── */

typedef int64_t rx_tick_t;   /* nanoseconds since power-on */

static inline rx_tick_t rx_tick_now(void)
{
    /* esp_timer_get_time() returns int64_t microseconds since boot. */
    return esp_timer_get_time() * 1000LL;
}

static inline rx_tick_t rx_tick_add_us(rx_tick_t t, long us)
{
    return t + (int64_t)us * 1000LL;
}

static inline int rx_tick_compare(rx_tick_t a, rx_tick_t b)
{
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

static inline void rx_tick_sleep_until(rx_tick_t target_ns)
{
    rx_tick_t now_ns = rx_tick_now();
    if (now_ns >= target_ns) return;

    int64_t rem_ns = target_ns - now_ns;

    /* Sleep for the millisecond portion via the FreeRTOS tick scheduler. */
    TickType_t ticks = (TickType_t)(rem_ns / 1000000LL / portTICK_PERIOD_MS);
    if (ticks > 0) vTaskDelay(ticks);

    /* Busy-wait for the sub-millisecond remainder to improve accuracy. */
    while (rx_tick_now() < target_ns) {
        /* tight spin — typically < 1 ms */
    }
}

/* ── mutex ──────────────────────────────────────────────────────────── */

typedef SemaphoreHandle_t rx_mutex_t;

static inline void rx_mutex_init(rx_mutex_t *m)
{
    *m = xSemaphoreCreateMutex();
}

static inline void rx_mutex_lock(rx_mutex_t *m)
{
    xSemaphoreTake(*m, portMAX_DELAY);
}

static inline void rx_mutex_unlock(rx_mutex_t *m)
{
    xSemaphoreGive(*m);
}

/* ── thread ─────────────────────────────────────────────────────────── */

typedef TaskHandle_t rx_thread_t;

static inline int rx_thread_create(rx_thread_t *t,
                                   void (*fn)(void *), void *arg)
{
    BaseType_t ret;

#if RXNET_FREERTOS_CORE_ID >= 0
    ret = xTaskCreatePinnedToCore(
        (TaskFunction_t)fn,
        "rxnet",
        RXNET_FREERTOS_STACK_SIZE,
        arg,
        RXNET_FREERTOS_TASK_PRIORITY,
        t,
        RXNET_FREERTOS_CORE_ID
    );
#else
    ret = xTaskCreate(
        (TaskFunction_t)fn,
        "rxnet",
        RXNET_FREERTOS_STACK_SIZE,
        arg,
        RXNET_FREERTOS_TASK_PRIORITY,
        t
    );
#endif

    return (ret == pdPASS) ? 0 : -1;
}

/* ── barrier (turnstile pattern with counting semaphore) ────────────── */
/*
 * A generation-based barrier using:
 *   - a mutex semaphore to protect the shared counter
 *   - a counting semaphore as a turnstile
 *
 * When the N-th thread arrives it "gives" the turnstile N times,
 * releasing all waiting threads simultaneously.
 */

typedef struct {
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t turnstile;  /* counting semaphore, max = total */
    unsigned int      waiting;
    unsigned int      total;
    unsigned int      generation;
} rx_barrier_t;

static inline int rx_barrier_init(rx_barrier_t *b, unsigned int count)
{
    b->waiting    = 0;
    b->total      = count;
    b->generation = 0;
    b->mutex      = xSemaphoreCreateMutex();
    b->turnstile  = xSemaphoreCreateCounting((UBaseType_t)count, 0);
    return (b->mutex != NULL && b->turnstile != NULL) ? 0 : -1;
}

static inline void rx_barrier_wait(rx_barrier_t *b)
{
    xSemaphoreTake(b->mutex, portMAX_DELAY);
    if (++b->waiting == b->total) {
        b->generation++;
        b->waiting = 0;
        /* Release all waiting threads. */
        for (unsigned int i = 0; i < b->total; ++i)
            xSemaphoreGive(b->turnstile);
    }
    xSemaphoreGive(b->mutex);
    xSemaphoreTake(b->turnstile, portMAX_DELAY);
}

/* ── trace subsystem defaults ───────────────────────────────────────── */

#ifndef RX_TRACE_NOW_NS
#  define RX_TRACE_NOW_NS() rx_tick_now()
#endif

#ifndef RX_TRACE_LOCK_TYPE
#  define RX_TRACE_LOCK_TYPE          SemaphoreHandle_t
#  define RX_TRACE_LOCK_INIT(lk)      ((lk) = xSemaphoreCreateMutex(), 0)
#  define RX_TRACE_LOCK_ACQUIRE(lk)   xSemaphoreTake((lk), portMAX_DELAY)
#  define RX_TRACE_LOCK_RELEASE(lk)   xSemaphoreGive((lk))
#endif

#ifdef __cplusplus
}
#endif
