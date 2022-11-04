#include "types.h"

#include <assert.h>
#include <sched.h>

#define EXP_NUM_REPS 8192
#define EXP_NUM_SUB_REPS 1024
#define EXP_NUM_QUANTS 10

#define EXP_CYCLES_HIST_GRAN 4
#define EXP_MISSES_HIST_GRAN 1
#define EXP_NANOS_HIST_GRAN 1

#ifndef CLFLUSH_ENABLE
uintptr_t* access_pattern_get_evics(pool_s** in, uintptr_t* aggs);
uintptr_t* access_pattern_get_anvil_evics(uintptr_t huge_page,
    size_t huge_page_size, uintptr_t* aggs);
#endif

#ifndef CLFLUSH_ENABLE
uintptr_t* access_pattern_assemble(
    uintptr_t* aggs, uintptr_t* evics, uintptr_t assembly);
#endif

unsigned char select_data_pattern(size_t seed);
unsigned char* vics_init(uintptr_t* vics, size_t num_vics, unsigned char d);

uintptr_t* access_pattern_anvil(uintptr_t huge_page, size_t huge_page_size,
    int row, uintptr_t** vics, size_t* num_vics);

uintptr_t* access_pattern_get_aggs_classic(
    pool_s** pool, int* agg_crds, const char* mns, uintptr_t** vics, size_t* num_vics);

uintptr_t* access_pattern_get_aggs(
    pool_s** pool, int* agg_crds, const char* mns, uintptr_t** vics, size_t* num_vics);

char* access_pattern_caused_flips(
    uintptr_t* vics, size_t num_vics, unsigned char* vic_data);

void access_pattern_install(uintptr_t* aggs, size_t num_aggs, size_t num_lanes);

void access_pattern_time_reads(
    uintptr_t* aggs, uintptr_t* evics, stat_s** cycles, stat_s** misses, stat_s** nanos);

stat_s* access_pattern_time_read_pair(uintptr_t* aggs, size_t num_reps, bool post_flush);

inline __attribute((always_inline)) void access_pattern_flush(uintptr_t* aggs)
{
    for (size_t i = 0; i < NUM_AGGS; i++) {
        clflush((char*)aggs[i]);
    }
}

