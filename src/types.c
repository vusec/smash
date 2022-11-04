#include "types.h"
#include "debug.h"
#include "stats.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

void rand_bytes(unsigned char* in, size_t cnt)
{
    for (size_t i = 0; i < cnt; i++) {
        in[i] = (unsigned char)(rand() % 256);
    }
}

size_t comp_num_vic_rows(int min_row_crd, int dist)
{
    size_t num_vics;

    if (dist != 1) {
        num_vics = (NUM_AGGS / 2) * 3;
    } else {
        num_vics = NUM_AGGS + 1;
    }

    /* TODO: add check for top row */
    if (min_row_crd == 0) {
        num_vics -= 1;
    }

    return num_vics;
}

int* filter(const int* in, size_t len, unsigned val)
{
    /* determine number of matches */
    size_t num_non_matches = 0;
    for (size_t i = 0; i < len; i++) {
        if (in[i] != val) {
            num_non_matches++;
        }
    }

    /* copy matches */
    if (num_non_matches < len) {
        int* out = calloc(len, sizeof(int));

        size_t j = 0;
        for (size_t i = 0; i < len; i++) {
            if (in[i] != val) {
                out[j] = in[i];
                j += 1;
            }
        }
        assert(j == num_non_matches);

        return out;
    } else if (num_non_matches == len) {
        int* out = calloc(len, sizeof(int));
        memcpy(out, in, sizeof(int) * len);
        return out;
    } else {
        return NULL;
    }
}

void shuffle(int* in, size_t len)
{
    /* implements Sattolo's algorithm, see e.g. https://danluu.com/sattolo/ */
    for (size_t i = 0; i < len; i++) {
        size_t j = (i + 1) + rand() % (len - (i + 1));

        /* swap */
        int tmp = in[i];
        in[i] = in[j];
        in[j] = tmp;
    }
}

void pretty_print(const unsigned char* in, size_t len)
{
    size_t num_per_line = 32;

    for (size_t i = 0; i < len; i++) {
        fprintf(stderr, " %02x", in[i]);

        if ((i + 1) % num_per_line == 0) {
            fprintf(stderr, " ...\n");
            /* NOTE: should be number of columns */
            i += 1024;
        }
    }

    fprintf(stderr, "\n");
}

void pretty_print_row(const int* in, size_t len, const char* label)
{
    char str[MAX_LEN_STR];
    memset(str, 0, MAX_LEN_STR);

    for (size_t i = 0; i < len; i++) {
        if (i == 0) {
            sprintf(str, "%*u", COLUMN_WIDTH, in[i]);
        } else {
            sprintf(str + COLUMN_WIDTH + SMALL_COLUMN_WIDTH * (i - 1), "%*u",
                SMALL_COLUMN_WIDTH, in[i]);
        }
    }

    assert(strlen(str) < MAX_LEN_STR);
    rowp(label, "%s\n", str);
}

void export_to_file(const int* in, size_t len)
{
    int fd = open(
        EXPORT_PATH, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);

    if (fd == -1) {
        failured_lib();
    }

    for (size_t i = 0; i < (len - 1); i++) {
        dprintf(fd, "%u ", in[i]);
    }
    dprintf(fd, "%u\n", in[len - 1]);
}
