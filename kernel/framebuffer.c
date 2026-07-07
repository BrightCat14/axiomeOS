#include "framebuffer.h"
#include "font8x16.h"

extern uint64_t pd_table2[512];

static void map_framebuffer(uintptr_t phys_addr, uintptr_t size)
{
    uintptr_t start = phys_addr & ~(uintptr_t)0x1FFFFF;
    uintptr_t end = phys_addr + size;

    for (uintptr_t p = start; p < end; p += 0x200000)
    {
        unsigned int idx = (p >> 21) & 0x1FF;
        pd_table2[idx] = p | 0x83;
    }

    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax");
}

static struct {
    volatile uint8_t *addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint8_t type;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t num_cols;
    uint32_t num_rows;
    uint32_t fg_color;
    uint32_t bg_color;
    int present;
} fb;

void fb_init(uintptr_t addr, uint32_t width, uint32_t height,
             uint32_t pitch, uint8_t bpp, uint8_t type)
{
    fb.addr = (volatile uint8_t *)addr;
    fb.width = width;
    fb.height = height;
    fb.pitch = pitch;
    fb.bpp = bpp;
    fb.type = type;
    fb.cursor_x = 0;
    fb.cursor_y = 0;
    fb.fg_color = 0x00FFFFFF;
    fb.bg_color = 0x00000000;
    fb.present = (addr != 0 && width > 0 && height > 0);
    fb.num_cols = width / FONT_WIDTH;
    fb.num_rows = height / FONT_HEIGHT;

    if (fb.present)
        map_framebuffer(addr, height * pitch);
}

int fb_active(void)
{
    return fb.present;
}

static void set_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= fb.width || y >= fb.height)
        return;

    uint8_t *p = (uint8_t *)fb.addr + y * fb.pitch + x * (fb.bpp / 8);
    p[0] = (uint8_t)(color >> 0);
    p[1] = (uint8_t)(color >> 8);
    p[2] = (uint8_t)(color >> 16);
}

static void draw_glyph(char c, uint32_t cell_x, uint32_t cell_y, uint32_t fg, uint32_t bg)
{
    if (cell_x >= fb.num_cols || cell_y >= fb.num_rows)
        return;

    unsigned int idx = (unsigned char)c - FONT_FIRST_CHAR;
    if (idx >= FONT_NUM_CHARS)
        return;

    uint32_t base_x = cell_x * FONT_WIDTH;
    uint32_t base_y = cell_y * FONT_HEIGHT;

    for (int row = 0; row < FONT_HEIGHT; row++)
    {
        uint8_t bits = font8x16[idx][row];
        for (int col = 0; col < FONT_WIDTH; col++)
        {
            uint32_t color = (bits & (1 << (7 - col))) ? fg : bg;
            set_pixel(base_x + col, base_y + row, color);
        }
    }
}

static void fill_row(uint32_t row, uint32_t color)
{
    if (row >= fb.num_rows)
        return;
    for (uint32_t y = row * FONT_HEIGHT; y < (row + 1) * FONT_HEIGHT; y++)
    {
        uint8_t *line = (uint8_t *)fb.addr + y * fb.pitch;
        for (uint32_t x = 0; x < fb.width; x++)
        {
            line[x * (fb.bpp / 8) + 0] = (uint8_t)(color >> 0);
            line[x * (fb.bpp / 8) + 1] = (uint8_t)(color >> 8);
            line[x * (fb.bpp / 8) + 2] = (uint8_t)(color >> 16);
        }
    }
}

static void shift_rows_up(void)
{
    for (uint32_t y = 0; y < (fb.num_rows - 1) * FONT_HEIGHT; y++)
    {
        uint8_t *dst = (uint8_t *)fb.addr + y * fb.pitch;
        uint8_t *src = (uint8_t *)fb.addr + (y + FONT_HEIGHT) * fb.pitch;
        for (uint32_t x = 0; x < fb.width * (fb.bpp / 8); x++)
            dst[x] = src[x];
    }
    fill_row(fb.num_rows - 1, fb.bg_color);
}

void fb_putchar(char c)
{
    if (!fb.present)
        return;

    switch (c)
    {
        case '\n':
            fb.cursor_x = 0;
            fb.cursor_y++;
            break;
        case '\r':
            fb.cursor_x = 0;
            break;
        case '\t':
            fb.cursor_x = (fb.cursor_x + 8) & ~7;
            break;
        case '\b':
            if (fb.cursor_x > 0)
                fb.cursor_x--;
            break;
        default:
            draw_glyph(c, fb.cursor_x, fb.cursor_y, fb.fg_color, fb.bg_color);
            fb.cursor_x++;
            break;
    }

    if (fb.cursor_x >= fb.num_cols)
    {
        fb.cursor_x = 0;
        fb.cursor_y++;
    }

    while (fb.cursor_y >= fb.num_rows)
    {
        shift_rows_up();
        fb.cursor_y--;
    }
}

void fb_write(const char *s)
{
    if (!fb.present)
        return;
    while (*s)
        fb_putchar(*s++);
}

void fb_scroll(void)
{
    if (!fb.present)
        return;
    shift_rows_up();
    fb.cursor_y = fb.num_rows - 1;
}

void fb_clear(void)
{
    if (!fb.present)
        return;
    for (uint32_t r = 0; r < fb.num_rows; r++)
        fill_row(r, fb.bg_color);
    fb.cursor_x = 0;
    fb.cursor_y = 0;
}
