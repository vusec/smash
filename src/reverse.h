#include "types.h"

#define CACHE_THLD_NUM_REPS 1048576
#define CACHE_THLD_HIST_GRAN 4

#define BANK_THLD_NUM_REPS 16384
#define BANK_THLD_HIST_GRAN 4

#define BANK_THLD_SIZE_LARGE_PL 16384 /* 16384 or higher */
#define BANK_THLD_SIZE_SMALL_PL 128 /* 128 works */

#define RC_BITS_SIZE_PL 8192

int find_cache_thrs(void);
int find_bank_thrs(uintptr_t huge_page, size_t huge_page_size);
void find_row_and_column_bits(
    uintptr_t huge_page, size_t huge_page_size, int bank_thrs);
