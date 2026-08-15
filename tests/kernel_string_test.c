#include "test_util.h"

#include <stddef.h>

#include "string.h"

static int test_kernel_string(void)
{
    char buf[64];
    const char *src = "hello kernel";

    /* memcpy */
    memset(buf, 0, sizeof(buf));
    CHECK(memcpy(buf, src, 12) == buf);
    CHECK_MEM(buf, src, 12);

    /* memset */
    memset(buf, 0xAA, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); i++)
        CHECK_EQ((unsigned char)buf[i], 0xAA);

    /* strlen */
    CHECK_EQ(strlen(""), 0);
    CHECK_EQ(strlen("abc"), 3);
    CHECK_EQ(strlen(src), 12);

    /* strcmp */
    CHECK_EQ(strcmp("abc", "abc"), 0);
    CHECK(strcmp("abc", "abd") < 0);
    CHECK(strcmp("abd", "abc") > 0);
    CHECK(strcmp("abc", "ab") > 0);
    CHECK(strcmp("ab", "abc") < 0);
    CHECK_EQ(strcmp("", ""), 0);

    /* strncmp */
    CHECK_EQ(strncmp("abc", "abc", 3), 0);
    CHECK_EQ(strncmp("abc", "abd", 2), 0);
    CHECK(strncmp("abc", "abd", 3) < 0);
    CHECK_EQ(strncmp("abc", "abc", 0), 0);
    CHECK_EQ(strncmp("abc", "abd", 0), 0);
    CHECK_EQ(strncmp("abc", "ab", 3), 'c');
    CHECK_EQ(strncmp("ab", "abc", 3), -'c');

    /* strcpy */
    memset(buf, 0, sizeof(buf));
    CHECK(strcpy(buf, src) == buf);
    CHECK_STR(buf, src);

    /* strncpy pads with NULs and does not read past n. */
    memset(buf, 0xFF, sizeof(buf));
    strncpy(buf, "abc", 5);
    CHECK_MEM(buf, "abc\0\0", 5);
    CHECK_EQ((unsigned char)buf[5], 0xFF);
    strncpy(buf, "abcdef", 4);
    CHECK_MEM(buf, "abcd", 4);

    TEST_REPORT("kernel/string");
}

int main(void)
{
    return test_kernel_string();
}
