#define _GNU_SOURCE

#include "more-drama.h"
#include "access_pattern.h"
#include "debug.h"
#include "map.h"
#include "perf.h"
#include "pool.h"
#include "reverse.h"
#include "stats.h"
#include "types.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <gsl/gsl_combination.h>
#include <gsl/gsl_sort_uint.h>
#include <gsl/gsl_statistics_uint.h>
#include <limits.h>
#include <math.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

bool verbose_flag;

static void scanner(const pool_s* pool, int* crds, const char* mns, int* rows,
        size_t num_rows, bool classic)
{
    for (size_t i = 0; i < num_rows; i++) {
        uintptr_t* aggs = NULL;
        uintptr_t* vics = NULL;
        uintptr_t* evics = NULL;

        size_t num_vics = 0;

#ifdef ANVIL
        aggs = access_pattern_anvil(pool->huge_page, pool->huge_page_size,
                rows[i], &vics, &num_vics);
#else
        pool_s* cpy = pool_copy(pool);

        if (classic) {
            int agg_crds[] = {
                rows[i],
                crds[mhsh("b", mns)]
            };

            aggs = access_pattern_get_aggs_classic(&cpy, agg_crds, "rb", &vics, &num_vics);
        } else {
            int agg_crds[] = {
                rows[i],
                -1,
                crds[mhsh("b", mns)],
                crds[mhsh("s", mns)],
                crds[mhsh("l", mns)]
            };

            aggs = access_pattern_get_aggs(&cpy, agg_crds, "rcbsl", &vics, &num_vics);
        }
#endif

        if (!aggs || !vics) {
            continue;
        }

        uintptr_t* pat = aggs;

#ifndef CLFLUSH_ENABLE
        /* assembly with evics */
#ifdef ANVIL
        evics = access_pattern_get_anvil_evics(pool->huge_page,
                pool->huge_page_size, aggs);
#else
        evics = access_pattern_get_evics(&cpy, aggs);
#endif

        /* uintptr_t assembly = 0x3F; */
        /* uintptr_t assembly = 0x3; */
        /* uintptr_t assembly = 0x300C03; */
        uintptr_t assembly = 0xC00C03;

#ifdef ANVIL
        assembly = 0x3;
#endif

#ifdef HWSEC04
        assembly = 0x3;
#endif

#ifdef HWSEC02
        /* assembly = 0xC00C03; */
        assembly = 0x3;
#endif

#ifdef HWSEC02_FST
        assembly = 0xc00c03;
#endif
        pat = access_pattern_assemble(aggs, evics, assembly);
#endif

        /* print pattern */
        addrs_print_crds(pat, PAT_LEN);

        /* determine flip percentage */
        size_t num_flips = 0;

        /* seed for data pattern and waiting */
        srand(time(NULL));

        for (size_t n = 0; n < NUM_REPS; n++) {
            stderrp("%lu,%2d\n", n + 1, NUM_REPS);

            for (size_t m = 0; m < NUM_DATA_PATTERNS; m++) {

                /*
                 * if (m != 2) {
                 *     continue;
                 * }
                 */
#ifdef HWSEC02
                if (m != 0) {
                    continue;
                }
#endif

                unsigned char d = select_data_pattern(m);

                unsigned char* vic_data = vics_init(vics, num_vics, d);

                access_pattern_install(pat, PAT_LEN, NUM_LANES);

#ifdef DOUBLE_DOUBLE_LANE
                access_pattern_install(evics, NUM_EVICS, NUM_LANES);
#endif

                /* TODO: verify whether this has an effect */
                long mil = 1000000;
                long ns = (rand() % 4) * 64 * mil;
                const struct timespec ts = { .tv_sec = 0, .tv_nsec = ns };
                nanosleep(&ts, NULL);

                access_pattern_hammer(pat, evics);

                char* buf = access_pattern_caused_flips(vics, num_vics, vic_data);

                if (buf) {
                    num_flips++;
                    stderrp("%s", buf);
                }

                stderrp("%ld,%02x,%ld\n", ns / (64 * mil), d, num_flips);
            }

            stderrp("\n");
        }

        /* report statistics for further processing */
        stat_s *cycles_stat, *misses_stat, *nanos_stat;

        /* collect stats of pattern */
        access_pattern_time_reads(
                aggs,
                evics,
                &cycles_stat,
                &misses_stat,
                &nanos_stat);

        rowp("percentage of flips", "%*.2f\n", COLUMN_WIDTH,
                100 * (float)num_flips / NUM_REPS);
        rowp("cycles (median)", "%*.2f\n", COLUMN_WIDTH, (float)cycles_stat->median);
        rowp("misses (median)", "%*.2f\n", COLUMN_WIDTH, (float)misses_stat->median);
        rowp("nanos (median)", "%*.2f\n", COLUMN_WIDTH, (float)nanos_stat->median);
        rowp("tREFI fit", "%*.2f\n", COLUMN_WIDTH, tREFI / (float)nanos_stat->median);
    }
}

