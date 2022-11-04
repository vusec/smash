#include "access_pattern.h"
#include "debug.h"
#include "map.h"
#include "more-drama.h"
#include "perf.h"
#include "pool.h"
#include "stats.h"

#include <gsl/gsl_sort_uint.h>
#include <string.h>
#include <time.h>

/* check for adjacent cache-line prefetch */
static bool no_prefetch_interference(uintptr_t* aggs)
{
    for (size_t i = 0; i < NUM_AGGS - 1; i++) {
        intptr_t addr = aggs[i] - (aggs[i] % CACHE_LINE_SIZE);
        intptr_t bddr = aggs[i + 1] - (aggs[i + 1] % CACHE_LINE_SIZE);

        if (bddr >= addr && bddr - addr < CACHE_LINE_SIZE) {
            return false;
        }

        if (addr > bddr && addr - bddr < CACHE_LINE_SIZE) {
            return false;
        }
    }

    return true;
}

static size_t add_row(uintptr_t base, int* crds, const char* mns, uintptr_t* addrs)
{
    assert(!strncmp(mns, "rcb", strlen("rcb")));

    /* set row */
    assert(addr_set_crd(base, crds[0], "r", &base, 1) == 1);

    for (uintptr_t c = 0; c < get_num_cols(); c++) {
        uintptr_t addr = 0;

        /* set column */
        assert(addr_set_crd(base, c, "c", &addr, 1) == 1);

        /* set bank, while preserving row and column */
        addrs[c] = addr_set_crd_prs(addr, crds[mhsh("b", mns)], "b", "rc");

        assert(addrs[c]);
    }

    return get_num_cols();
}

uintptr_t* access_pattern_anvil(uintptr_t huge_page, size_t huge_page_size,
    int row, uintptr_t** vics, size_t* num_vics)
{
    assert(NUM_AGGS_PER_SET % 2 == 0);
    size_t num_ds_pairs = NUM_AGGS_PER_SET / 2;
    /* size_t num_aux_pairs = NUM_AGGS_PER_SET / 2; */

    /* aggressor pool */
    char* mns = "rcbsl";
    int agg_crds[] = { -1, -1, 8, -1, -1 };
    pool_s* agg_pool = pool_new_with_crds((uintptr_t)huge_page,
        huge_page_size, agg_crds, mns);
    pool_special_extend(&agg_pool);

    /* victims */
    int vic_crds[] = { -1, -1, agg_crds[mhsh("b", mns)], -1, -1 };

    /* auxiliaries */
    char* aux_mns = "sl";
    int aux_crds[] = { -1, -1 };
    pool_s* aux_pool = NULL;

    size_t new_num_vics = num_ds_pairs * (3 + 3) * get_num_cols();
    uintptr_t* aggs = calloc(NUM_AGGS, sizeof(uintptr_t));
    uintptr_t* new_vics = calloc(new_num_vics, sizeof(uintptr_t));

    size_t v = 0;
    for (size_t i = 0; i < NUM_AGGS; i += 4) {
        if (i > 0) {
            agg_crds[mhsh("r", mns)] = -1; /* random row */
        } else {
            agg_crds[mhsh("r", mns)] = row;
        }

        /* low aggressor */
        aggs[i] = pool_pop_rand_addr(&agg_pool, agg_crds, mns);
        agg_crds[mhsh("r", mns)] = addr_get_crd(aggs[i], "r");
        agg_crds[mhsh("s", mns)] = addr_get_crd(aggs[i], "s");
        agg_crds[mhsh("l", mns)] = addr_get_crd(aggs[i], "l");

        /* low/middle victim */
        vic_crds[0] = agg_crds[0] - 1;
        v += add_row(agg_pool->huge_page, vic_crds, mns, new_vics + v);

        /* also write to aggressor row */
        vic_crds[0] = agg_crds[0];
        v += add_row(agg_pool->huge_page, vic_crds, mns, new_vics + v);

        /* middle/high victim */
        vic_crds[0] = agg_crds[0] + 1;
        v += add_row(agg_pool->huge_page, vic_crds, mns, new_vics + v);

        /* auxiliary low aggressor */
        aux_crds[mhsh("s", aux_mns)] = agg_crds[mhsh("s", mns)];
        aux_crds[mhsh("l", aux_mns)] = agg_crds[mhsh("l", mns)];
        aux_pool = pool_new_with_crds((uintptr_t)huge_page, huge_page_size,
            aux_crds, aux_mns);
        aggs[i + 2] = pool_pop_rand_addr(&aux_pool, aux_crds, aux_mns);
        pool_free(aux_pool);

        /* second aggressor (two rows after first aggressor) */
        agg_crds[mhsh("r", mns)] += 2;
        agg_crds[mhsh("s", mns)] = -1;
        agg_crds[mhsh("l", mns)] = -1;
        aggs[i + 1] = pool_pop_rand_addr(&agg_pool, agg_crds, mns);

        int sign = 1;
        if (!aggs[i + 1]) {
            agg_crds[mhsh("r", mns)] -= 4;
            aggs[i + 1] = pool_pop_rand_addr(&agg_pool, agg_crds, mns);
            sign = -1;
        }

        /* also write to aggressor row */
        vic_crds[0] = agg_crds[0];
        v += add_row(agg_pool->huge_page, vic_crds, mns, new_vics + v);

        /* low/high victim */
        vic_crds[0] = agg_crds[0] + sign * 1;
        v += add_row(agg_pool->huge_page, vic_crds, mns, new_vics + v);

        /* dummy aggressor row to simplify things (even number) */
        vic_crds[0] = agg_crds[0] + sign * 2;
        v += add_row(agg_pool->huge_page, vic_crds, mns, new_vics + v);

        /* auxiliary high aggressor */
        aux_crds[mhsh("s", aux_mns)] = addr_get_crd(aggs[i + 1], "s");
        aux_crds[mhsh("l", aux_mns)] = addr_get_crd(aggs[i + 1], "l");
        aux_pool = pool_new_with_crds((uintptr_t)huge_page, huge_page_size,
            aux_crds, aux_mns);
        aggs[i + 3] = pool_pop_rand_addr(&aux_pool, aux_crds, aux_mns);
        pool_free(aux_pool);

        if (verbose_flag) {
            addrs_print_crds(aggs + i, 4);
        }
    }

    *vics = new_vics;
    *num_vics = new_num_vics;

    return aggs;
}

