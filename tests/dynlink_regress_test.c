/* Host regression test for the in-kernel/standalone dynamic linker core
 * (kernel/dynlink.c).  It feeds the fixture PIE + shared object (built with
 * the host toolchain) to the *real* kernel sources and checks:
 *
 *   - PT_DYNAMIC parsing (strtab, dynsym, RELA and JMPREL tables)
 *   - DT_NEEDED dependency-name extraction
 *   - symbol resolution across the executable+library scope, including a
 *     fully independent raw dynsym walk for cross-validation
 *   - the relocation pass: every relocation is applied exactly once, all
 *     write targets stay inside the image, and a scope that is missing a
 *     dependency fails loudly instead of leaving GOT/PLT entries zeroed
 */

#include "test_util.h"

#include "dynlink.h"
#include "elf.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* NOTE: no <string.h> here: with KERNEL_INC in the include path, the host
 * header is shadowed by kernel/string.h. Use the helpers below instead. */

static const char *fixture_find_slash(const char *s)
{
    const char *last = 0;
    for (; *s; s++)
        if (*s == '/')
            last = s;
    return last;
}

static size_t fixture_len(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void fixture_copy(char *dst, const char *src, size_t n)
{
    while (n--)
        *dst++ = *src++;
}

static void fixture_path(const char *argv0, const char *name,
                         char *out, size_t outsz)
{
    const char *slash = fixture_find_slash(argv0);
    const char *dir = slash ? argv0 : ".";
    size_t dlen = slash ? (size_t)(slash - argv0) : 1;
    size_t nlen = fixture_len(name);
    if (dlen + 1 + nlen + 1 > outsz) {
        fprintf(stderr, "fixture_path: buffer too small\n");
        exit(2);
    }
    fixture_copy(out, dir, dlen);
    out[dlen] = '/';
    fixture_copy(out + dlen + 1, name, nlen);
    out[dlen + 1 + nlen] = 0;
}

static uint8_t *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long len;
    if (!f) {
        perror(path);
        exit(2);
    }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0) {
        perror("seek");
        exit(2);
    }
    rewind(f);
    data = malloc((size_t)len + 1);
    if (!data) {
        fprintf(stderr, "OOM\n");
        exit(2);
    }
    if (fread(data, 1, (size_t)len, f) != (size_t)len) {
        perror("read");
        exit(2);
    }
    fclose(f);
    data[len] = 0;
    if (out_len)
        *out_len = (size_t)len;
    return data;
}

/* Optional independent count of relocations that are not R_X86_64_NONE,
 * straight off the raw tables. Used to prove the apply pass visits every
 * relocation the parser reported. */
static int count_nonzero_relocs(const struct elf_object *o)
{
    int n = 0;
    if (o->info.rela_off && o->info.rela_count) {
        const struct elf64_rela *t =
            (const struct elf64_rela *)(o->buf + o->info.rela_off);
        for (size_t i = 0; i < o->info.rela_count; i++)
            if (ELF64_R_TYPE(t[i].r_info) != 0)
                n++;
    }
    if (o->info.jmprel_off && o->info.jmprel_count) {
        const struct elf64_rela *t =
            (const struct elf64_rela *)(o->buf + o->info.jmprel_off);
        for (size_t i = 0; i < o->info.jmprel_count; i++)
            if (ELF64_R_TYPE(t[i].r_info) != 0)
                n++;
    }
    return n;
}

static int g_place_count;
static int g_place_out_of_span;

static void place_cb(uint64_t target_va, uint64_t value, void *ctx)
{
    const struct elf_object *obj = (const struct elf_object *)ctx;
    (void)value;
    g_place_count++;
    if (target_va < obj->info.seg_lo || target_va >= obj->info.seg_hi)
        g_place_out_of_span++;
}

