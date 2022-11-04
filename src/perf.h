#define SPACE " "
#define MSR_WRITE "wrmsr -a" /* add -a if not setting core affinity */
#define MSR_READ "rdmsr"
#define IA32_PMC0 "0xc1" /* used for misses */
#define IA32_PMC1 "0xc2"
#define IA32_PERFEVTSEL0 "0x186" /* used for misses */
#define IA32_PERFEVTSEL1 "0x187"
#define ENABLE_LLC_MISSES "0x51412E"
#define ENABLE_LLC_REFERENCES "0x514F2E"
#define DISABLE_LLC_MISSES "0x11412E"
#define DISABLE_LLC_REFERENCES "0x114F2E"
#define RESET "0x0"

void cpuid_arch_perf(void);
void counter_disable(const char* select_reg_addr, const char* counter_reg_addr,
    const char* mask_disable);
void counter_enable(const char* select_reg_addr, const char* counter_reg_addr,
    const char* mask_enable, const char* mask_disable);
