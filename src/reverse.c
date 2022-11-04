#include "reverse.h"
#include "access_pattern.h"
#include "debug.h"
#include "map.h"
#include "more-drama.h"
#include "pool.h"
#include "stats.h"

#include <assert.h>
#include <gsl/gsl_combination.h>
#include <string.h>

static int estimate_thrs(const stat_s* lower, const stat_s* upper)
{
    int u = upper->quants[0].val;
    int l = lower->quants[(lower->num_quants - 2)].val;

    assertd(l < u, "quantiles overlap\n");

    return ((u + l + 1) / 2);
}

static int ask_for_thrs(void)
{
    stderrp("threshold? ");
    int bank_thrs = 0;
    assert(scanf("%d", &bank_thrs) == 1);
    return bank_thrs;
}

int find_cache_thrs(void)
{
    assertd(CACHE_THLD_NUM_REPS <= (1 << 20), "too many rounds");
    int cycles[CACHE_THLD_NUM_REPS];

    size_t num_pages = 1;
    char* buf = map_large_buf(num_pages);

    /* reload */
    time_read_single(buf); /* make sure it's cached */
    for (size_t r = 0; r < CACHE_THLD_NUM_REPS; r++) {
        cycles[r] = time_read_single(buf);
    }

    /* pretty_print(cycles, CACHE_THLD_NUM_REPS); */

    infop("stats for RELOAD (cache hits)\n");
    hist(cycles, CACHE_THLD_NUM_REPS, CACHE_THLD_HIST_GRAN);
    size_t reload_num_quants = 100;
    stat_s* reload_stat = stat_new(
        cycles, CACHE_THLD_NUM_REPS, reload_num_quants);
    stat_print(reload_stat, NULL, false);
    blanklinep();

    /* although we are about to overwrite, just to be sure */
    memset(cycles, 0, sizeof(int) * CACHE_THLD_NUM_REPS);

    /* flush+reload */
    for (size_t r = 0; r < CACHE_THLD_NUM_REPS; r++) {
        clflush(buf);
        cycles[r] = time_read_single(buf);
    }

    infop("stats for FLUSH+RELOAD (cache misses)\n");
    hist(cycles, CACHE_THLD_NUM_REPS, CACHE_THLD_HIST_GRAN);
    size_t flush_reload_num_quants = 100;
    stat_s* flush_reload_stat = stat_new(
        cycles, CACHE_THLD_NUM_REPS, flush_reload_num_quants);
    stat_print(flush_reload_stat, NULL, false);
    blanklinep();

    /* estimating threshold */
    int cache_thrs = estimate_thrs(reload_stat, flush_reload_stat);

    rowp("cache threshold (in cycles)", "%*d\n", COLUMN_WIDTH, cache_thrs);

    unmap_large_buf(buf, num_pages);

    return cache_thrs;
}

static int gsl_combination_eor(
    gsl_combination* in, int* offsets, uintptr_t addr)
{
    int eor = 0;
    for (size_t i = 0; i < in->k; i++) {
        eor ^= 0x1 & (addr >> offsets[in->data[i]]);
    }
    return eor;
}