uintptr_t* access_pattern_get_aggs_classic(
    pool_s** pool, int* agg_crds, const char* mns, uintptr_t** vics, size_t* num_vics)
{
    assert(!strncmp(mns, "rb", strlen("rb")));

    size_t new_num_vics = (NUM_AGGS + 1) * get_num_cols();
    uintptr_t* aggs = calloc(NUM_AGGS, sizeof(uintptr_t));
    uintptr_t* new_vics = calloc(new_num_vics, sizeof(uintptr_t));
    int vic_crds[] = { -1, -1, agg_crds[mhsh("b", mns)] };

    size_t v = 0;

    /* TODO: think of edges... */
    vic_crds[0] = agg_crds[0] - 1;
    v += add_row((*pool)->huge_page, vic_crds, "rcb", new_vics + v);

    for (size_t i = 0; i < NUM_AGGS; i++) {
        aggs[i] = pool_pop_rand_addr(pool, agg_crds, mns);

        if (!aggs[i]) {
            free(aggs);
            return NULL;
        } else {
            vic_crds[0] = agg_crds[0] + 1;
            v += add_row((*pool)->huge_page, vic_crds, "rcb", new_vics + v);

            agg_crds[0] += 2;
        }
    }

    assertd(v == new_num_vics, "%lu,%lu\n", v, new_num_vics);
    assert(no_prefetch_interference(aggs));

    *vics = new_vics;
    *num_vics = new_num_vics;

    return aggs;
}

/*
 * NOTE: this procedure makes assumptions about the order of addresses in
 * the pool, as done by pool_special_extend() 
 */
