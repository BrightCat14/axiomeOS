#include "security.h"
#include "sched.h"
#include "printk.h"
#include "slab.h"
#include "string.h"
#include "vfs.h"
#include <stddef.h>

/* ---------------------------------------------------------------- *
 * Static role -> capability table
 * ---------------------------------------------------------------- */
const uint64_t role_caps[NROLES] = {
    [ROLE_GUEST] =
        CAP_FORK |
        CAP_FILE_READ |
        CAP_FILE_WRITE_TMP |
        CAP_EXECUTE,
    [ROLE_USER] =
        CAP_FORK |
        CAP_FILE_READ |
        CAP_FILE_WRITE_SELF |
        CAP_KILL_SELF |
        CAP_NET |
        CAP_IPC |
        CAP_SETUID |
        CAP_SETGID |
        CAP_EXECUTE,
    [ROLE_ADMIN] =
        /* all USER caps ... */
        CAP_FORK |
        CAP_FILE_READ |
        CAP_FILE_WRITE_SELF |
        CAP_KILL_SELF |
        CAP_NET |
        CAP_IPC |
        CAP_SETUID |
        CAP_SETGID |
        CAP_EXECUTE |
        /* ... plus: */
        CAP_FILE_WRITE_ANY |
        CAP_KILL_ANY |
        CAP_SIGNAL_OTHER |
        CAP_MOUNT |
        CAP_UMOUNT |
        CAP_DRIVER |
        CAP_NET_RAW |
        CAP_USER_MGMT |
        CAP_SERVICE_MGMT |
        CAP_SYS_ADMIN |
        CAP_PTRACE |
        CAP_BOOT |
        CAP_TIME |
        CAP_RESOURCE,
    [ROLE_SYSTEM] =
        /* everything -- kernel daemons only */
        0xFFFFFFFFULL,
};

/* ---------------------------------------------------------------- *
 * User database
 * ---------------------------------------------------------------- */
struct user_entry g_users[MAX_USERS];
int g_nusers;

/* Built-in fallback if /etc/passwd is missing (so the system still boots
   with a usable root/admin identity). */
static const struct user_entry g_fallback_users[] = {
    { "root",  0,    0,    ROLE_SYSTEM, "x", "/root",      "/bin/sh" },
    { "system",3,    3,    ROLE_SYSTEM, "x", "/sbin",      "/sbin/nologin" },
    { "alice", 1000, 1000, ROLE_USER,   "x", "/home/alice","/bin/sh" },
    { "guest", 65534,65534, ROLE_GUEST, "x", "/tmp",       "/sbin/nologin" },
};

/* ---------------------------------------------------------------- *
 * Capability check
 * ---------------------------------------------------------------- */
int sec_check_cap(struct thread *t, uint64_t cap)
{
    if (!t)
        return 0;
    if (t->role == ROLE_SYSTEM)
        return 1;
    return (t->caps_eff & cap) != 0;
}

/* ---------------------------------------------------------------- *
 * User DB lookups
 * ---------------------------------------------------------------- */
const struct user_entry *security_lookup_name(const char *name)
{
    for (int i = 0; i < g_nusers; i++)
        if (strcmp(g_users[i].name, name) == 0)
            return &g_users[i];
    return 0;
}

const struct user_entry *security_lookup_uid(uid_t uid)
{
    for (int i = 0; i < g_nusers; i++)
        if (g_users[i].uid == uid)
            return &g_users[i];
    return 0;
}

/* ---------------------------------------------------------------- *
 * Minimal SHA-256 (FIPS 180-4) -- used to verify /etc/passwd hashes
 * ---------------------------------------------------------------- */
static uint32_t sha_rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

static const uint32_t SHA_K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256(const uint8_t *msg, size_t len, uint8_t out[32])
{
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t total = len + 1;
    uint64_t bitlen = (uint64_t)len * 8;
    size_t padded = total + (64 - (total % 64)) % 64;
    if ((padded % 64) == 0 && (total % 64) > 56) padded += 64;
    else if (total % 64 == 0) padded = total + 64;

    uint8_t *buf = kmalloc(padded ? padded : 64);
    if (!buf) { memset(out, 0, 32); return; }
    size_t i;
    for (i = 0; i < len; i++) buf[i] = msg[i];
    buf[i++] = 0x80;
    while (i < padded - 8) buf[i++] = 0;
    for (int j = 0; j < 8; j++)
        buf[padded - 1 - j] = (uint8_t)(bitlen >> (8 * j));

    for (size_t off = 0; off < padded; off += 64)
    {
        uint32_t w[64];
        for (int t = 0; t < 16; t++)
            w[t] = ((uint32_t)buf[off + t*4] << 24) |
                   ((uint32_t)buf[off + t*4+1] << 16) |
                   ((uint32_t)buf[off + t*4+2] << 8) |
                   ((uint32_t)buf[off + t*4+3]);
        for (int t = 16; t < 64; t++)
            w[t] = sha_rotr(w[t-2],17) ^ sha_rotr(w[t-2],19) ^ (w[t-2]>>10) + w[t-7] +
                   (sha_rotr(w[t-15],7) ^ sha_rotr(w[t-15],18) ^ (w[t-15]>>3)) + w[t-16];

        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int t = 0; t < 64; t++)
        {
            uint32_t S1 = sha_rotr(e,6)^sha_rotr(e,11)^sha_rotr(e,25);
            uint32_t ch = (e&f)^((~e)&g);
            uint32_t t1 = hh + S1 + ch + SHA_K[t] + w[t];
            uint32_t S0 = sha_rotr(a,2)^sha_rotr(a,13)^sha_rotr(a,22);
            uint32_t maj = (a&b)^(a&c)^(b&c);
            uint32_t t2 = S0 + maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    kfree(buf);
    for (int i = 0; i < 8; i++)
    {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)(h[i]);
    }
}

