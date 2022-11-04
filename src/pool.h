#include "types.h"

#define COLUMN_MASK_LAB3 0x1FF8UL
#define MAX_POOL_SIZE (1 << 20)

pool_s* pool_new_rand(
    uintptr_t huge_page, size_t huge_page_size, size_t num_addrs);

pool_s* pool_new_with_crds(
    uintptr_t huge_page, size_t huge_page_size, int* crds, const char* map_names);

void pool_special_extend(pool_s** in);

uintptr_t pool_rand_addr_outside(pool_s* in);

uintptr_t pool_pop_rand_addr(pool_s** in, int* crds, const char* map_names);

pool_s* pool_copy(const pool_s* in);

void pool_print(pool_s* in);

void pool_free(pool_s* in);
