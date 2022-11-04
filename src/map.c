#include "map.h"
#include "debug.h"
#include "more-drama.h"

#include <assert.h>
#include <string.h>

static map_s maps[NUM_MAPS];

/* poor man's hash table */
int mhsh(const char* map_name, const char* map_names)
{
    for (size_t i = 0; i < strlen(map_names); i++) {
        if (map_name[0] == map_names[i]) {
            return i;
        }
    }

    return -1;
}

static int num_trailing_zeros(uintptr_t x)
{
    int ntz = 0;

    while (x && !(x & (uintptr_t)0x1)) {
        ntz++;
        x = x >> 1;
    }

    return ntz;
}

int num_one_bits(uintptr_t x)
{
    int nob = 0;

    while (x) {
        nob += x & (uintptr_t)0x1;
        x = x >> 1;
    }

    return nob;
}

static map_s* map_new(const char* map_name)
{
    map_s* map = calloc(1, sizeof(map_s));
    map->name = map_name;
    return map;
}

static void map_set_masks(map_s* in, uintptr_t* masks, size_t num_masks)
{
    in->num_masks = num_masks;
    memcpy(in->masks, masks, sizeof(uintptr_t) * num_masks);
}

static int map_get_crd(const map_s* in, uintptr_t addr)
{
    if (in->num_masks == 1) {
        uintptr_t tmp = in->masks[0] & addr;
        tmp = tmp >> num_trailing_zeros(in->masks[0]);

        if (in->name[0] == 'r') {
            tmp ^= ROW_PERM;
        }

        return tmp;

    } else if (in->num_masks > 1) {

        int index = 0;
        for (size_t i = 0; i < in->num_masks; i++) {
            int tmp = num_one_bits(in->masks[i] & addr) % 2;
            index |= tmp << i;
        }

        return index;
    }

    return -1;
}

static map_s* get_map(const char* map_name)
{
    return &maps[mhsh(map_name, MAP_NAMES)];
}

static void set_map(const map_s* map)
{
    memcpy(&maps[mhsh(map->name, MAP_NAMES)], map, sizeof(map_s));
}

static uintptr_t addr_set_crd_from_mask(uintptr_t in, uintptr_t mask, int crd)
{
    /* TODO: what happens if the coordinate is too small or large? */
    uintptr_t tmp = 0;

    for (size_t i = 0; i < NUM_BITS_PER_BYTE * sizeof(int); i++) {
        int shift = num_trailing_zeros(mask);

        tmp = in & ((uintptr_t)0x1 << shift);
        in ^= tmp; /* zero it */

        tmp = (crd & (uintptr_t)0x1) << shift;
        in ^= tmp; /* set it */

        /* unset bit in mask */
        mask ^= ((uintptr_t)0x1 << shift);
        crd = crd >> 1;
    }

    return in;
}

int addr_get_crd(uintptr_t in, const char* map_name)
{
    return map_get_crd(get_map(map_name), in);
}

/* set crd and preserves those specified in map_names_prs, returns one address */
#define MAX_NUM_ADDRS 1024
uintptr_t addr_set_crd_prs(
    uintptr_t in, int crd, const char* map_name, const char* map_names_prs)
{
    size_t num_map_names_prs = strlen(map_names_prs);
    int crds_prs[num_map_names_prs];
    for (size_t i = 0; i < num_map_names_prs; i++) {
        crds_prs[i] = addr_get_crd(in, &map_names_prs[i]);
    }

    uintptr_t addrs[MAX_NUM_ADDRS] = { 0 };
    size_t num_addrs = addr_set_crd(in, crd, map_name, addrs, MAX_NUM_ADDRS);

    for (size_t i = 0; i < num_addrs; i++) {
        if (addr_has_crds(addrs[i], crds_prs, map_names_prs)) {
            return addrs[i];
        }
    }

    return 0;
}

/* sets crd and returns array with addresses that have that crd */
size_t addr_set_crd(
    uintptr_t in, int crd, const char* map_name, uintptr_t* addrs, size_t num_addrs)
{
    map_s* map = get_map(map_name);

    if (map->num_masks == 1) {

        if (map_name[0] == 'r') {
            crd ^= ROW_PERM;
        }

        /* TODO: this should be able to fail */
        addrs[0] = addr_set_crd_from_mask(in, map->masks[0], crd);
        return 1;
    } else if (map->num_masks > 1) {
        size_t new_num_addrs = 0;
        uintptr_t jn_mask = 0;

        for (size_t i = 0; i < map->num_masks; i++) {
            jn_mask |= map->masks[i];
        }

        for (uintptr_t jn_crd = 0;
             jn_crd < (uintptr_t)0x1 << num_one_bits(jn_mask);
             jn_crd++) {
            uintptr_t addr = addr_set_crd_from_mask(in, jn_mask, jn_crd);

            if (addr_has_crds(addr, &crd, map_name)) {
                assert(new_num_addrs < num_addrs);
                addrs[new_num_addrs] = addr;
                new_num_addrs++;
            }
        }

        return new_num_addrs;
    } else {
        return 0;
    }
}

