/*
 * Scorbit SDK — C example call-site benchmarking helpers.
 *
 * Wall: CLOCK_MONOTONIC. CPU: CLOCK_THREAD_CPUTIME_ID when available.
 * TSC: x86_64 GCC/Clang only; otherwise deltas are 0.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** Snapshot state between begin/end of one timed region. */
typedef struct CApiBenchSpan {
    uint64_t wall_ns;
    uint64_t cpu_ns;
    uint64_t tsc_delta;
} CApiBenchSpan;

/** Opaque clocks at span start (stack-allocated by caller). */
typedef struct CApiBenchTick {
    int64_t wall_sec;
    int64_t wall_nsec;
    int64_t cpu_sec;
    int64_t cpu_nsec;
    uint64_t tsc;
    int cpu_valid;
} CApiBenchTick;

/** Non-zero if thread CPU time clock is available on this system. */
int c_api_bench_thread_cpu_supported(void);

/** Non-zero if hardware TSC deltas are recorded (x86_64 GCC/Clang). */
int c_api_bench_tsc_supported(void);

/** Start a span (monotonic wall + optional thread CPU + optional TSC). */
void c_api_bench_tick(CApiBenchTick *t);

/** Finish span; writes deltas into @a out. */
void c_api_bench_tock(const CApiBenchTick *t, CApiBenchSpan *out);

/**
 * Sort a copy of @a samples (length @a n) and print min, mean, percentiles, max.
 * Units are in the sample domain (e.g. nanoseconds). @a label is printed as a heading.
 */
void c_api_bench_print_distribution(const char *label, const uint64_t *samples, size_t n);

/** Print @a v with comma as thousands separator (e.g. 12,345,678). */
void c_api_bench_fprint_u64_grouped(FILE *fp, uint64_t v);

void c_api_bench_fprint_size_grouped(FILE *fp, size_t z);

/** Print non-negative @a x with grouped integer part and @a dec fractional digits. */
void c_api_bench_fprint_double_grouped(FILE *fp, double x, int dec);