uintptr_t* access_pattern_get_aggs(
    pool_s** pool, int* agg_crds, const char* mns, uintptr_t** vics, size_t* num_vics)
{
    assert(NUM_AGGS <= (*pool)->num_addrs);
    assert(!strncmp(mns, "rcbsl", strlen("rcbsl")));

    size_t new_num_vics = NUM_AGGS_PER_SET * 2 * 3 * get_num_cols();
    uintptr_t* aggs = calloc(NUM_AGGS, sizeof(uintptr_t));
    uintptr_t* new_vics = calloc(new_num_vics, sizeof(uintptr_t));

    int vic_crds[] = { -1, -1, agg_crds[mhsh("b", mns)] };
    int other_agg_crds[] = { -1, -1, agg_crds[mhsh("b", mns)], -1, -1 };

    size_t v = 0;
    for (size_t a = 0; a < NUM_AGGS; a += 2) {
        if (a > 0) {
            agg_crds[mhsh("r", mns)] = -1; /* random row */
        }

        /* first aggressor (at random row) */
        aggs[a] = pool_pop_rand_addr(pool, agg_crds, mns);

        /* could not find row? */
        if (!aggs[a]) {
            return NULL;
        }

        /* find out row we chose (needed by other aggressor and victims) */
        agg_crds[mhsh("r", mns)] = addr_get_crd(aggs[a], "r");

        if (a == 0) {
            /* make sure we stick to one set-slice combination */
            agg_crds[mhsh("s", mns)] = addr_get_crd(aggs[a], "s");
            agg_crds[mhsh("l", mns)] = addr_get_crd(aggs[a], "l");
            other_agg_crds[mhsh("l", mns)] = addr_get_crd(aggs[a], "l");
        }

        /* NOTE: using 0 here instead of mhsh, dangerous */
        /* low/middle victim */
        vic_crds[0] = agg_crds[0] - 1;
        v += add_row((*pool)->huge_page, vic_crds, mns, new_vics + v);

        /* also write to aggressor row */
        vic_crds[0] = agg_crds[0];
        v += add_row((*pool)->huge_page, vic_crds, mns, new_vics + v);

        /* middle/high victim */
        vic_crds[0] = agg_crds[0] + 1;
        v += add_row((*pool)->huge_page, vic_crds, mns, new_vics + v);

        /* second aggressor (two rows after first aggressor) */
        other_agg_crds[mhsh("r", mns)] = agg_crds[mhsh("r", mns)] + 2;

        /* uncomment if you want only a single double-sided pair */
        /*
         * if (a == 0) {
         *     other_agg_crds[mhsh("r", mns)] = agg_crds[mhsh("r", mns)] + 2;
         * } else {
         *     other_agg_crds[mhsh("r", mns)] = -1;
         * }
         */

        aggs[a + 1] = pool_pop_rand_addr(pool, other_agg_crds, mns);

        int sign = 1;
        if (!aggs[a + 1]) {
            other_agg_crds[mhsh("r", mns)] = agg_crds[mhsh("r", mns)] - 2;
            aggs[a + 1] = pool_pop_rand_addr(pool, other_agg_crds, mns);
            sign = -1;
        }

        if (!aggs[a + 1]) {
            return NULL;
        }

        if (a == 0) {
            other_agg_crds[mhsh("s", mns)] = addr_get_crd(aggs[a + 1], "s");
            /* other_agg_crds[mhsh("l", mns)] = addr_get_crd(aggs[a + 1], "l"); */
        }

        /* also write to aggressor row */
        vic_crds[0] = other_agg_crds[0];
        v += add_row((*pool)->huge_page, vic_crds, mns, new_vics + v);

        /* low/high victim */
        vic_crds[0] = other_agg_crds[0] + sign * 1;
        v += add_row((*pool)->huge_page, vic_crds, mns, new_vics + v);

        /* dummy aggressor row */
        vic_crds[0] = other_agg_crds[0] + sign * 2;
        v += add_row((*pool)->huge_page, vic_crds, mns, new_vics + v);

        if (verbose_flag) {
            addrs_print_crds(aggs + a, 2);
        }
    }

    assertd(v == new_num_vics, "%lu,%lu\n", v, new_num_vics);
    assert(no_prefetch_interference(aggs));

    *vics = new_vics;
    *num_vics = new_num_vics;

    return aggs;
}

unsigned char select_data_pattern(size_t seed)
{
    unsigned char d;

    switch (seed % NUM_DATA_PATTERNS) {
    case 0:
        d = 0xff;
        break;
    case 1:
        d = 0x55;
        break;
    case 2:
        d = 0xaa;
        break;
    case 3:
        d = 0x00;
        break;
    case 4:
        d = rand() % 256;
        break;
    }

    return d;
}

unsigned char* vics_init(uintptr_t* vics, size_t num_vics, unsigned char d)
{
    assert(num_vics % 2 == 0);

    unsigned char* data = calloc(num_vics, sizeof(unsigned char));

    /* write to data */
    for (size_t i = 0; i < num_vics; i++) {
        if ((i / get_num_cols()) % 2 == 1) {
            data[i] = d ^ 0xff; /* aggressor */
        } else {
            data[i] = d; /* victim */
        }
    }

    /* copy data to vics */
    for (size_t i = 0; i < num_vics; i++) {
        *(unsigned char*)vics[i] = data[i];
    }

    return data;
}