int main(int argc, char **argv)
{
    char app_path[512], lib_path[512];
    fixture_path(argc ? argv[0] : "tests", "dynlink_fixture_app", app_path,
                 sizeof app_path);
    fixture_path(argc ? argv[0] : "tests", "dynlink_fixture_lib.sl", lib_path,
                 sizeof lib_path);

    size_t asz, lsz;
    uint8_t *abuf = read_file(app_path, &asz);
    uint8_t *lbuf = read_file(lib_path, &lsz);

    struct elf_object app = { abuf, asz, { 0 } };
    struct elf_object lib = { lbuf, lsz, { 0 } };

    CHECK_EQ(elf_parse_dynamic(&app), 0);
    CHECK_EQ(elf_parse_dynamic(&lib), 0);
    CHECK(app.info.has_dynamic);
    CHECK(lib.info.has_dynamic);
    CHECK(lib.info.dynsym_count > 0);

    /* Scope is built from the *parsed* objects (info is filled by
     * elf_parse_dynamic). Kernel does the same: parse all images, then link. */
    struct elf_object scope[2] = { app, lib };

    /* DT_NEEDED: the executable must name exactly its dependency. */
    CHECK_EQ(app.info.nneeded, 1);
    CHECK_STR((const char *)app.buf + app.info.dynstr_off +
                  app.info.needed_off[0],
              "dynlink_fixture_lib.sl");

    /* Function symbol: found in the scope, and its value must match an
     * independent raw dynsym lookup inside the shared object. */
    int found = 0;
    uint64_t mul = elf_resolve_symbol(scope, 2, (const uint8_t *)"fixture_mul2",
                                      &found);
    CHECK(found);
    CHECK(mul != 0);
    {
        uint64_t indep = 0;
        const struct elf64_sym *tab =
            (const struct elf64_sym *)(lib.buf + lib.info.dynsym_off);
        const uint8_t *str = lib.buf + lib.info.dynstr_off;
        for (size_t i = 1; i < lib.info.dynsym_count; i++) {
            if (tab[i].st_shndx == SHN_UNDEF)
                continue;
            if (tab[i].st_name >= lib.info.dynstr_size)
                continue;
            if (test_str_eq(
                    (const char *)(str + tab[i].st_name), "fixture_mul2")) {
                indep = tab[i].st_value;
                break;
            }
        }
        CHECK(indep != 0);
        CHECK_EQ(mul, indep); /* bias is 0: resolve == st_value */
    }

    /* Data symbol and a symbol only the executable defines. */
    found = 0;
    CHECK(elf_resolve_symbol(scope, 2, (const uint8_t *)"fixture_global",
                             &found) != 0);
    CHECK(found);

    /* A PIE's own private globals (like main) live in .symtab/.strtab, not
     * .dynsym, so the dynamic linker must NOT resolve them: it only binds what
     * is exported through the dynamic symbol table. Asserting the negative
     * here pins down that contract. */
    found = 1;
    CHECK_EQ(elf_resolve_symbol(scope, 2, (const uint8_t *)"main", &found), 0);
    CHECK(!found);

    /* Missing symbols report not-found (and never resolve to address 0). */
    found = 1;
    CHECK_EQ(elf_resolve_symbol(scope, 2,
                                (const uint8_t *)"fixture_nope_zzz", &found),
             0);
    CHECK(!found);

    /* Resolution must keep searching past the executable into deps when the
     * symbol is not defined by the executable itself. */
    {
        struct elf_object one = lib;
        int f = 0;
        uint64_t only_lib =
            elf_resolve_symbol(&one, 1, (const uint8_t *)"fixture_mul2", &f);
        found = 0;
        uint64_t via_scope =
            elf_resolve_symbol(scope, 2, (const uint8_t *)"fixture_mul2",
                               &found);
        CHECK(f);
        CHECK(found);
        CHECK_EQ(via_scope, only_lib);
    }

    /* Full relocation pass. Every reported relocation must be applied, each
     * write must stay inside the image, and the counts must agree with an
     * independent table walk. */
    {
        int applied = -1;
        g_place_count = 0;
        g_place_out_of_span = 0;
        CHECK_EQ(elf_apply_relocations(&lib, scope, 2, place_cb, &lib,
                                       &applied),
                 0);
        CHECK(applied >= 0);
        CHECK_EQ(g_place_count, applied);
        CHECK_EQ(g_place_count, count_nonzero_relocs(&lib));
        CHECK_EQ(g_place_out_of_span, 0);
    }
    {
        int applied = -1;
        g_place_count = 0;
        g_place_out_of_span = 0;
        CHECK_EQ(elf_apply_relocations(&app, scope, 2, place_cb, &app,
                                       &applied),
                 0);
        CHECK(applied > 0);
        CHECK_EQ(g_place_count, applied);
        CHECK_EQ(g_place_count, count_nonzero_relocs(&app));
        CHECK_EQ(g_place_out_of_span, 0);
    }

    /* Without its dependency in scope, eager binding must fail loudly. */
    {
        int applied = -1;
        g_place_count = 0;
        CHECK_EQ(elf_apply_relocations(&app, &app, 1, place_cb, &app,
                                       &applied),
                 -1);
    }

    TEST_REPORT("dynlink");
}