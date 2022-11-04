#pragma once
#define _GNU_SOURCE

/* try to keep includes in this file minimal, and never include a local file */
#include <gsl/gsl_histogram.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* global macros */
#define GB (1 << 30)
#define MB (1 << 20)

#define PATH "/mnt/huge/1gb-file"
#define MAP_HUGE_1GB (30 << MAP_HUGE_SHIFT)

#define BUS_OFFSET_SIZE 3
#define CACHE_LINE_SIZE 64

#define CACHE_LINE_OFFSET_SIZE 6
#define HUGE_2MB_OFFSET_SIZE 21
#define HUGE_1GB_OFFSET_SIZE 30

#define NUM_BITS_PER_BYTE 8

#define PAGE_SIZE 4096
#define EXPORT_PATH "./more-drama-data.csv"

#define TERMINAL_WIDTH 180
#define COLUMN_WIDTH 16
#define SMALL_COLUMN_WIDTH 4

#define MAX_LEN_USR_STR 64
#define MAX_LEN_STR 512

#define tREFI 7800

#define NO_FENCE 0
#define MFENCE 1
#define LFENCE 2
#define SFENCE 3

/* convenient */
#ifdef HWSEC04
#define NUM_NOPS 100
#define NUM_AGGS_PER_SET 2
// #define CLFLUSH_ENABLE
// #define CLFLUSH_BATCH
// #define CLFLUSH_BATCH_FENCE MFENCE
#define NUM_LANES 2
#define NUM_SLOTS 1
#endif

#ifdef HWSEC02
// #define NUM_NOPS 100
#define NUM_ACTIVATIONS 2000000
#define NUM_AGGS_PER_SET 5
// #define CLFLUSH_ENABLE
// #define CLFLUSH_BATCH
#define NUM_LANES 2
#define NUM_SLOTS 1
#endif

#ifdef HWSEC02_FST
// #define NUM_NOPS 100
#define NUM_ACTIVATIONS 2000000
#define NUM_AGGS_PER_SET 5
#define NUM_CYCLES 5
// #define CLFLUSH_ENABLE
// #define CLFLUSH_BATCH
#define NUM_LANES 2
#define NUM_SLOTS 3
#define PAT_LEN 160
#endif

#ifdef ANVIL
// #define NUM_AGGS_PER_SET 18
#define NUM_LANES 2
#define NUM_SLOTS 1
#endif

/* compile-time arguments */
#ifndef CACHE_ASSOCIATIVITY
#define CACHE_ASSOCIATIVITY 16
#endif

#define NUM_SETS 2 /* fundamental assumption */

/* NUM_AGGS_PER_SET ~ NUM_AGG_PAIRS */
#ifndef NUM_AGGS_PER_SET
#define NUM_AGGS_PER_SET 9
#endif

#define NUM_AGGS (NUM_AGGS_PER_SET * NUM_SETS)

/* length of access pattern */
#ifndef PAT_LEN
#define PAT_LEN (NUM_AGGS + NUM_EVICS)
#endif

#ifndef NUM_COAL
#define NUM_COAL 1
#endif

#ifndef NUM_ACTIVATIONS
#define NUM_ACTIVATIONS (2000000 / NUM_COAL)
#endif

#ifndef NUM_REPS
#define NUM_REPS 20
#endif

#ifndef NUM_NOPS
#define NUM_NOPS 4000
#endif

#ifndef NUM_INNER_NOPS
#define NUM_INNER_NOPS 0
#endif

#ifndef NUM_LANES
#define NUM_LANES 2
#endif

#ifndef NUM_SLOTS
#define NUM_SLOTS 3
#endif

#define NUM_DATA_PATTERNS 5

#ifndef ROW_PERM
#define ROW_PERM 0
#endif

/* convenient */
#ifdef FAST_FLIP
#define CLFLUSH_ENABLE
#define CLFLUSH_BATCH

#undef NUM_LANES
#define NUM_LANES 1

#undef NUM_NOPS
#define NUM_NOPS 0
#endif

#ifndef CLFLUSH_ENABLE /* cycles only exist in the non-clflush world */

