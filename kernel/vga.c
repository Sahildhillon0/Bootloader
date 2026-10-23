/* vga.c — 80x25 VGA text mode driver
 *
 * The text buffer at 0xB8000 holds one 16-bit cell per character:
 * low byte ASCII, high byte attribute (background << 4 | foreground).
 * The hardware cursor is moved through the CRT controller at 0x3D4/0x3D5.
 */
#include "io.h"
#include "vga.h"

#define VGA_COLS 80
#define VGA_ROWS 25

static volatile uint16_t *const vga = (volatile uint16_t *)0xB8000;
static uint8_t cur_row, cur_col;
static uint8_t attr = VGA_LIGHT_GREY | VGA_BLACK << 4;

static void update_cursor(void)
{
    uint16_t pos = cur_row * VGA_COLS + cur_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
    outb(0x3D4, 0x0E);
    outb(0x3D5, pos >> 8);
}

static void scroll(void)
{
    for (int i = 0; i < VGA_COLS * (VGA_ROWS - 1); i++)
        vga[i] = vga[i + VGA_COLS];
    for (int i = VGA_COLS * (VGA_ROWS - 1); i < VGA_COLS * VGA_ROWS; i++)
        vga[i] = ' ' | attr << 8;
    cur_row = VGA_ROWS - 1;
}

/* Continue where the bootloader stopped, so its log stays on screen */
void vga_init(uint8_t row, uint8_t col)
{
    cur_row = row < VGA_ROWS ? row : VGA_ROWS - 1;
    cur_col = col < VGA_COLS ? col : 0;
    update_cursor();
}

void vga_clear(void)
{
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        vga[i] = ' ' | attr << 8;
    cur_row = cur_col = 0;
    update_cursor();
}

void vga_set_color(enum vga_color fg, enum vga_color bg)
{
    attr = fg | bg << 4;
}

void vga_putc(char c)
{
    switch (c) {
    case '\n':
        cur_col = 0;
        cur_row++;
        break;
    case '\r':
        cur_col = 0;
        break;
    case '\b':
        if (cur_col > 0) {
            cur_col--;
            vga[cur_row * VGA_COLS + cur_col] = ' ' | attr << 8;
        }
        break;
    case '\t':
        cur_col = (cur_col + 8) & ~7;
        break;
    default:
        vga[cur_row * VGA_COLS + cur_col] = (uint8_t)c | attr << 8;
        cur_col++;
    }
    if (cur_col >= VGA_COLS) {
        cur_col = 0;
        cur_row++;
    }
    if (cur_row >= VGA_ROWS)
        scroll();
    update_cursor();
}
