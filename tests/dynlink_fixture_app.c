/* PIE executable side of the dynamic-linker regression fixture.  Depends on
 * dynlink_fixture_lib.sl (GLOB_DAT for fixture_global, JUMP_SLOT for
 * fixture_mul2) and defines `main` itself, so kernel/dynlink.c can be
 * exercised against the same relocation shapes the axiome loader handles:
 * symbol resolution, NEEDED discovery, eager binding with zero bias. */

extern int fixture_global;
extern int fixture_mul2(int);

int main(void)
{
    return fixture_mul2(fixture_global);
}