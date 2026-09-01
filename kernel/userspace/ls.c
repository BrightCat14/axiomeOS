#include "stdio.h"
#include "syscall.h"
#include "string.h"
#include "stdlib.h"

#define MAXE 1024

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : ".";
    struct vfs_dirent ents[MAXE];
    int n = readdir(path, ents, MAXE);
    if (n < 0) {
        printf("ls: cannot access '%s'\n", path);
        sys_exit(1);
        return 1;
    }
    
    for (int i = 0; i < n && i < MAXE; i++) {
        char typ = (ents[i].type == DT_DIR) ? 'd' : 'f';
        /* Skip . and .. for cleaner output */
        if (ents[i].name[0] == '.' && 
            (ents[i].name[1] == 0 || (ents[i].name[1] == '.' && ents[i].name[2] == 0))) {
            continue;
        }
        printf("%c %s\n", typ, ents[i].name);
    }
    sys_exit(0);
    return 0;
}
