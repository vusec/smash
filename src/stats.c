#include "stats.h"
#include "debug.h"
#include "map.h"
#include "pool.h"

#include <assert.h>
#include <fcntl.h>
#include <gsl/gsl_sort_int.h>
#include <gsl/gsl_statistics_int.h>
#include <limits.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

char* map_large_buf(size_t num_pages)
{
    char* buf = mmap(NULL, num_pages * PAGE_SIZE,
        PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    return buf;
}

void unmap_large_buf(char* buf, size_t num_pages)
{
    munmap(buf, num_pages * PAGE_SIZE);
}

int median(int* data, size_t len)
{
    gsl_sort_int(data, 1, len);
    return gsl_stats_int_median_from_sorted_data(data, 1, len);
}

gsl_histogram* hist(const int* data, size_t len, size_t granularity)
{
    int* cpy = calloc(len, sizeof(int));
    memcpy(cpy, data, sizeof(int) * len);

    size_t num_quants = 100;
    stat_s* stat = stat_new(cpy, len, num_quants);

    /* find min and max */
    int min = stat->quants[0].val;
    int max = stat->quants[num_quants].val;

    if (max == min) {
        max += 1;
    }

    /* set bins */
    int num_bins = (max + 1 - min + granularity - 1) / granularity;

    gsl_histogram* hist = gsl_histogram_alloc(num_bins);
    gsl_histogram_set_ranges_uniform(hist, min, max + 1);

    for (size_t i = 0; i < len; i++) {
        gsl_histogram_increment(hist, cpy[i]);
    }

    /* printing */
    double sum = gsl_histogram_sum(hist);
    double prev_lower = 0;
    bool first = true;

    for (size_t i = 0; i < num_bins; i++) {
        double count = gsl_histogram_get(hist, i);

        if (count > 0) {
            double lower = 0;
            double upper = 0;
            gsl_histogram_get_range(hist, i, &lower, &upper);

            if (lower - prev_lower > granularity && !first) {
                stderrp("\n");
            }

            /* NOTE: printing without decimals might give misleading results */
            stderrp("%.2f ", lower);
            for (size_t j = 0; j < count; j++) {
                if (j > TERMINAL_WIDTH) {
                    break;
                }

                stderrp("-");
            }

            stderrp(" %.2f\n", 100 * count / sum);

            prev_lower = lower;
            first = false;
        }
    }

    return hist;
}

static size_t stat_filter_negative(int* data, size_t len)
{
    size_t new_len = 0;

    for (size_t i = 0; i < len; i++) {
        if (data[i] >= 0) {
            data[new_len] = data[i];
            new_len++;
        }
    }

    return new_len;
}

/* TODO: might want to make this const data* int? */
stat_s* stat_new(int* data, size_t len, size_t num_quants)
{
    len = stat_filter_negative(data, len);

    assert(num_quants > 0);
    /* add one so we always have quantiles at zero and one */
    num_quants = num_quants + 1;

    gsl_sort_int(data, 1, len);

    stat_s* stat = calloc(1, sizeof(stat_s) + sizeof(quant_s) * num_quants);
    stat->num_quants = num_quants;

    double incr = (double)1 / (num_quants - 1);
    double frac = 0;
    for (size_t i = 0; i < num_quants; i++, frac += incr) {
        stat->quants[i].frac = frac;
        stat->quants[i].val = gsl_stats_int_quantile_from_sorted_data(
            data, 1, len, frac);
    }

    /* median (might also use quantiles, but median is always interesting) */
    stat->median = gsl_stats_int_median_from_sorted_data(data, 1, len);
    stat->sample_size = len;

    return stat;
}

void stat_print(const stat_s* in, const char* str, bool all_quants)
{
    size_t bounds = 4;
    assert(in->num_quants >= (2 * bounds + 1));

    char* quant_str = "(%4.2f):[%6.1f]  ";

    if (str) {
        int len = strlen(str);
        assertd(len < NUM_SPACES, "description longer than %u characters\n",
            NUM_SPACES);
        stderrp("%s%*s", str, NUM_SPACES - len, " ");
    }

    if (all_quants) {
        for (size_t i = 0; i < in->num_quants; i++) {
            stderrp(quant_str, in->quants[i].frac, in->quants[i].val);
        }

        fprintf(stderr, "\n");
    } else {
        for (size_t i = 0; i < bounds; i++) {
            stderrp(quant_str, in->quants[i].frac, in->quants[i].val);
        }

        stderrp(quant_str, 0.50, (double)in->median);

        for (size_t i = in->num_quants - bounds; i < in->num_quants; i++) {
            stderrp(quant_str, in->quants[i].frac, in->quants[i].val);
        }

        stderrp("\n");
    }
}
