#ifndef AXIOME_FAT32_H
#define AXIOME_FAT32_H

/* Probe IDE drives, find the first FAT32 partition, and mount it at /boot.
    Safe to call after vfs_init() has mounted the root ramfs. Prints status. */
void fat32_automount(void);

/* Mount the FAT32 partition `part` (0 = whole-device superfloppy) of IDE
   (bus,drive) at `mp`. Returns 0 on success, -1 on failure. */
int fat32_mount_part(int bus, int drive, int part, const char *mp);

#endif