inline __attribute((always_inline)) void access_pattern_core(
    uintptr_t* aggs, uintptr_t* evics)
{
    uintptr_t agg = aggs[0];

#if NUM_LANES == 2
    uintptr_t bgg = aggs[1];
#endif

    for (size_t c = 0; c < NUM_COAL; c++) {
        /* let's not unroll this loop, otherwise compilation will take forever */

        /* use this to change only the number of NOPs right after a ref. */
        int m = (c == 0) ? 0 : 0;
        for (size_t n = 0; n < (NUM_NOPS + m); n++) {
            asm volatile("nop\n\t");
        }

#ifdef CLFLUSH_ENABLE
#ifdef CLFLUSH_BATCH /* CLFLUSH_ENABLE, CLFLUSH_BATCH */
#if NUM_LANES == 2
#pragma GCC unroll 32768
        for (size_t k = 0; k < NUM_AGGS_PER_SET; k++) {
            asm volatile(
                "mov (%%rcx), %%rcx\n\t"
                "mov (%%rdx), %%rdx\n\t"
                : "+c"(agg), "+d"(bgg)
                :
                :);
#elif NUM_LANES == 1
#pragma GCC unroll 32768
        for (size_t k = 0; k < NUM_AGGS; k++) {
            asm volatile(
                "mov (%%rcx), %%rcx\n\t"
                : "+c"(agg)
                :
                :);
#endif
            if (k == 2 || k == 5 || k == 8) {
#pragma GCC unroll 32768
                for (size_t n = 0; n < NUM_INNER_NOPS; n++) {
                    asm volatile("nop\n\t");
                }
            }
        }

        access_pattern_flush(aggs);

#if CLFLUSH_BATCH_FENCE == NO_FENCE
#elif CLFLUSH_BATCH_FENCE == MFENCE
        asm volatile("mfence\n\t");
#elif CLFLUSH_BATCH_FENCE == LFENCE
    asm volatile("lfence\n\t");
#elif CLFLUSH_BATCH_FENCE == SFENCE
    asm volatile("sfence\n\t");
#endif

#else /* CLFLUSH_ENABLE */
#if NUM_LANES == 2
#pragma GCC unroll 32768
        for (size_t k = 0; k < NUM_AGGS_PER_SET; k++) {
            asm volatile(
                "mov (%%rcx), %%rcx\n\t"
                "mov (%%rdx), %%rdx\n\t"
                : "+c"(agg), "+d"(bgg)
                :
                :);

            /* post flushing seems to work better */
            asm volatile(
                "clflush (%%rcx)\n\t"
                "clflush (%%rdx)\n\t"
#if CLFLUSH_FENCE == NO_FENCE
#elif CLFLUSH_FENCE == MFENCE
                "mfence\n\t"
#elif CLFLUSH_FENCE == LFENCE
                "lfence\n\t"
#elif CLFLUSH_FENCE == SFENCE
                "sfence\n\t"
#endif
                : "+c"(agg), "+d"(bgg)
                :
                :);
#elif NUM_LANES == 1
#pragma GCC unroll 32768
        for (size_t k = 0; k < NUM_AGGS; k++) {
            asm volatile(
                "mov (%%rcx), %%rcx\n\t"
                : "+c"(agg)
                :
                :);

            /* post flushing seems to work better */
            asm volatile(
                "clflush (%%rcx)\n\t"
#if CLFLUSH_FENCE == NO_FENCE
#elif CLFLUSH_FENCE == MFENCE
                "mfence\n\t"
#elif CLFLUSH_FENCE == LFENCE
                "lfence\n\t"
#elif CLFLUSH_FENCE == SFENCE
                "sfence\n\t"
#endif
                : "+c"(agg)
                :
                :);
#endif
        }
#endif
#else /* CLFLUSH_DISABLE */
#ifndef DOUBLE_DOUBLE_LANE /* NO DOUBLE_DOUBLE_LANE */
#if NUM_LANES == 2
#pragma GCC unroll 32768
        for (size_t k = 0;
             k < NUM_CYCLES * (NUM_AGGS_PER_CYCLE_PER_SET + NUM_EVICS_PER_CYCLE_PER_SET);
             k++) {
            asm volatile("mov (%%rcx), %%rcx\n\t"
                         "mov (%%rdx), %%rdx\n\t"
                         : "+c"(agg), "+d"(bgg)
                         :
                         :);
        }
#elif NUM_LANES == 1
#pragma GCC unroll 32768
        for (size_t k = 0;
             k < NUM_SETS * NUM_CYCLES * (NUM_AGGS_PER_CYCLE_PER_SET + NUM_EVICS_PER_CYCLE_PER_SET);
             k++) {
            asm volatile("mov (%%rcx), %%rcx\n\t"
                         : "+c"(agg)
                         :
                         :);
        }
#endif
#else /* DOUBLE_DOUBLE_LANE */
        uintptr_t evic = evics[0];
        uintptr_t fvic = evics[1];

        for (size_t l = 0; l < NUM_CYCLES; l++) {
            for (size_t k = 0; k < NUM_AGGS_PER_CYCLE_PER_SET; k++) {
                asm volatile(
                    "mov (%%rcx), %%rcx\n\t"
                    "mov (%%rdx), %%rdx\n\t"

                    "mov (%%rsi), %%rsi\n\t"
                    "mov (%%rdi), %%rdi\n\t"

                    "mov (%%rsi), %%rsi\n\t"
                    "mov (%%rdi), %%rdi\n\t"

                    "mov (%%rsi), %%rsi\n\t"
                    "mov (%%rdi), %%rdi\n\t"

                    "mov (%%rsi), %%rsi\n\t"
                    "mov (%%rdi), %%rdi\n\t"
                    : "+c"(agg), "+d"(bgg), "+S"(evic), "+D"(fvic)
                    :
                    :);
            }

            /* NOTE: makes assumptions about pattern: 3/9 */
            asm volatile("mov (%%rsi), %%rsi\n\t"
                         "mov (%%rdi), %%rdi\n\t"
                         : "+S"(evic), "+D"(fvic)
                         :
                         :);
        }
#endif
#endif
    }
}

#ifndef CLFLUSH_ENABLE
inline __attribute((always_inline)) void access_pattern_init(
    uintptr_t* aggs, uintptr_t assembly)
{
    /* flush pattern first (side-effect of installing it) */
    access_pattern_flush(aggs);

    /* access in desired way (just one cycle): first aggressors, then rest */
    for (size_t i = 0; i < NUM_AGGS_PER_CYCLE + NUM_EVICS_PER_CYCLE; i++) {
        if (0x1 & (assembly >> i)) {
            asm volatile(
                "mov (%%rcx), %%rdx\n\t"
                :
                : "c"(aggs[i])
                : "rdx", "memory");
        }
    }

    for (size_t i = 0; i < NUM_AGGS_PER_CYCLE + NUM_EVICS_PER_CYCLE; i++) {
        if (0x0 & (assembly >> i)) {
            asm volatile(
                "mov (%%rcx), %%rdx\n\t"
                :
                : "c"(aggs[i])
                : "rdx", "memory");
        }
    }
}
#endif

inline __attribute((always_inline)) void access_pattern_hammer(
    uintptr_t* aggs, uintptr_t* evics)
{
    /* from TRRespass */
    sched_yield();

    for (size_t i = 0; i < NUM_ACTIVATIONS; i++) {
        access_pattern_core(aggs, evics);
    }
}
