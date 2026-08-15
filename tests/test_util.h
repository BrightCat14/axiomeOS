#ifndef AXIOME_TESTS_TEST_UTIL_H
#define AXIOME_TESTS_TEST_UTIL_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static int g_checks = 0;
static int g_failures = 0;

static inline int test_str_eq(const char *a, const char *b)
{
    if (!a || !b)
        return a == b;
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

static inline int test_mem_eq(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return 0;
    return 1;
}

static inline const char *test_or_null(const char *s)
{
    return s ? s : "(null)";
}

#define CHECK(cond)                                                       \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(cond)) {                                                    \
            g_failures++;                                                 \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                    #cond);                                               \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        long long _a = (long long)(a);                                    \
        long long _b = (long long)(b);                                    \
        g_checks++;                                                       \
        if (_a != _b) {                                                   \
            g_failures++;                                                 \
            fprintf(stderr, "  FAIL %s:%d: %s == %lld, want %lld\n",      \
                    __FILE__, __LINE__, #a, _a, _b);                      \
        }                                                                 \
    } while (0)

#define CHECK_MEM(a, b, n)                                                \
    do {                                                                  \
        g_checks++;                                                       \
        if (!test_mem_eq((a), (b), (n))) {                                \
            g_failures++;                                                 \
            fprintf(stderr, "  FAIL %s:%d: %s != %s (%zu bytes)\n",       \
                    __FILE__, __LINE__, #a, #b, (size_t)(n));             \
        }                                                                 \
    } while (0)

#define CHECK_STR(a, b)                                                   \
    do {                                                                  \
        g_checks++;                                                       \
        if (!test_str_eq((a), (b))) {                                     \
            g_failures++;                                                 \
            fprintf(stderr, "  FAIL %s:%d: \"%s\" != \"%s\"\n",           \
                    __FILE__, __LINE__, test_or_null((a)),                \
                    test_or_null((b)));                                   \
        }                                                                 \
    } while (0)

#define TEST_REPORT(name)                                                 \
    do {                                                                  \
        if (g_failures == 0) {                                            \
            printf("PASS %-28s (%d checks)\n", name, g_checks);           \
        } else {                                                          \
            printf("FAIL %-28s (%d/%d failed)\n", name, g_failures,       \
                   g_checks);                                             \
        }                                                                 \
        return g_failures ? 1 : 0;                                        \
    } while (0)

#endif
