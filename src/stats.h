#include "types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* misc */
char* map_large_buf(size_t num_pages);
void unmap_large_buf(char* buf, size_t num_pages);

/* actual stats */
int median(int* data, size_t len);

stat_s* stat_new(int* data, size_t len, size_t num_quants);
void stat_print(const stat_s* in, const char* str, bool all_quants);

gsl_histogram* hist(const int* data, size_t len, size_t granularity);

/* timings */
inline __attribute__((always_inline)) unsigned long time_read_single(const void* addr)
{
    /*
     * NOTE: these comments are taken from the Intel manual, i.e., from the
     * description of RDTSC!
     */
    unsigned long ccnt = 0;
    asm volatile(
        /*
         * ensures RDTSC [is] executed only after all previous instructions
         * have executed and all previous loads and stores (clflush) are
         * globally visible
         */
        "mfence\n\t"
        "lfence\n\t"
        "rdtsc\n\t"
        /*
         * ensures RDTSC [is] executed prior to execution of any subsequent
         * instruction (including any memory accesses)
         */
        "lfence\n\t"
        "mov %%rax, %%rbx\n\t"

        "mov (%[addr]), %%rcx\n\t"
        /*
         * ensures RDTSC to be executed only after all previous instructions
         * have executed and all previous loads are globally visible
         */
        "lfence\n\t"
        "rdtsc\n\t"
        "sub %%rbx, %%rax\n\t"
        : "+a"(ccnt)
        : [addr] "r"(addr)
        : "rbx", "rcx", "rdx");
    return ccnt;
}
