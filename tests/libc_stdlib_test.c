#include "test_util.h"

#include <stddef.h>
#include <stdint.h>

#include "errno.h"
#include "stdlib.h"
#include "string.h"

extern char **environ;

static int test_libc_stdlib(void)
{
    /* malloc / free. */
    {
        char *p = (char *)malloc(64);
        CHECK(p != NULL);
        memset(p, 0x77, 64);
        for (int i = 0; i < 64; i++)
            CHECK_EQ((unsigned char)p[i], 0x77);
        free(p);

        /* malloc(0) is implementation-defined; this one returns NULL. */
        CHECK(malloc(0) == NULL);
        free(NULL);
    }

    /* calloc zeroes memory. */
    {
        unsigned *p = (unsigned *)calloc(8, sizeof(unsigned));
        CHECK(p != NULL);
        for (int i = 0; i < 8; i++)
            CHECK_EQ(p[i], 0u);
        free(p);

        /* Overflow detection. */
        CHECK(calloc(SIZE_MAX, 2) == NULL);
        CHECK_EQ(errno, ENOMEM);
        errno = 0;
    }

    /* realloc preserves contents. */
    {
        char *p = (char *)malloc(4);
        CHECK(p != NULL);
        memcpy(p, "abcd", 4);
        char *q = (char *)realloc(p, 100);
        CHECK(q != NULL);
        CHECK_MEM(q, "abcd", 4);

        /* Growing in place must keep the same pointer if it fits; the
           implementation grows in place when the free space allows. */
        char *r = (char *)realloc(q, 64);
        CHECK(r != NULL);
        CHECK_MEM(r, "abcd", 4);
        free(r);

        /* realloc(NULL, n) == malloc(n). */
        char *s = (char *)realloc(NULL, 16);
        CHECK(s != NULL);
        CHECK(realloc(s, 0) == NULL);
    }

    /* atoi / atol. */
    CHECK_EQ(atoi("0"), 0);
    CHECK_EQ(atoi("42"), 42);
    CHECK_EQ(atoi("-42"), -42);
    CHECK_EQ(atoi("  +7"), 7);
    CHECK_EQ(atoi("  123"), 123);
    CHECK_EQ(atoi("abc"), 0);
    CHECK_EQ(atoi("12abc"), 12);
    CHECK_EQ(atoi(""), 0);
    CHECK_EQ(atol("0"), 0L);
    CHECK_EQ(atol("123456789"), 123456789L);
    CHECK_EQ(atol("-987654321"), -987654321L);

    /* getenv. */
    {
        char *env[] = {
            "HOME=/root",
            "PATH=/bin:/usr/bin",
            "AXIOME_OS=1",
            NULL,
        };
        environ = env;
        CHECK_STR(getenv("HOME"), "/root");
        CHECK_STR(getenv("PATH"), "/bin:/usr/bin");
        CHECK_STR(getenv("AXIOME_OS"), "1");
        CHECK(getenv("NOT_SET") == NULL);
        CHECK(getenv(NULL) == NULL);
        CHECK(getenv("") == NULL);
    }

    TEST_REPORT("libc/stdlib");
}

int main(void)
{
    return test_libc_stdlib();
}