char* access_pattern_caused_flips(
    uintptr_t* vics, size_t num_vics, unsigned char* vic_data)
{
    int buf_size = 128;
    char* buf = calloc(buf_size, sizeof(char));

    assert(num_vics % 2 == 0);

    for (size_t i = 0; i < num_vics; i++) {
        if ((i / get_num_cols()) % 2 == 1) {
            continue;
        }

        unsigned char before = vic_data[i];
        unsigned char after = *(unsigned char*)vics[i];

        if (before != after) {

            /* 1 -> 0 or 1 -> 0? */
            unsigned char kind = 0x10;

            if (after > before) {
                kind = 0x1;
            }

            char* tmp;
            assert(asprintf(&tmp, "%d,%d,%d %02x -> %02x %02x\n",
                       addr_get_crd(vics[i], "r"),
                       addr_get_crd(vics[i], "c"),
                       addr_get_crd(vics[i], "b"),
                       before,
                       after,
                       kind)
                != -1);
            size_t len = strlen(tmp);

            if (len > buf_size) {
                infop("Too many flips!?\n");
                break;
            } else {
                strncat(buf, tmp, buf_size);
                buf_size -= len;
            }
        }
    }

    if (strlen(buf) > 0) {
        return buf;
    } else {
        free(buf);
        return NULL;
    }
}

#ifndef CLFLUSH_ENABLE
uintptr_t* access_pattern_assemble(
    uintptr_t* aggs, uintptr_t* evics, uintptr_t assembly)
{
    uintptr_t* pat = calloc(PAT_LEN, sizeof(uintptr_t));

    size_t a = 0;
    size_t e = 0;

    for (size_t i = 0; i < NUM_CYCLES; i++) {
        for (size_t j = 0; j < NUM_AGGS_PER_CYCLE + NUM_EVICS_PER_CYCLE; j++) {
            size_t k = i * (NUM_AGGS_PER_CYCLE + NUM_EVICS_PER_CYCLE) + j;

            /* one means aggressor, zero means evic */
            if (0x1 & (assembly >> j)) {
                pat[k] = aggs[a];
                a = (a + 1) % NUM_AGGS;
            } else {
                pat[k] = evics[e];
                e++;
            }

            /* support for arbitrary W' or M */
            pat[k] += i * sizeof(pat[k]);
        }
    }

    infop("a %lu %d e %lu %d\n", a, NUM_AGGS, e, NUM_EVICS);
    assert(a == 0);
    assert(e == NUM_EVICS);

    return pat;
}
#endif

#ifndef CLFLUSH_ENABLE
uintptr_t* access_pattern_get_evics(pool_s** in, uintptr_t* aggs)
{
    uintptr_t* evics = calloc(NUM_EVICS, sizeof(uintptr_t));

    for (size_t i = 0; i < NUM_EVICS; i++) {

        int crds[] = {
            addr_get_crd(aggs[i % NUM_SETS], "s"),
            addr_get_crd(aggs[i % NUM_SETS], "l")
        };

        if (i < NUM_EVICS_PER_CYCLE) {
            evics[i] = pool_pop_rand_addr(in, crds, "sl");
        } else {
            /*
             * changing cache line offsets (see '+ sizeof(uinptr_t)') to
             * produce different addresses that will behave identically
             * both in terms of caches and DRAM
             */
            /* we now do this during assembly, for all both aggs and evics */
            /* evics[i] = evics[i - NUM_EVICS_PER_CYCLE] + sizeof(uintptr_t); */
            evics[i] = evics[i - NUM_EVICS_PER_CYCLE];
        }
    }

    return evics;
}

uintptr_t* access_pattern_get_anvil_evics(uintptr_t huge_page,
    size_t huge_page_size, uintptr_t* aggs)
{
    uintptr_t* evics = calloc(NUM_EVICS, sizeof(uintptr_t));

    for (size_t i = 0; i < NUM_EVICS; i += 2 * NUM_EVICS_PER_CYCLE) {
        for (size_t j = 0; j < 2; j++) {

            int crds[] = {
                addr_get_crd(aggs[i / (2 * NUM_EVICS_PER_CYCLE) * 4 + j], "s"),
                addr_get_crd(aggs[i / (2 * NUM_EVICS_PER_CYCLE) * 4 + j], "l")
            };

            pool_s* pool = pool_new_with_crds(huge_page, huge_page_size, crds, "sl");

            for (size_t k = 0; k < NUM_EVICS_PER_CYCLE; k += 2) {
                evics[i + j + k] = pool_pop_rand_addr(&pool, crds, "sl");
            }

            pool_free(pool);
        }

        for (size_t k = 0; k < NUM_EVICS_PER_CYCLE; k++) {
            evics[i + NUM_EVICS_PER_CYCLE + k] = evics[i + k]
                + sizeof(uintptr_t);
        }
    }

    addrs_print_crds(evics, NUM_EVICS);

    return evics;
}
#endif

