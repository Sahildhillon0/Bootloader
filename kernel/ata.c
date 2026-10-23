/* ata.c — ATA PIO (polling) on the primary channel, master drive
 *
 * Task file registers at 0x1F0-0x1F7, device control at 0x3F6. Reads use
 * LBA28: drive/head register = 0xE0 | LBA bits 27..24, then sector count
 * and LBA bits 23..0, then READ SECTORS (0x20). For each sector we wait
 * for BSY to clear and DRQ to set, then pull 256 words from the data port.
 * Interrupts are disabled on the drive (nIEN); we only poll.
 */
#include "ata.h"
#include "io.h"

#define ATA_DATA     0x1F0
#define ATA_ERROR    0x1F1
#define ATA_COUNT    0x1F2
#define ATA_LBA_LO   0x1F3
#define ATA_LBA_MID  0x1F4
#define ATA_LBA_HI   0x1F5
#define ATA_DRIVE    0x1F6
#define ATA_STATUS   0x1F7
#define ATA_CMD      0x1F7
#define ATA_CTRL     0x3F6

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DF   0x20
#define ST_BSY  0x80

#define CMD_READ     0x20
#define CMD_IDENTIFY 0xEC

static struct ata_info info;

/* Reading the alternate status 4 times gives the drive its 400 ns */
static void delay400(void)
{
    for (int i = 0; i < 4; i++)
        inb(ATA_CTRL);
}

static bool wait_not_busy(void)
{
    for (uint32_t i = 0; i < 1000000; i++)
        if (!(inb(ATA_STATUS) & ST_BSY))
            return true;
    return false;
}

static bool wait_drq(void)
{
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t st = inb(ATA_STATUS);
        if (st & (ST_ERR | ST_DF))
            return false;
        if (!(st & ST_BSY) && (st & ST_DRQ))
            return true;
    }
    return false;
}

/* IDENTIFY strings are big-endian words padded with spaces */
static void ata_string(char *dst, const uint16_t *src, int words)
{
    for (int i = 0; i < words; i++) {
        dst[2 * i]     = src[i] >> 8;
        dst[2 * i + 1] = src[i] & 0xFF;
    }
    int n = words * 2;
    dst[n] = 0;
    while (n > 0 && dst[n - 1] == ' ')
        dst[--n] = 0;
}

bool ata_init(void)
{
    uint16_t id[256];

    outb(ATA_CTRL, 0x02);           /* nIEN: no interrupts from the drive */
    outb(ATA_DRIVE, 0xA0);          /* select master */
    delay400();
    outb(ATA_COUNT, 0);
    outb(ATA_LBA_LO, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HI, 0);
    outb(ATA_CMD, CMD_IDENTIFY);

    if (inb(ATA_STATUS) == 0)       /* no drive on the bus */
        return false;
    if (!wait_not_busy())
        return false;
    if (inb(ATA_LBA_MID) || inb(ATA_LBA_HI))
        return false;               /* ATAPI or SATA signature, not ATA */
    if (!wait_drq())
        return false;
    for (int i = 0; i < 256; i++)
        id[i] = inw(ATA_DATA);

    ata_string(info.serial, &id[10], 10);
    ata_string(info.model, &id[27], 20);
    info.sectors = id[60] | (uint32_t)id[61] << 16;
    info.present = true;
    return true;
}

const struct ata_info *ata_info(void)
{
    return &info;
}

bool ata_read(uint32_t lba, uint8_t count, void *buf)
{
    uint16_t *p = buf;

    if (!info.present || count == 0 || lba + count > info.sectors)
        return false;
    if (!wait_not_busy())
        return false;

    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    delay400();
    outb(ATA_COUNT, count);
    outb(ATA_LBA_LO, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HI, (lba >> 16) & 0xFF);
    outb(ATA_CMD, CMD_READ);

    for (int s = 0; s < count; s++) {
        delay400();
        if (!wait_drq())
            return false;
        for (int i = 0; i < 256; i++)
            *p++ = inw(ATA_DATA);
    }
    return true;
}
