#include "test_util.h"

#include <stddef.h>

#include "string.h"

static int test_libc_string(void)
{
    char buf[64];
    const char *src = "hello libc";

    /* memcpy */
    memset(buf, 0, sizeof(buf));
    CHECK(memcpy(buf, src, 10) == buf);
    CHECK_MEM(buf, src, 10);

    /* memset */
    memset(buf, 0x5A, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); i++)
        CHECK_EQ((unsigned char)buf[i], 0x5A);

    /* memmove: overlapping copies both directions. */
    strcpy(buf, "0123456789");
    memmove(buf + 2, buf, 6);
    CHECK_MEM(buf, "0101234589", 10);
    strcpy(buf, "0123456789");
    memmove(buf, buf + 2, 6);
    CHECK_MEM(buf, "2345676789", 10);

    /* memcmp */
    CHECK_EQ(memcmp("abc", "abc", 3), 0);
    CHECK(memcmp("abc", "abd", 3) < 0);
    CHECK(memcmp("abd", "abc", 3) > 0);

    /* memchr */
    CHECK(memchr("hello", 'l', 5) == (void *)("hello" + 2));
    CHECK(memchr("hello", 'z', 5) == NULL);

    /* strlen */
    CHECK_EQ(strlen(""), 0);
    CHECK_EQ(strlen(src), 10);

    /* strcpy */
    memset(buf, 0, sizeof(buf));
    CHECK(strcpy(buf, src) == buf);
    CHECK_STR(buf, src);

    /* strncpy */
    memset(buf, 0xFF, sizeof(buf));
    strncpy(buf, "abc", 5);
    CHECK_MEM(buf, "abc\0\0", 5);
    CHECK_EQ((unsigned char)buf[5], 0xFF);

    /* strcat / strncat */
    strcpy(buf, "foo");
    CHECK(strcat(buf, "bar") == buf);
    CHECK_STR(buf, "foobar");
    buf[3] = '\0';
    CHECK(strncat(buf, "barbaz", 3) == buf);
    CHECK_STR(buf, "foo" "bar");

    /* strcmp / strncmp */
    CHECK_EQ(strcmp("abc", "abc"), 0);
    CHECK(strcmp("abc", "abd") < 0);
    CHECK(strcmp("abd", "abc") > 0);
    CHECK_EQ(strncmp("abc", "abd", 2), 0);
    CHECK(strncmp("abc", "abd", 3) < 0);

    /* strchr / strrchr */
    CHECK(strchr("hello", 'l') == (void *)("hello" + 2));
    CHECK(strchr("hello", 'z') == NULL);
    CHECK(strrchr("hello", 'l') == (void *)("hello" + 3));
    CHECK(strrchr("hello", 'h') == (void *)"hello");
    CHECK(strrchr("hello", 'z') == NULL);
    CHECK(strchr("abc", '\0') == (void *)("abc" + 3));

    /* strstr */
    CHECK(strstr("hello world", "world") == (void *)("hello world" + 6));
    CHECK(strstr("hello", "") == (void *)"hello");
    CHECK(strstr("hello", "xyz") == NULL);

    /* strdup */
    {
        char *d = strdup("dup me");
        CHECK(d != NULL);
        CHECK_STR(d, "dup me");
        CHECK(d != (void *)"dup me");
        free(d);
        CHECK(strdup(NULL) == NULL);
    }

    /* strcspn / strspn / strpbrk */
    CHECK_EQ(strcspn("hello world", " "), 5);
    CHECK_EQ(strcspn("abc", "xyz"), 3);
    CHECK_EQ(strspn("abcabc", "ab"), 2);
    CHECK_EQ(strspn("hello", "z"), 0);
    CHECK(strpbrk("hello world", " w") == (void *)("hello world" + 5));
    CHECK(strpbrk("hello", "xyz") == NULL);

    /* strtok */
    {
        char s[] = "a,b,,c";
        CHECK_STR(strtok(s, ","), "a");
        CHECK_STR(strtok(NULL, ","), "b");
        CHECK_STR(strtok(NULL, ","), "c");
        CHECK(strtok(NULL, ",") == NULL);
    }

    TEST_REPORT("libc/string");
}

int main(void)
{
    return test_libc_string();
}
