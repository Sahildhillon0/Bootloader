/* fat12.c — read-only FAT12 driver (root directory only)
 *
 * Same on-disk logic as boot/fat12.inc, now in C on top of ATA PIO:
 * the BPB in sector 0 gives the layout, the root directory holds 32-byte
 * entries, and the FAT is a chain of 12-bit cluster numbers.
 */
#include "ata.h"
#include "fat12.h"
#include "string.h"

#define ATTR_VOLUME 0x08
#define ATTR_DIR    0x10
#define ATTR_LFN    0x0F

struct bpb {
    uint8_t  jump[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors;
    uint8_t  media;
    uint16_t sectors_per_fat;
} __attribute__((packed));

struct dirent {
    char     name[11];
    uint8_t  attr;
    uint8_t  reserved[14];
    uint16_t cluster;
    uint32_t size;
} __attribute__((packed));

#define MAX_FAT_SECTORS  12
#define MAX_ROOT_SECTORS 32

static uint8_t  fat[MAX_FAT_SECTORS * 512];
static uint8_t  root[MAX_ROOT_SECTORS * 512];
static uint16_t root_entries;
static uint32_t data_lba;
static uint8_t  sectors_per_cluster;
static bool     mounted;

bool fat12_mount(void)
{
    uint8_t sector[512];

    if (!ata_read(0, 1, sector))
        return false;
    const struct bpb *b = (const struct bpb *)sector;
    if (sector[510] != 0x55 || sector[511] != 0xAA ||
        b->bytes_per_sector != 512 || b->sectors_per_cluster == 0 ||
        b->num_fats == 0 || b->sectors_per_fat > MAX_FAT_SECTORS)
        return false;

    uint32_t root_lba  = b->reserved_sectors + b->num_fats * b->sectors_per_fat;
    uint32_t root_secs = (b->root_entries * 32 + 511) / 512;
    if (root_secs > MAX_ROOT_SECTORS)
        return false;

    root_entries        = b->root_entries;
    sectors_per_cluster = b->sectors_per_cluster;
    data_lba            = root_lba + root_secs;

    if (!ata_read(b->reserved_sectors, b->sectors_per_fat, fat) ||
        !ata_read(root_lba, root_secs, root))
        return false;
    mounted = true;
    return true;
}

static uint16_t fat_next(uint16_t cluster)
{
    uint16_t v = fat[cluster + cluster / 2] | fat[cluster + cluster / 2 + 1] << 8;
    return cluster & 1 ? v >> 4 : v & 0x0FFF;
}

/* "HELLO   TXT" -> "HELLO.TXT" */
static void name_from_83(char *dst, const char *src)
{
    int n = 0;
    for (int i = 0; i < 8 && src[i] != ' '; i++)
        dst[n++] = src[i];
    if (src[8] != ' ') {
        dst[n++] = '.';
        for (int i = 8; i < 11 && src[i] != ' '; i++)
            dst[n++] = src[i];
    }
    dst[n] = 0;
}

/* "hello.txt" -> "HELLO   TXT"; false if it cannot be an 8.3 name */
static bool name_to_83(char *dst, const char *src)
{
    memset(dst, ' ', 11);
    int i = 0, limit = 8;
    for (; *src; src++) {
        char c = *src;
        if (c == '.') {
            if (limit == 11)
                return false;
            i = 8;
            limit = 11;
            continue;
        }
        if (i >= limit)
            return false;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        dst[i++] = c;
    }
    return true;
}

static const struct dirent *entry(int i)
{
    return (const struct dirent *)(root + i * 32);
}

static bool is_file(const struct dirent *d)
{
    return (uint8_t)d->name[0] != 0xE5 && d->attr != ATTR_LFN &&
           !(d->attr & (ATTR_VOLUME | ATTR_DIR));
}

void fat12_list(void (*fn)(const struct fat12_dirent *e))
{
    if (!mounted)
        return;
    for (int i = 0; i < root_entries && entry(i)->name[0]; i++) {
        const struct dirent *d = entry(i);
        if (!is_file(d))
            continue;
        struct fat12_dirent e = { .attr = d->attr, .cluster = d->cluster,
                                  .size = d->size };
        name_from_83(e.name, d->name);
        fn(&e);
    }
}

int64_t fat12_read(const char *name, void *buf, uint32_t max)
{
    char want[11];

    if (!mounted || !name_to_83(want, name))
        return -1;

    for (int i = 0; i < root_entries && entry(i)->name[0]; i++) {
        const struct dirent *d = entry(i);
        if (!is_file(d) || memcmp(d->name, want, 11))
            continue;

        uint32_t left = d->size < max ? d->size : max;
        uint32_t done = 0;
        uint8_t  sector[512];
        for (uint16_t c = d->cluster; c >= 2 && c < 0xFF8 && left; c = fat_next(c)) {
            uint32_t lba = data_lba + (uint32_t)(c - 2) * sectors_per_cluster;
            for (int s = 0; s < sectors_per_cluster && left; s++) {
                if (!ata_read(lba + s, 1, sector))
                    return -1;
                uint32_t n = left < 512 ? left : 512;
                memcpy((uint8_t *)buf + done, sector, n);
                done += n;
                left -= n;
            }
        }
        return done;
    }
    return -1;
}
