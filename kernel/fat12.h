/* fat12.h — read-only FAT12 over the ATA driver */
#ifndef FAT12_H
#define FAT12_H

#include <stdbool.h>
#include <stdint.h>

struct fat12_dirent {
    char     name[13];      /* "HELLO.TXT" */
    uint8_t  attr;
    uint16_t cluster;
    uint32_t size;
};

bool fat12_mount(void);
/* Calls fn for each file in the root directory */
void fat12_list(void (*fn)(const struct fat12_dirent *e));
/* Returns bytes read (<= max), or -1 if the file does not exist */
int64_t fat12_read(const char *name, void *buf, uint32_t max);

#endif