void access_pattern_install(uintptr_t* aggs, size_t num_aggs, size_t num_lanes)
{
    assert(num_aggs % num_lanes == 0);

    for (size_t i = 0; i < num_aggs; i++) {
        *((uintptr_t*)aggs[i]) = aggs[(i + num_lanes) % num_aggs];
    }

    /* check if installed successfully: do the actual pointer chasing */
    for (size_t l = 0; l < num_lanes; l++) {
        uintptr_t head = *(uintptr_t*)aggs[l];

        for (size_t i = 1; i < num_aggs; i++) {
            assert(head == aggs[(l + i * num_lanes) % num_aggs]);
            head = *(uintptr_t*)head;
        }
    }
}

void access_pattern_time_reads(
    uintptr_t* aggs, uintptr_t* evics, stat_s** cycles, stat_s** misses, stat_s** nanos)
{
    int ccnts[EXP_NUM_REPS] = { 0 };
    int mcnts[EXP_NUM_REPS] = { 0 };
    int ncnts[EXP_NUM_REPS] = { 0 };

    struct timespec ts;

    counter_enable(IA32_PERFEVTSEL0, IA32_PMC0, ENABLE_LLC_MISSES,
        DISABLE_LLC_MISSES);

    /* from TRRespass */
    sched_yield();

    for (size_t i = 0; i < EXP_NUM_REPS; i++) {
        /* must be unsigned! */
        unsigned long ccnt = 0;
        unsigned long mcnt = 0;
        long ncnt = ts.tv_nsec;

        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        asm_probe_rdpmc_pre;
        asm_probe_rdtsc_pre;

        for (size_t l = 0; l < EXP_NUM_SUB_REPS; l++) {
            access_pattern_core(aggs, evics);
        }

        asm_probe_rdtsc_post;
        asm_probe_rdpmc_post;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

        /* NOTE: integer division */
        ccnts[i] = ccnt / EXP_NUM_SUB_REPS;
        mcnts[i] = mcnt / EXP_NUM_SUB_REPS;
        ncnts[i] = (ts.tv_nsec - ncnt) / EXP_NUM_SUB_REPS;
    }

    counter_disable(IA32_PERFEVTSEL0, IA32_PMC0, DISABLE_LLC_MISSES);

    /* TODO: move this (or just the printing) out of here */
    hist(ccnts, EXP_NUM_REPS, EXP_CYCLES_HIST_GRAN);
    *cycles = stat_new(ccnts, EXP_NUM_REPS, EXP_NUM_QUANTS);
    stat_print(*cycles, "Time-Stamp Counter (Delta)", false);

    hist(mcnts, EXP_NUM_REPS, EXP_MISSES_HIST_GRAN);
    *misses = stat_new(mcnts, EXP_NUM_REPS, EXP_NUM_QUANTS);
    stat_print(*misses, "Last Level Cache Misses", false);

    hist(ncnts, EXP_NUM_REPS, EXP_NANOS_HIST_GRAN);
    *nanos = stat_new(ncnts, EXP_NUM_REPS, EXP_NUM_QUANTS);
    stat_print(*nanos, "CLOCK_MONOTONIC_RAW (Delta)", false);
}

stat_s* access_pattern_time_read_pair(
    uintptr_t* aggs, size_t num_reps, bool post_flush)
{
    int* cnts = calloc(num_reps, sizeof(int));
    unsigned long ccnt = 0;

    for (size_t i = 0; i < num_reps; i++) {
        asm volatile(
            "mfence\n\t" /* stores must be visible, e.g., clflush */
            "lfence\n\t" /* loads must be visible, e.g., mov */
            "rdtsc\n\t"
            "lfence\n\t" /* execute rdtsc prior to any other instruction */
            "mov %%rax, %%rbx\n\t"

            "mov (%[agg]), %%rcx\n\t"
            "mov (%%rcx), %%rcx\n\t"

            "lfence\n\t" /* loads must be visible, e.g., mov */
            "rdtsc\n\t"
            "lfence\n\t"
            "sub %%rbx, %%rax\n\t"
            : "+a"(ccnt)
            : [agg] "r"(aggs[0])
            : "rbx", "rcx", "rdx");

        if (post_flush) {
            asm volatile("clflush (%0)\n\t"
                         "clflush (%1)\n\t"
                         :
                         : "r"(aggs[0]), "r"(aggs[1])
                         : "memory");
        }

        cnts[i] = ccnt;
    }

    size_t num_quants = 100;
    stat_s* cycles = stat_new(cnts, num_reps, num_quants);

    return cycles;
}