bool addr_has_crds(uintptr_t in, int* crds, const char* map_names)
{
    for (size_t i = 0; i < strlen(map_names); i++) {
        /* -1 is a wildcard */
        if (crds[i] == -1) {
            continue;
        }

        if (crds[i] != addr_get_crd(in, &map_names[i])) {
            return false;
        }
    }

    return true;
}

int addr_get_free_bits(uintptr_t in)
{
    /* gets 18 and 19 */
    int bits = 0;
    bits ^= 0x3 & (in >> 18);
    /* TODO */
    /* bits ^= 0x3 & (in >> 20); */
    return bits;
}

void addr_print_bits(uintptr_t addr)
{
    for (int i = sizeof(uintptr_t) * NUM_BITS_PER_BYTE - 1; i >= 0; i--) {
        stderrp("%2d|", i);
    }
    stderrp("\n");

    for (int i = sizeof(uintptr_t) * NUM_BITS_PER_BYTE - 1; i >= 0; i--) {
        stderrp("%2x|", (char)((uintptr_t)0x1 & (addr >> i)));
    }
    stderrp("\n");
}

void addrs_print_crds(uintptr_t* in, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        fprintf(stderr,
            "%lu %p %d,%d,%d %d,%d\n",
            i,
            (char*)in[i],
            addr_get_crd(in[i], "r"),
            addr_get_crd(in[i], "c"),
            addr_get_crd(in[i], "b"),
            addr_get_crd(in[i], "s"),
            addr_get_crd(in[i], "l"));
    }
}

static uintptr_t line_to_mask(char* line, const char* delim)
{
    uintptr_t mask = 0;
    int offset = 0;

    char* token = NULL;
    size_t i = 0;
    while ((token = strtok(i ? NULL : line, delim)) != NULL) {
        assert(i < NUM_BITS_PER_BYTE * sizeof(uintptr_t));

        sscanf(token, "%d", &offset);

        if (verbose_flag) {
            stderrp("%d ", offset);
        }

        mask |= (uintptr_t)0x1 << offset;

        i++;
    }

    if (verbose_flag) {
        stderrp("\n");
    }

    return mask;
}

static map_s* map_new_from_file(
    const char* file, const char* delim, const char* name)
{
    FILE* stream = fopen(file, "r");
    if (stream == NULL) {
        failured_lib();
    }

    char* lineptr = NULL;
    size_t num_chars_read = 0;

    uintptr_t masks[MAX_NUM_MASKS] = { 0 };
    size_t i = 0;
    while (getline(&lineptr, &num_chars_read, stream) != -1 && i < MAX_NUM_MASKS) {
        masks[i] = line_to_mask(lineptr, delim);
        i++;
    }

    map_s* map = map_new(name);
    map_set_masks(map, masks, i);

    fclose(stream);

    return map;
}

void maps_from_files(
    const char* path, const char** files, size_t num_files, const char* delim,
    const char* map_names)
{
    char buf[MAX_LEN_STR] = { 0 };
    strncat(buf, path, MAX_LEN_USR_STR);
    size_t len = strlen(buf);

    for (size_t i = 0; i < num_files; i++) {
        assert(i < strlen(map_names));

        strncat(buf, files[i], MAX_LEN_USR_STR);
        map_s* map = map_new_from_file(buf, delim, &map_names[i]);
        memset(buf + len, 0, MAX_LEN_USR_STR);

        set_map(map);
    }
}

void maps_print(void)
{
    for (size_t i = 0; i < strlen(MAP_NAMES); i++) {
        map_s* map = &maps[i];

        if (map->name) {
            stderrp("%c %lu ", map->name[0], map->num_masks);

            for (size_t j = 0; j < MAX_NUM_MASKS; j++) {
                stderrp("%lx", map->masks[j]);

                if (j + 1 < MAX_NUM_MASKS) {
                    stderrp(",");
                }
            }

            stderrp("\n");
        }
    }
}

size_t get_num_cols(void)
{
    map_s* map = get_map("c");
    assert(map->num_masks == 1);
    return 0x1 << num_one_bits(map->masks[0]);
}