/* NOTE: this code is not freeing anything at the moment */
int main(int argc, char* argv[])
{
    /* TODO: do we really need to be sudo if we run with just -n? */
    /* check assumptions about architecture */
    assertd(sizeof(unsigned) == sizeof(uint32_t), "integer width mismatch");

    /* TODO: change these based on new types.h */
    infop("compile-time arguments (set directly or default values)\n");
    rowp("CACHE_ASSOCIATIVITY", "%*u\n", COLUMN_WIDTH, CACHE_ASSOCIATIVITY);
    rowp("NUM_SETS", "%*u\n", COLUMN_WIDTH, NUM_SETS);
    blanklinep();

    rowp("NUM_AGGS_PER_SET", "%*u\n", COLUMN_WIDTH, NUM_AGGS_PER_SET);
    rowp("NUM_AGGS", "%*u\n", COLUMN_WIDTH, NUM_AGGS);
    rowp("PAT_LEN", "%*u\n", COLUMN_WIDTH, PAT_LEN);
    blanklinep();

    rowp("NUM_COAL", "%*u\n", COLUMN_WIDTH, NUM_COAL);
    rowp("NUM_ACTIVATIONS", "%*u\n", COLUMN_WIDTH, NUM_ACTIVATIONS);
    rowp("NUM_REPS", "%*u\n", COLUMN_WIDTH, NUM_REPS);
    rowp("NUM_NOPS", "%*u\n", COLUMN_WIDTH, NUM_NOPS);
    rowp("NUM_INNER_NOPS", "%*u\n", COLUMN_WIDTH, NUM_INNER_NOPS);
    rowp("NUM_LANES", "%*u\n", COLUMN_WIDTH, NUM_LANES);
    rowp("NUM_SLOTS", "%*u\n", COLUMN_WIDTH, NUM_SLOTS);
    rowp("ROW_PERM", "%*u\n", COLUMN_WIDTH, ROW_PERM);
    blanklinep();

    bool clflush_enable = false;
#ifdef CLFLUSH_ENABLE
    clflush_enable = true;
#else
    /* no longer required with support for more W' or M */
    /* assert(NUM_AGGS_PER_SET % NUM_SLOTS == 0); */
    assert(NUM_CYCLES * (NUM_AGGS_PER_CYCLE + NUM_EVICS_PER_CYCLE) == PAT_LEN);

    infop("compile-time constants based on compile-time arguments "
            "(set indirectly)\n");
    rowp("NUM_SLOTS", "%*u\n", COLUMN_WIDTH, NUM_SLOTS);
    rowp("NUM_CYCLES", "%*u\n", COLUMN_WIDTH, NUM_CYCLES);
    blanklinep();

    rowp("NUM_AGGS_PER_CYCLE_PER_SET", "%*u\n", COLUMN_WIDTH, NUM_AGGS_PER_CYCLE_PER_SET);
    rowp("NUM_AGGS_PER_CYCLE", "%*u\n", COLUMN_WIDTH, NUM_AGGS_PER_CYCLE);
    blanklinep();

    rowp("NUM_EVICS_PER_CYCLE_PER_SET", "%*u\n",
            COLUMN_WIDTH, NUM_EVICS_PER_CYCLE_PER_SET);
    rowp("NUM_EVICS_PER_CYCLE", "%*u\n", COLUMN_WIDTH, NUM_EVICS_PER_CYCLE);
    rowp("NUM_EVICS", "%*u\n", COLUMN_WIDTH, NUM_EVICS);
    blanklinep();
#endif

    rowp("CLFLUSH_ENABLE", "%*s\n", COLUMN_WIDTH, clflush_enable ? "yes" : "no");

    bool clflush_batch = false;
#ifdef CLFLUSH_BATCH
    clflush_batch = true;
#endif
    rowp("CLFLUSH_BATCH", "%*s\n", COLUMN_WIDTH, clflush_batch ? "yes" : "no");
    rowp("CLFLUSH_FENCE", "%*u\n", COLUMN_WIDTH, CLFLUSH_FENCE);
    rowp("CLFLUSH_BATCH_FENCE", "%*u\n", COLUMN_WIDTH, CLFLUSH_BATCH_FENCE);
    blanklinep();

    /* seed random number generator */
    /* srand(target_row); */
    time_t t = time(NULL);
    /* Evaluation seed */
    /* t = 1595580533; */
    /* stable ANVIL */
    /* t = 3905475199; */
    /* HWSEC02 seed */
    t = 1602509940;
    rowp("SEED", "%*ld\n", COLUMN_WIDTH, t);
    srand(t);

    blanklinep();

    /* parse command-line arguments */
    verbose_flag = false;
    bool use_classic_pattern = false;
    bool find_cache_thrs_flag = false;
    bool cpuid_arch_perf_flag = false;
    bool find_bank_thrs_flag = false;
    bool find_row_and_column_bits_flag = false;

    int target_row = 0;

    char* hostname = NULL;

    int c;
    while ((c = getopt(argc, argv, "vpcbrln:t:")) != -1) {
        switch (c) {
            case 'v':
                verbose_flag = true;
                break;

            case 'p':
                cpuid_arch_perf_flag = true;
                break;

            case 'c':
                find_cache_thrs_flag = true;
                break;

            case 'b':
                find_bank_thrs_flag = true;
                break;

            case 'r':
                find_bank_thrs_flag = true;
                find_row_and_column_bits_flag = true;
                break;

            case 'l':
                use_classic_pattern = true;
                break;

            case 'n':
                infop("hostname is %s\n", optarg);
                assert(asprintf(&hostname, "%s", optarg) != -1);
                break;

            case 't':
                target_row = atoi(optarg);
                break;

            case '?':
                if (optopt == 'n') {
                    errord("please specify the hostname through -n\n");
                } else {
                    errord("unknown option\n");
                }

            default:
                /* case ':'? */
                errord("parsing command-line arguments failed miserably\n");
        }
    }

    /* query for info about performance counters */
    if (cpuid_arch_perf_flag) {
        cpuid_arch_perf();
        return EXIT_SUCCESS;
    }

    /* find cache threshold */
    if (find_cache_thrs_flag) {
        find_cache_thrs();
        return EXIT_SUCCESS;
    }

    /* open file and create it if it does not exits */
    int fd = open(PATH, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

    if (fd == -1) {
        failured_lib();
    }

    /* create mapping */
    size_t huge_page_size = 1 * GB;
    char* huge_page = mmap(
            (char*)0x7f2300000000,
            huge_page_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_HUGETLB | MAP_HUGE_1GB | MAP_POPULATE | MAP_FIXED,
            fd,
            0);

    if (huge_page == MAP_FAILED) {
        failured_lib();
    } else {
        successd_lib(
                "mapped 1 GB at %p-%p\n", huge_page, huge_page + huge_page_size);
    }

    /* makes measurements much faster? do we really need it? */
    cpu_set_t set;
    cpu_set_t another_set;
    CPU_ZERO(&set);
    /* NOTE: if you change this, check performance counters */
    int cpu = 2;
    CPU_SET(cpu, &set);
    sched_setaffinity(0, sizeof(cpu_set_t), &set);

    sched_getaffinity(0, sizeof(cpu_set_t), &another_set);
    for (size_t i = 0; i < 8; i++) {
        if (CPU_ISSET(i, &another_set)) {
            infop("running on CPU %lu\n", i);
        }
    }

    if (find_bank_thrs_flag) {
        int bank_thrs = find_bank_thrs((uintptr_t)huge_page, huge_page_size);

        if (find_row_and_column_bits_flag) {
            assertd(hostname, "-r requires -n for bank.conf\n");

            /* requires bank.conf from find_bank_thrs */
            maps_from_files(
                    hostname, (const char* []){ "bank.conf" }, 1, " ", "b");

            find_row_and_column_bits((uintptr_t)huge_page, huge_page_size, bank_thrs);
        }

        return EXIT_SUCCESS;
    }

    maps_from_files(hostname,
            (const char* []){
            "row.conf",
            "column.conf",
            "bank.conf",
            "set.conf",
            "slice.conf" },
            5,
            " ",
            "rcbsl");

    /* NOTE: pin set? */
    /* int crds[] = { 2, 835, 5 }; */
    /* int crds[] = { 3, -1, -1 }; */
    /* int crds[] = { 59, -1, -1 }; */

    /* HWSEC02 */
    /* int crds[] = { 2, 835, 5 }; */

    /* in both cases: int crds[] = { 8, -1, 6 }; */
    /* target 138 on hwsec04 */
    /* target 5131 on i7-5775C (or 340) */
    /* for (size_t i = 0; i < 64; i++) { */
    /* int crds[] = { i, -1, -1 }; */
    int crds[] = { 0, -1, 0 };

    /* size_t num_rows = 1024; */

    size_t num_rows = 1;
    int rows[num_rows];

    for (size_t i = 0; i < num_rows; i++) {
        rows[i] = target_row + i;
    }

    pool_s* pool = pool_new_with_crds((uintptr_t)huge_page, huge_page_size, crds, "bsl");

    if (!use_classic_pattern) {
        pool_special_extend(&pool);
    }

    scanner(pool, crds, "bsl", rows, num_rows, use_classic_pattern);
    /* } */

    if (remove(PATH) != 0) {
        failured_lib();
    }
}
