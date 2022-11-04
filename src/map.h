#include "types.h"

int mhsh(const char* map_name, const char* map_names);

int addr_get_crd(uintptr_t in, const char* map_name);
uintptr_t addr_set_crd_prs(
    uintptr_t in, int crd, const char* map_name, const char* map_names_prs);
size_t addr_set_crd(
    uintptr_t in, int crd, const char* map_name, uintptr_t* addrs, size_t num_addrs);

bool addr_has_crds(uintptr_t in, int* crds, const char* map_names);
void addr_print_bits(uintptr_t addr);

int addr_get_non_free_bits(uintptr_t in);
int addr_get_free_bits(uintptr_t in);

void addrs_print_crds(uintptr_t* in, size_t len);

void maps_from_files(
    const char* path, const char** files, size_t num_files, const char* delim,
    const char* map_names);

void maps_print(void);

size_t get_num_cols(void);

int num_one_bits(uintptr_t x);