/* does not work very well on my own machine? */
int find_bank_thrs(uintptr_t huge_page, size_t huge_page_size)
{
    assertd(huge_page_size == GB, "requires 1 GB huge page\n");

    pool_s* large_pool = pool_new_rand(
        huge_page, huge_page_size, BANK_THLD_SIZE_LARGE_PL);
    pool_s* small_pool = pool_new_rand(
        huge_page, huge_page_size, BANK_THLD_SIZE_SMALL_PL);

    int medians_unsorted[BANK_THLD_SIZE_LARGE_PL];
    uintptr_t addr_pair[2];

    for (size_t i = 0; i < BANK_THLD_SIZE_LARGE_PL; i++) {
        addr_pair[0] = small_pool->addrs[i % small_pool->num_addrs];
        addr_pair[1] = large_pool->addrs[i];

        access_pattern_install(addr_pair, 2, 1);

        stat_s* cycles_stat = access_pattern_time_read_pair(
            addr_pair, BANK_THLD_NUM_REPS, true);

        medians_unsorted[i] = cycles_stat->median;
    }

    infop("stats for BANK CONFLICTS\n");
    size_t num_quants = 100;

    int medians_sorted[BANK_THLD_SIZE_LARGE_PL];
    memcpy(medians_sorted, medians_unsorted, sizeof(int) * BANK_THLD_SIZE_LARGE_PL);

    hist(medians_sorted, BANK_THLD_SIZE_SMALL_PL, BANK_THLD_HIST_GRAN);
    stat_s* medians_stat = stat_new(
        medians_sorted, BANK_THLD_SIZE_LARGE_PL, num_quants);
    stat_print(medians_stat, NULL, false);
    blanklinep();

    int bank_thrs = ask_for_thrs();

    rowp("bank conflict threshold (in cycles)", "%*d\n", COLUMN_WIDTH,
        bank_thrs);

    /* II. find all pairs that belong to the same bank */
    uintptr_t addr_to_flip = 0;
    for (size_t i = 0; i < BANK_THLD_SIZE_LARGE_PL; i++) {
        if (medians_unsorted[i] > bank_thrs) {
            addr_pair[0] = small_pool->addrs[i % BANK_THLD_SIZE_SMALL_PL];
            addr_to_flip = large_pool->addrs[i];

            rowp("bank conflict pair", "%*d%*p%*p\n", COLUMN_WIDTH,
                medians_unsorted[i], COLUMN_WIDTH,
                (char*)addr_pair[0], COLUMN_WIDTH,
                (char*)addr_to_flip);

            break;
        }
    }

    int sign_bits[HUGE_1GB_OFFSET_SIZE] = { 0 };

    for (size_t b = 0; b < HUGE_1GB_OFFSET_SIZE; b++) {
        addr_pair[1] = addr_to_flip ^ (0x1 << b);

        /* make a wrapper for this part? */
        access_pattern_install(addr_pair, 2, 1);
        stat_s* cycles_stat = access_pattern_time_read_pair(
            addr_pair, BANK_THLD_NUM_REPS, true);

        /* bit moves address out of bank, means it's significant */
        if (cycles_stat->median < bank_thrs) {
            stderrp("%lu %d\n", b, cycles_stat->median);
            sign_bits[b] = 1;
        }
    }

    /* print significant offsets */;
    int offsets[HUGE_1GB_OFFSET_SIZE] = { 0 };

    size_t num_offsets = 0;
    for (size_t i = 0; i < HUGE_1GB_OFFSET_SIZE; i++) {
        if (sign_bits[i] == 1) {
            offsets[num_offsets] = i;
            num_offsets++;
        }
    }

    pretty_print_row(sign_bits, HUGE_1GB_OFFSET_SIZE, "significant bits");
    pretty_print_row(offsets, num_offsets, "offsets");

    /* III. find functions */
    int functions[HUGE_1GB_OFFSET_SIZE] = { 0 };

    for (size_t k = 2; k <= num_offsets; k++) {
        gsl_combination* c = gsl_combination_calloc(num_offsets, k);

        do {
            bool found_function = true;

            for (size_t i = 0; i < BANK_THLD_SIZE_SMALL_PL; i++) {
                int base_eor = gsl_combination_eor(
                    c, offsets, small_pool->addrs[i]);

                for (size_t j = i; j < BANK_THLD_SIZE_LARGE_PL;
                     j += BANK_THLD_SIZE_SMALL_PL) {

                    /* bank conflict -> same bank -> same bits */
                    if (medians_unsorted[j] > bank_thrs) {

                        int eor = gsl_combination_eor(
                            c, offsets, large_pool->addrs[j]);

                        /* because the bits of combination c are equal? */
                        if (eor != base_eor) {
                            found_function = false;
                            break;
                        }
                    }
                }
            }

            if (found_function) {
                /* make sure function is not a linear combination */
                size_t sum = 0;
                for (size_t j = 0; j < c->k; j++) {
                    sum += functions[c->data[j]];
                }

                if (sum < c->k) {
                    /* TODO: use row printing here */
                    /* TODO: write to file after confirmation? */
                    stderrp("found function");
                    for (size_t j = 0; j < c->k; j++) {
                        stderrp(" %u", offsets[c->data[j]]);
                        functions[c->data[j]] = 1;
                    }
                    stderrp("\n");
                }
            }

        } while (gsl_combination_next(c) == GSL_SUCCESS);

        gsl_combination_free(c);
    }

    return bank_thrs;
}

void find_row_and_column_bits(
    uintptr_t huge_page, size_t huge_page_size, int bank_thrs)
{
    pool_s* pool = pool_new_rand(huge_page, huge_page_size, RC_BITS_SIZE_PL);
    uintptr_t addr = pool_rand_addr_outside(pool);

    /* separate conflicting addresses from those that do not */
    uintptr_t addr_pair[] = { addr, 0 };

    size_t row_misses = 0;
    size_t num_significant_bits = 0;
    size_t significant_offsets[HUGE_1GB_OFFSET_SIZE];

    size_t num_addrs = 1024;
    uintptr_t addrs[num_addrs];

    int crd = addr_get_crd(addr, "b");

    for (size_t b = 3; b < HUGE_1GB_OFFSET_SIZE; b++) {

        addr_pair[1] = addr ^ (0x1 << b);

        /* changed bank? */
        if (!addr_has_crds(addr_pair[1], &crd, "b")) {

            /* find all options, might reverse flip */
            /* NOTE: this might also flip other bits than just b... */
            num_addrs = addr_set_crd(addr_pair[1], crd, "b", addrs, num_addrs);
            assert(num_addrs > 1);

            for (size_t i = 0; i < num_addrs; i++) {
                /* make sure flipping was not reversed */
                int f = (0x1 & (addrs[i] >> b)) ^ (0x1 & (addr >> b));

                /*
                 * NOTE: this number three is to minimize number of other bits
                 * we flip (and it cannot always be 2!)
                 */
                if ((num_one_bits(addrs[i] ^ addr) <= 3) && f) {
                    addr_pair[1] = addrs[i];
                    addr_print_bits(addr ^ addr_pair[1]);
                    break;
                }
            }
        }

        access_pattern_install(addr_pair, 2, 1);

        stat_s* cycles_stat = access_pattern_time_read_pair(
            addr_pair, BANK_THLD_NUM_REPS, true);

        stderrp("%lu %d\n", b, cycles_stat->median);

        if (cycles_stat->median < bank_thrs) { /* nothing or column */
            row_misses++;
        } else { /* row */
            significant_offsets[num_significant_bits] = b;
            num_significant_bits++;
        }
    }

    /* print significant offsets */
    infop("conflicts:");
    for (size_t i = 0; i < num_significant_bits; i++) {
        fprintf(stderr, " %lu", significant_offsets[i]);
    }
    fprintf(stderr, "\n");

    infop("got %lu row hits, %lu row misses\n", num_significant_bits,
        row_misses);
}
