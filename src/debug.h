#include <stdio.h>
#include <stdlib.h>

#define NUM_SPACES 48

#define blanklinep() fprintf(stderr, "\n")

#define stderrp(...) fprintf(stderr, __VA_ARGS__)

/* don't use debugp() directly */
#define debugp() fprintf(stderr, "%s:%d:%s: ", __FILE__, __LINE__, __func__)

#define infop(...) successd_lib(__VA_ARGS__)

#define successd_lib(...)     \
    do {                      \
        debugp();             \
        stderrp(__VA_ARGS__); \
    } while (0)

#define failured_lib()      \
    do {                    \
        debugp();           \
        perror(NULL);       \
        exit(EXIT_FAILURE); \
    } while (0)

#define assertd(assertion, ...)   \
    do {                          \
        if (!(assertion)) {       \
            debugp();             \
            stderrp(__VA_ARGS__); \
            exit(EXIT_FAILURE);   \
        }                         \
    } while (0)

#define errord(...)           \
    do {                      \
        debugp();             \
        stderrp(__VA_ARGS__); \
        exit(EXIT_FAILURE);   \
    } while (0)

#define rowp(label, ...)                          \
    do {                                          \
        int len = snprintf(NULL, 0, "%s", label); \
        assert(len < NUM_SPACES);                 \
        stderrp("%s", label);                     \
        stderrp("%*s", NUM_SPACES - len, " ");    \
        stderrp(__VA_ARGS__);                     \
    } while (0)
