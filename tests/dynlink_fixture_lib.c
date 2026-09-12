/* Shared library side of the dynamic-linker regression fixture.  Built as a
 * plain -shared -fPIC object with the host toolchain; kernel/dynlink.c then
 * parses it exactly as the in-kernel loader would (raw buffers, bias 0). */

int fixture_global = 33;

int fixture_mul2(int x)
{
    return x * 2;
}

const char *fixture_name(void)
{
    return "dynlink_fixture";
}