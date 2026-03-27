/*
 * Scorbit SDK — C example call-site benchmarking helpers.
 */

#define _POSIX_C_SOURCE 200809L

#include "c_api_bench.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void c_api_bench_fprint_u64_grouped(FILE *fp, uint64_t v)
{
    if (v == 0) {
        fputc('0', fp);
        return;
    }
    char buf[48];
    size_t pos = sizeof(buf);
    buf[--pos] = '\0';
    int digits = 0;
    while (v > 0) {
        if (digits > 0 && digits % 3 == 0) {
            buf[--pos] = ',';
        }
        buf[--pos] = (char)('0' + (unsigned)(v % 10u));
        v /= 10u;
        ++digits;
    }
    fputs(buf + pos, fp);
}

void c_api_bench_fprint_size_grouped(FILE *fp, size_t z)
{
    c_api_bench_fprint_u64_grouped(fp, (uint64_t)z);
}

void c_api_bench_fprint_double_grouped(FILE *fp, double x, int dec)
{
    char tmp[160];
    if (dec < 0) {
        dec = 0;
    }
    if (dec > 30) {
        dec = 30;
    }
    snprintf(tmp, sizeof(tmp), "%.*f", dec, x);
    const int neg = (tmp[0] == '-');
    char *s = neg ? tmp + 1 : tmp;
    char *dot = strchr(s, '.');
    if (!dot) {
        fputs(tmp, fp);
        return;
    }
    if (neg) {
        fputc('-', fp);
    }
    *dot = '\0';
    if (s[0] == '\0') {
        c_api_bench_fprint_u64_grouped(fp, 0);
    } else {
        unsigned long long w = strtoull(s, NULL, 10);
        c_api_bench_fprint_u64_grouped(fp, (uint64_t)w);
    }
    fprintf(fp, ".%s", dot + 1);
}

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#    include <x86intrin.h>
#    define C_API_BENCH_HAS_TSC 1
static uint64_t c_api_bench_rdtsc(void)
{
    return (uint64_t)__rdtsc();
}
#else
#    define C_API_BENCH_HAS_TSC 0
#endif

#if defined(CLOCK_THREAD_CPUTIME_ID)
#    define C_API_BENCH_HAS_THREAD_CPU 1
#else
#    define C_API_BENCH_HAS_THREAD_CPU 0
#endif

static int g_thread_cpu_probe_done;
static int g_thread_cpu_ok;

static void probe_thread_cpu(void)
{
    if (g_thread_cpu_probe_done) {
        return;
    }
    g_thread_cpu_probe_done = 1;
#if C_API_BENCH_HAS_THREAD_CPU
    struct timespec ts;
    g_thread_cpu_ok = (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0);
#else
    g_thread_cpu_ok = 0;
#endif
}

int c_api_bench_thread_cpu_supported(void)
{
    probe_thread_cpu();
    return g_thread_cpu_ok;
}

static uint64_t timespec_diff_ns(const struct timespec *a, const struct timespec *b)
{
    long long sec = (long long)b->tv_sec - (long long)a->tv_sec;
    long long nsec = (long long)b->tv_nsec - (long long)a->tv_nsec;
    if (nsec < 0) {
        --sec;
        nsec += 1000000000LL;
    }
    return (uint64_t)(sec * 1000000000LL + nsec);
}

int c_api_bench_tsc_supported(void)
{
#if C_API_BENCH_HAS_TSC
    return 1;
#else
    return 0;
#endif
}

void c_api_bench_tick(CApiBenchTick *t)
{
    probe_thread_cpu();
    struct timespec wall;
    clock_gettime(CLOCK_MONOTONIC, &wall);
    t->wall_sec = (int64_t)wall.tv_sec;
    t->wall_nsec = (int64_t)wall.tv_nsec;

#if C_API_BENCH_HAS_THREAD_CPU
    if (g_thread_cpu_ok) {
        struct timespec cpu;
        t->cpu_valid = (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu) == 0);
        if (t->cpu_valid) {
            t->cpu_sec = (int64_t)cpu.tv_sec;
            t->cpu_nsec = (int64_t)cpu.tv_nsec;
        }
    } else
