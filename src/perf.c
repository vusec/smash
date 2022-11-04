#include "perf.h"
#include "debug.h"
#include "types.h"

#include <assert.h>
#include <string.h>

void cpuid_arch_perf(void)
{
    uint32_t eax, ebx, ecx, edx;
    asm volatile("mov $0x0a, %%eax\n\t"
                 "cpuid\n\t"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    assert(ecx == 0);

    infop("cpuid-based info about architectural performance monitoring\n");
#define VERSION_ID 0xFF
    unsigned version_id = eax & VERSION_ID;
    rowp("version id",
        "%*u\n", COLUMN_WIDTH,
        version_id);
    assert(version_id > 0);

#define LLC_REFERENCES_AVAILABLE 0x8
    unsigned llc_reference_available = ebx & LLC_REFERENCES_AVAILABLE;
    rowp("llc references available?",
        "%*s\n", COLUMN_WIDTH,
        (llc_reference_available == LLC_REFERENCES_AVAILABLE) ? "no" : "yes");
    assert(llc_reference_available == 0x0);

#define LLC_MISSES_AVAILABLE 0x10
    unsigned llc_miss_available = ebx & LLC_MISSES_AVAILABLE;
    rowp("llc misses available?",
        "%*s\n", COLUMN_WIDTH,
        (llc_reference_available == LLC_MISSES_AVAILABLE) ? "no" : "yes");
    assert(llc_miss_available == 0x0);

#define BIT_WIDTH 0xFF0000
    unsigned bit_width = (eax & BIT_WIDTH) >> 16;
    rowp("bit width gen. purp. perf. counter",
        "%*u\n", COLUMN_WIDTH,
        bit_width);
    blanklinep();
}

void counter_disable(const char* select_reg_addr, const char* counter_reg_addr,
    const char* mask_disable)
{
    /* disable counter */
    char str[MAX_LEN_STR];
    strncpy(str, MSR_WRITE, MAX_LEN_STR);
    strcat(str, SPACE);
    strcat(str, select_reg_addr);
    strcat(str, SPACE);
    strcat(str, mask_disable);
    assert(system(str) == 0);
    memset(str, 0, MAX_LEN_STR);

    /* reset value */
    strncpy(str, MSR_WRITE, MAX_LEN_STR);
    strcat(str, SPACE);
    strcat(str, counter_reg_addr);
    strcat(str, SPACE);
    strcat(str, RESET);
    assert(system(str) == 0);
    memset(str, 0, MAX_LEN_STR);
}

/* we cannot actually perform the instruction, need to use kernel module */
void counter_enable(const char* select_reg_addr, const char* counter_reg_addr,
    const char* mask_enable, const char* mask_disable)
{
    counter_disable(select_reg_addr, counter_reg_addr, mask_disable);

    /* enable counter */
    char str[MAX_LEN_STR];
    strncpy(str, MSR_WRITE, MAX_LEN_STR);
    strcat(str, SPACE);
    strcat(str, select_reg_addr);
    strcat(str, SPACE);
    strcat(str, mask_enable);
    assert(system(str) == 0);
    memset(str, 0, MAX_LEN_STR);
}
