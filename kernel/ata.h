/* ata.h — ATA PIO driver for the primary master */
#ifndef ATA_H
#define ATA_H

#include <stdbool.h>
#include <stdint.h>

struct ata_info {
    bool     present;
    char     model[41];
    char     serial[21];
    uint32_t sectors;       /* LBA28 addressable sectors */
};

bool ata_init(void);
const struct ata_info *ata_info(void);
bool ata_read(uint32_t lba, uint8_t count, void *buf);

#endif