#endif
    {
        t->cpu_valid = 0;
        t->cpu_sec = 0;
        t->cpu_nsec = 0;
    }

#if C_API_BENCH_HAS_TSC
    t->tsc = c_api_bench_rdtsc();
#else
    t->tsc = 0u;
#endif
}

void c_api_bench_tock(const CApiBenchTick *t, CApiBenchSpan *out)
{
    struct timespec wall_end;
    clock_gettime(CLOCK_MONOTONIC, &wall_end);
    struct timespec wall_start = {.tv_sec = (time_t)t->wall_sec, .tv_nsec = t->wall_nsec};
    out->wall_ns = timespec_diff_ns(&wall_start, &wall_end);

    if (t->cpu_valid) {
#if C_API_BENCH_HAS_THREAD_CPU
        struct timespec cpu_end;
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_end) == 0) {
            struct timespec cpu_start = {.tv_sec = (time_t)t->cpu_sec, .tv_nsec = t->cpu_nsec};
            out->cpu_ns = timespec_diff_ns(&cpu_start, &cpu_end);
        } else
#endif
        {
            out->cpu_ns = 0;
        }
    } else {
        out->cpu_ns = 0;
    }

#if C_API_BENCH_HAS_TSC
    out->tsc_delta = c_api_bench_rdtsc() - t->tsc;
#else
    out->tsc_delta = 0u;
#endif
}

static int compare_u64(const void *ap, const void *bp)
{
    uint64_t a = *(const uint64_t *)ap;
    uint64_t b = *(const uint64_t *)bp;
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

/** Inclusive percentile index: p in [0,100], n >= 1. */
static size_t percentile_index(size_t n, unsigned p)
{
    if (n == 0) {
        return 0;
    }
    if (p >= 100) {
        return n - 1;
    }
    double idx = (double)p / 100.0 * (double)(n - 1);
    size_t i = (size_t)(idx + 0.5);
    if (i >= n) {
        i = n - 1;
    }
    return i;
}

void c_api_bench_print_distribution(const char *label, const uint64_t *samples, size_t n)
{
    printf("\n%s\n", label);
    if (n == 0 || samples == NULL) {
        printf("  (no samples)\n");
        return;
    }

    uint64_t *sorted = (uint64_t *)malloc(n * sizeof(uint64_t));
    if (!sorted) {
        printf("  (allocation failed)\n");
        return;
    }
    memcpy(sorted, samples, n * sizeof(uint64_t));
    qsort(sorted, n, sizeof(uint64_t), compare_u64);

    long double sum = 0;
    for (size_t i = 0; i < n; ++i) {
        sum += (long double)sorted[i];
    }
    long double mean = sum / (long double)n;

    size_t i50 = percentile_index(n, 50);
    size_t i90 = percentile_index(n, 90);
    size_t i95 = percentile_index(n, 95);
    size_t i99 = percentile_index(n, 99);

    fputs("  count=", stdout);
    c_api_bench_fprint_size_grouped(stdout, n);
    fputs(" min=", stdout);
    c_api_bench_fprint_u64_grouped(stdout, sorted[0]);
    fputs(" mean=", stdout);
    c_api_bench_fprint_double_grouped(stdout, (double)mean, 2);
    fputs(" p50=", stdout);
    c_api_bench_fprint_u64_grouped(stdout, sorted[i50]);
    fputs(" p90=", stdout);
    c_api_bench_fprint_u64_grouped(stdout, sorted[i90]);
    fputs(" p95=", stdout);
    c_api_bench_fprint_u64_grouped(stdout, sorted[i95]);
    fputs(" p99=", stdout);
    c_api_bench_fprint_u64_grouped(stdout, sorted[i99]);
    fputs(" max=", stdout);
    c_api_bench_fprint_u64_grouped(stdout, sorted[n - 1]);
    fputc('\n', stdout);

    free(sorted);
}
