0  guest
1  user
2  admin
3  system

### guest

minimal account:

* temporary files
* application launching
* public resources only
* no system configuration

analogous to the Windows Guest account.

---

### user

standard user:

* own files
* own processes
* own settings
* standard applications

this is your primary mode.

---

### admin

administrator:

* package installation
* user management
* system config modification
* service management

but without direct access to critical kernel mechanisms.

---

### system

internal role:

* system daemons
* drivers
* device managers
* OS services

analogous to a mix of root and SYSTEM, but not necessarily an interactive account.

---

and on top, you can implement a POSIX compatibility layer:

AxiomeOS security model

┌─────────────┐
│ capabilities │
└──────┬──────┘
│
┌──────▼──────┐
│  4 roles    │
│ guest/user/ │
│ admin/sys   │
└──────┬──────┘
│
┌──────▼──────┐
│ POSIX layer │
│ uid/gid     │
│ rwx bits    │
│ signals     │
│ processes   │
└─────────────┘

meaning POSIX acts as an adapter rather than the foundation.

for example:

uid 0  -> system
uid 1000+ -> user

and permissions:

rwxr-xr-x

simply map to capabilities:

owner write
↓
CAP_FILE_WRITE_SELF

group execute
↓
CAP_EXECUTE_SHARED

this way, legacy software sees a familiar Linux-like world:

open("/etc/config", O_WRONLY);
fork();
execve();

while internally, you are using your own model. It would also be cool to have root exist only within the POSIX layer:

root (uid 0)
| 
v
compatibility account
| 
v
admin role + set of capabilities

In other words, no magical "uid 0 = God" like in classic Unix. Root is just a historical kludge for compatibility
