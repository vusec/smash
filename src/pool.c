#include "pool.h"
#include "debug.h"
#include "map.h"
#include "more-drama.h"

#include <assert.h>
#include <string.h>

extern int row_perm;

static pool_s* pool_new(
    uintptr_t huge_page, size_t huge_page_size, size_t num_addrs)
{
    pool_s* pool = calloc(1, sizeof(pool_s) + sizeof(uintptr_t) * num_addrs);
    pool->huge_page = huge_page;
    pool->huge_page_size = huge_page_size;
    pool->num_addrs = num_addrs;
    return pool;
}

pool_s* pool_copy(const pool_s* in)
{
    pool_s* pool = pool_new(in->huge_page, in->huge_page_size, in->num_addrs);
    memcpy(pool->addrs, in->addrs, sizeof(uintptr_t) * in->num_addrs);
    return pool;
}

static void pool_prune(pool_s** in, size_t new_num_addrs)
{
    *in = realloc(*in, sizeof(pool_s) + sizeof(uintptr_t) * new_num_addrs);
    assert(*in);
    (*in)->num_addrs = new_num_addrs;
}

static uintptr_t pool_remove(pool_s** in, uintptr_t addr)
{
    uintptr_t bddr = 0;

    for (size_t i = 0; i < (*in)->num_addrs; i++) {
        if ((*in)->addrs[i] == addr) {
            bddr = addr;
            (*in)->addrs[i] = (*in)->addrs[(*in)->num_addrs - 1];
            (*in)->addrs[(*in)->num_addrs - 1] = 0;
        }
    }

    pool_prune(in, (*in)->num_addrs - 1);

    return bddr;
}

static void pool_filter(pool_s** in, int* crds, const char* map_names)
{
    size_t new_num_addrs = 0;

    for (size_t i = 0; i < (*in)->num_addrs; i++) {
        if (!addr_has_crds((*in)->addrs[i], crds, map_names)) {
            (*in)->addrs[i] = 0;
        } else {
            (*in)->addrs[new_num_addrs] = (*in)->addrs[i];
            new_num_addrs++;
        }
    }

    pool_prune(in, new_num_addrs);
}

uintptr_t pool_rand_addr_outside(pool_s* in)
{
    bool found_rand_addr_outside = false;
    uintptr_t rand_addr = 0;

    while (!found_rand_addr_outside) {
        rand_addr = in->huge_page
            + (rand() % (in->huge_page_size / CACHE_LINE_SIZE)) * CACHE_LINE_SIZE;

        bool addr_in_pool = false;
        for (size_t i = 0; i < in->num_addrs; i++) {
            if (in->addrs[i] == rand_addr) {
                addr_in_pool = true;
                break;
            }
        }

        if (!addr_in_pool) {
            found_rand_addr_outside = true;
        }
    }

    return rand_addr;
}

uintptr_t pool_pop_rand_addr(pool_s** in, int* crds, const char* map_names)
{
    pool_s* subpool = pool_copy(*in);
    pool_filter(&subpool, crds, map_names);

    if (subpool->num_addrs == 1) {
        return pool_remove(in, subpool->addrs[0]);
    } else if (subpool->num_addrs > 1) {
        return pool_remove(in, subpool->addrs[rand() % subpool->num_addrs]);
    } else {
        return 0;
    }
}

pool_s* pool_new_rand(
    uintptr_t huge_page, size_t huge_page_size, size_t num_addrs)
{
    pool_s* pool = pool_new(huge_page, huge_page_size, num_addrs);

    assert(huge_page_size % CACHE_LINE_SIZE == 0);

    for (size_t i = 0; i < num_addrs; i++) {
        pool->addrs[i] = huge_page
            + (rand() % (huge_page_size / CACHE_LINE_SIZE)) * CACHE_LINE_SIZE;
    }

    return pool;
}

void pool_print(pool_s* in)
{
    addrs_print_crds(in->addrs, in->num_addrs);
}

pool_s* pool_new_with_crds(
    uintptr_t huge_page, size_t huge_page_size, int* crds, const char* map_names)
{
    pool_s* pool = pool_new(huge_page, huge_page_size, MAX_POOL_SIZE);

    size_t num_addrs = 0;
    for (uintptr_t mb_page = huge_page; mb_page < huge_page + GB; mb_page += 2 * MB) {
        for (uintptr_t offset = 0; offset < 2 * MB; offset += CACHE_LINE_SIZE) {
            uintptr_t addr = mb_page + offset;

            if (addr_has_crds(addr, crds, map_names) && num_addrs < MAX_POOL_SIZE) {
                pool->addrs[num_addrs] = addr;
                num_addrs++;
            }
        }
    }

    pool_prune(&pool, num_addrs);

    return pool;
}

void pool_special_extend(pool_s** in)
{
    pool_s* pool = pool_new((*in)->huge_page, (*in)->huge_page_size, MAX_POOL_SIZE);
    size_t new_num_addrs = 0;

    for (size_t i = 0; i < (*in)->num_addrs; i++) {
        uintptr_t addr = (*in)->addrs[i];

        /* TODO: get rid of addr_get_free_bits */
        if (addr_get_free_bits(addr) == 1) {
            pool->addrs[new_num_addrs] = addr;
            new_num_addrs++;

            /* TODO: ugly */
            int dummy = addr_get_crd(0x80000, "r");

            if (dummy == (4 ^ ROW_PERM)) { /* 8 GB */
                pool->addrs[new_num_addrs] = addr ^ (1 << 18) ^ (1 << 15)
                    ^ (1 << 19) ^ (1 << 16);
            } else if (dummy == 1) { /* 16 GB */
                pool->addrs[new_num_addrs] = addr ^ (1 << 20) ^ (1 << 16)
                    ^ (1 << 13) ^ (1 << 9);
            } else {
                errord("unknown host?\n");
            }

            new_num_addrs++;
        }
    }

    pool_prune(&pool, new_num_addrs);
    *in = pool;
}

void pool_free(pool_s* in)
{
    free(in);
}
