// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#pragma once

/*
 * rxtest.h — minimal test harness for rxnet
 *
 * Designed for portability: only requires printf (can be redirected on embedded).
 * No dynamic allocation, no POSIX dependencies.
 *
 * Usage:
 *   void test_something(void) {
 *       ASSERT_EQ(0, some_function());
 *   }
 *
 *   int main(void) {
 *       TEST_SUITE("My suite");
 *       RUN_TEST(test_something);
 *       TEST_SUMMARY();
 *   }
 */

#include <stdio.h>

static int rxtest_run    = 0;
static int rxtest_failed = 0;
static int rxtest_current_failed = 0;

#define TEST_SUITE(name) \
    printf("\n=== " name " ===\n")

#define RUN_TEST(fn) do {              \
    rxtest_run++;                      \
    rxtest_current_failed = 0;         \
    printf("  %-55s ", #fn);           \
    fn();                              \
    if (!rxtest_current_failed) {      \
        printf("PASS\n");              \
    }                                  \
} while (0)

/* ASSERT stops the current test on failure (via return). */
#define ASSERT(cond) do {                                               \
    if (!(cond)) {                                                      \
        printf("FAIL\n    Assertion failed: %s\n    at %s:%d\n",       \
               #cond, __FILE__, __LINE__);                              \
        rxtest_failed++;                                                \
        rxtest_current_failed = 1;                                      \
        return;                                                         \
    }                                                                   \
} while (0)

#define ASSERT_EQ(expected, actual) do {                                          \
    long long _e = (long long)(expected);                                         \
    long long _a = (long long)(actual);                                           \
    if (_e != _a) {                                                               \
        printf("FAIL\n    Expected %lld, got %lld\n    at %s:%d\n",              \
               _e, _a, __FILE__, __LINE__);                                       \
        rxtest_failed++;                                                          \
        rxtest_current_failed = 1;                                                \
        return;                                                                   \
    }                                                                             \
} while (0)

#define ASSERT_NULL(ptr)     ASSERT((ptr) == NULL)
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL)
#define ASSERT_TRUE(cond)    ASSERT((cond))
#define ASSERT_FALSE(cond)   ASSERT(!(cond))

/*
 * Place TEST_SUMMARY() as the last statement in main().
 * Returns 0 on all pass, 1 if any test failed.
 */
#define TEST_SUMMARY() do {                                             \
    int _passed = rxtest_run - rxtest_failed;                          \
    printf("\n%d/%d tests passed", _passed, rxtest_run);              \
    if (rxtest_failed > 0) {                                           \
        printf(", %d FAILED\n", rxtest_failed);                        \
        return 1;                                                       \
    }                                                                   \
    printf("\n");                                                       \
    return 0;                                                           \
} while (0)