#ifndef NUM_CYCLES
#define NUM_CYCLES (NUM_AGGS_PER_SET / NUM_SLOTS)
#endif

#define NUM_AGGS_PER_CYCLE_PER_SET NUM_SLOTS

#define NUM_AGGS_PER_CYCLE (NUM_SLOTS * NUM_SETS)

#define NUM_EVICS_PER_CYCLE_PER_SET (CACHE_ASSOCIATIVITY - NUM_SLOTS)

#define NUM_EVICS_PER_CYCLE (NUM_SETS * NUM_EVICS_PER_CYCLE_PER_SET)

#define NUM_EVICS (NUM_CYCLES * NUM_EVICS_PER_CYCLE)

#else

#define NUM_EVICS 0

#endif

#ifndef CLFLUSH_FENCE
#define CLFLUSH_FENCE SFENCE
#endif

#ifndef CLFLUSH_BATCH_FENCE
#define CLFLUSH_BATCH_FENCE MFENCE
#endif

/* NOTE: channel is part of bank */
#define MAP_NAMES "rcbsl" /* row column bank set slice */
#define NUM_MAPS 5
#define MAX_NUM_MASKS 8

typedef struct map_s {
    const char* name;
    size_t num_masks;
    uintptr_t masks[MAX_NUM_MASKS];
} map_s;

typedef struct {
    uintptr_t huge_page;
    size_t huge_page_size;

    size_t num_addrs;
    uintptr_t addrs[];
} pool_s;

typedef struct {
    double frac;
    double val;
} quant_s;

typedef struct {
    int median;
    int sample_size;

    size_t num_quants;
    quant_s quants[];
} stat_s;

void rand_bytes(unsigned char* in, size_t cnt);
int* filter(const int* in, size_t len, unsigned val);
void shuffle(int* in, size_t len);
void pretty_print(const unsigned char* in, size_t len);
void pretty_print_row(const int* in, size_t len, const char* label);
void export_to_file(const int* in, size_t len);

size_t comp_num_vic_rows(int min_row_crd, int dist);

/* global asm */
inline __attribute__((always_inline)) void clflush(const char* cache_line)
{
    asm volatile("clflush (%[cache_line])"
                 :
                 : [cache_line] "r"(cache_line)
                 : "memory");
}

/*
 * as per the GCC manual, do not modify the contents of input operands, or if
 * you do, tie them to output operands: add the + constraint to the output
 * operand or add an input operand
 */
#define asm_probe_rdpmc_pre      \
    asm volatile(                \
        "cpuid\n\t"              \
        "mov $0x0, %%rcx\n\t"    \
        "rdpmc\n\t"              \
        "mov %%rax, %[mcnt]\n\t" \
        "cpuid\n\t"              \
        : [mcnt] "+r"(mcnt)      \
        :                        \
        : "rax", "rbx", "rcx", "rdx")

#define asm_probe_rdpmc_post     \
    asm volatile(                \
        "cpuid\n\t"              \
        "mov $0x0, %%rcx\n\t"    \
        "rdpmc\n\t"              \
        "sub %[mcnt], %%rax\n\t" \
        "mov %%rax, %[mcnt]\n\t" \
        "cpuid\n\t"              \
        : [mcnt] "+r"(mcnt)      \
        :                        \
        : "rax", "rbx", "rcx", "rdx")

#define asm_probe_rdtsc_pre      \
    asm volatile(                \
        "mfence\n\t"             \
        "lfence\n\t"             \
        "rdtsc\n\t"              \
        "lfence\n\t"             \
        "mov %%rax, %[ccnt]\n\t" \
        : [ccnt] "+r"(ccnt)      \
        :                        \
        : "rax", "rdx")

/* clobbers rax and rdx */
#define asm_probe_rdtsc_post     \
    asm volatile(                \
        "mfence\n\t"             \
        "lfence\n\t"             \
        "rdtsc\n\t"              \
        "lfence\n\t"             \
        "sub %[ccnt], %%rax\n\t" \
        "mov %%rax, %[ccnt]\n\t" \
        : [ccnt] "+r"(ccnt)      \
        :                        \
        : "rax", "rdx")
