#include "security.h"
#include "sched.h"
#include "printk.h"
#include "string.h"

/* ---------------------------------------------------------------- *
 * POSIX adapter: maps between the legacy uid/gid view presented to
 * userspace and the internal capability/role model.
 *
 * NOTE: the spec is internally inconsistent about uid 0. Sections 2,
 * 5.3 and 9.1 map uid 0 to ROLE_SYSTEM, while the headline security
 * model and section 9.4 state that POSIX root (uid 0) is NOT a magic
 * superuser and maps to ROLE_ADMIN with bounded capabilities. We
 * follow the explicit security-model intent here: a *login* as uid 0
 * receives the admin role. The kernel's own init/daemons are instead
 * hard-coded to ROLE_SYSTEM (see kernel.c / sched.c) and never route
 * through this function, so the kernel still boots with full power.
 * ---------------------------------------------------------------- */

user_role_t posix_uid_to_role(uid_t uid)
{
    if (uid == 0)
        return ROLE_ADMIN;          /* POSIX root = admin (bounded caps) */
    if (uid < 1000)
        return ROLE_ADMIN;          /* system accounts (daemon, etc.) */
    if (uid == 65534)
        return ROLE_GUEST;          /* unauthenticated sessions */
    return ROLE_USER;               /* human users */
}

/* Next free uid in a role's range (used by user-management tooling).
   Returns -1 (as uid_t) if none available. */
uid_t posix_role_next_uid(user_role_t role)
{
    uid_t lo = 0, hi = 0;
    switch (role) {
    case ROLE_SYSTEM: lo = 0;   hi = 0;    break;  /* singleton */
    case ROLE_ADMIN:  lo = 1;   hi = 999;  break;
    case ROLE_USER:   lo = 1000;hi = 65533;break;
    case ROLE_GUEST:  lo = 65534; hi = 65534; break;
    default: return (uid_t)-1;
    }
    for (uid_t u = lo; u <= hi; u++)
    {
        int used = 0;
        for (int i = 0; i < g_nusers; i++)
            if (g_users[i].uid == u) { used = 1; break; }
        if (!used)
            return u;
    }
    return (uid_t)-1;
}