static void sha256_hex(const char *msg, char *hex /* 65 bytes */)
{
    uint8_t d[32];
    sha256((const uint8_t *)msg, strlen(msg), d);
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++)
    {
        hex[i*2]   = hx[d[i] >> 4];
        hex[i*2+1] = hx[d[i] & 0xF];
    }
    hex[64] = 0;
}

/* ---------------------------------------------------------------- *
 * Authentication: find user by name, verify password hash.
 * Returns uid on success, -1 on failure.
 * ---------------------------------------------------------------- */
int security_authenticate(const char *username, const char *password)
{
    const struct user_entry *u = security_lookup_name(username);
    if (!u)
        return -1;
    /* 'x' or empty stored hash means "no password set" (shadowed);
       accept any password in this minimal OS. */
    if (u->passwd_hash[0] == 0 || (u->passwd_hash[0] == 'x' && u->passwd_hash[1] == 0))
        return (int)u->uid;
    char hex[65];
    sha256_hex(password, hex);
    if (strcmp(hex, u->passwd_hash) == 0)
        return (int)u->uid;
    return -1;
}

/* tiny atoi (avoid pulling full libc into the kernel) */
static int atoi_simple(const char *s)
{
    int v = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

/* ---------------------------------------------------------------- *
 * Parse a single /etc/passwd line:
 *   username:password_hash:uid:gid:role:home:shell
 * ---------------------------------------------------------------- */
static void parse_passwd_line(const char *line)
{
    char tmp[256];
    size_t i = 0;
    while (line[i] && i < sizeof(tmp) - 1) { tmp[i] = line[i]; i++; }
    tmp[i] = 0;

    char *fields[7];
    int nf = 0;
    char *p = tmp;
    while (*p && nf < 7)
    {
        fields[nf++] = p;
        while (*p && *p != ':') p++;
        if (*p) { *p = 0; p++; }
    }
    if (nf < 7)
        return;

    if (g_nusers >= MAX_USERS)
        return;

    struct user_entry *u = &g_users[g_nusers];
    memset(u, 0, sizeof(*u));
    strncpy(u->name, fields[0], 31);
    strncpy(u->passwd_hash, fields[1], 63);
    u->uid = (uid_t)atoi_simple(fields[2]);
    u->gid = (gid_t)atoi_simple(fields[3]);
    if (strcmp(fields[4], "guest") == 0)       u->role = ROLE_GUEST;
    else if (strcmp(fields[4], "user") == 0)   u->role = ROLE_USER;
    else if (strcmp(fields[4], "admin") == 0)  u->role = ROLE_ADMIN;
    else if (strcmp(fields[4], "system") == 0) u->role = ROLE_SYSTEM;
    else u->role = posix_uid_to_role(u->uid);
    strncpy(u->home, fields[5], 127);
    strncpy(u->shell, fields[6], 63);
    g_nusers++;
}

/* Read /etc/passwd from the (already-mounted) root filesystem and populate
   the in-kernel user database. Falls back to a built-in table if missing. */
void security_init(void)
{
    g_nusers = 0;

    struct thread *t = sched_current();
    const char *cwd = t ? t->cwd : "/";

    struct vnode *n = vfs_lookup("/etc/passwd", cwd);
    if (n && n->type == VFS_FILE)
    {
        char *buf = kmalloc(n->size + 1);
        if (buf)
        {
            size_t got = vfs_read(n, 0, buf, n->size);
            buf[got] = 0;
            size_t pos = 0;
            while (pos < got)
            {
                size_t start = pos;
                while (pos < got && buf[pos] != '\n') pos++;
                buf[pos] = 0;
                if (buf[start])
                    parse_passwd_line(buf + start);
                pos++;
            }
            kfree(buf);
        }
        vfs_release(n);
    }

    if (g_nusers == 0)
    {
        printk("SEC: /etc/passwd not found, using built-in user table\n");
        for (size_t i = 0; i < sizeof(g_fallback_users)/sizeof(g_fallback_users[0]); i++)
            g_users[g_nusers++] = g_fallback_users[i];
    }
    else
    {
        printk("SEC: loaded %d users from /etc/passwd\n", g_nusers);
    }
}
