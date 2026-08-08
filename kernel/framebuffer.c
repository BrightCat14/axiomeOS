#include "framebuffer.h"
#include "font8x16.h"
#include "arch_mmu.h"
#include "pmm.h"
#include <stddef.h>

static struct {
    volatile uint8_t *addr;
    uint8_t *back_buffer;
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
    int dirty;
    uint32_t dirty_x;
    uint32_t dirty_y;
    uint32_t dirty_w;
    uint32_t dirty_h;
} fb;

static inline uint8_t *fb_ptr(void)
{
    return fb.back_buffer ? fb.back_buffer : (uint8_t *)fb.addr;
}

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
    fb.back_buffer = 0;
    fb.present = (addr != 0 && width > 0 && height > 0);
    fb.num_cols = width / FONT_WIDTH;
    fb.num_rows = height / FONT_HEIGHT;
    fb.dirty = 0;
    fb.dirty_w = 0;

    if (fb.present)
    {
        fb.addr = (volatile uint8_t *)mmu_map_framebuffer(
            addr, (size_t)height * pitch);
        __builtin_memset((void *)fb.addr, 0, (size_t)height * pitch);
    }
}

void fb_init_buffers(void)
{
    if (!fb.present)
        return;
    if (fb.back_buffer)
        return;

    size_t fb_size = (size_t)fb.pitch * fb.height;
    size_t num_frames = (fb_size + 0xFFF) >> 12;
    void *phys = pmm_alloc_frames(num_frames);
    if (!phys)
        return;

    fb.back_buffer = (uint8_t *)(uintptr_t)phys;
    __builtin_memset(fb.back_buffer, 0, fb_size);
}

int fb_active(void)
{
    return fb.present;
}

uint32_t fb_width(void)  { return fb.width; }
uint32_t fb_height(void) { return fb.height; }
uint32_t fb_pitch(void)  { return fb.pitch; }
volatile void *fb_addr(void) { return fb.addr; }

static void mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0)
        return;

    uint32_t new_end_x = x + w;
    uint32_t new_end_y = y + h;

    if (fb.dirty_w == 0)
    {
        fb.dirty_x = x;
        fb.dirty_y = y;
        fb.dirty_w = w;
        fb.dirty_h = h;
    }
    else
    {
        if (x < fb.dirty_x) fb.dirty_x = x;
        if (y < fb.dirty_y) fb.dirty_y = y;
        if (new_end_x > fb.dirty_x + fb.dirty_w)
            fb.dirty_w = new_end_x - fb.dirty_x;
        if (new_end_y > fb.dirty_y + fb.dirty_h)
            fb.dirty_h = new_end_y - fb.dirty_y;
    }
    fb.dirty = 1;
}

static void set_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= fb.width || y >= fb.height)
        return;

    uint8_t *p = fb_ptr() + y * fb.pitch + x * (fb.bpp / 8);
    p[0] = (uint8_t)(color >> 0);
    p[1] = (uint8_t)(color >> 8);
    p[2] = (uint8_t)(color >> 16);
    p[3] = (uint8_t)(color >> 24);
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

    mark_dirty(base_x, base_y, FONT_WIDTH, FONT_HEIGHT);
}

static void fill_row(uint32_t row, uint32_t color)
{
    if (row >= fb.num_rows)
        return;
    uint8_t *ptr = fb_ptr();
    for (uint32_t y = row * FONT_HEIGHT; y < (row + 1) * FONT_HEIGHT; y++)
    {
        uint32_t *line = (uint32_t *)(ptr + y * fb.pitch);
        for (uint32_t x = 0; x < fb.width; x++)
            line[x] = color;
    }
    mark_dirty(0, row * FONT_HEIGHT, fb.width, FONT_HEIGHT);
}

static void shift_rows_up(void)
{
    uint8_t *ptr = fb_ptr();
    for (uint32_t y = 0; y < (fb.num_rows - 1) * FONT_HEIGHT; y++)
    {
        __builtin_memcpy(ptr + y * fb.pitch, ptr + (y + FONT_HEIGHT) * fb.pitch, fb.pitch);
    }
    fill_row(fb.num_rows - 1, fb.bg_color);
    mark_dirty(0, 0, fb.width, fb.height - FONT_HEIGHT);
}

void fb_flush(void)
{
    if (!fb.present || !fb.back_buffer || !fb.dirty)
        return;

    size_t sz = (size_t)fb.pitch * fb.height;
    __builtin_memcpy((void *)fb.addr, fb.back_buffer, sz);

    fb.dirty = 0;
    fb.dirty_w = 0;
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
    if (!fb.present || !s)
        return;
    if (!*s)
        return;
    while (*s)
        fb_putchar(*s++);
    if (fb.back_buffer)
        fb_flush();
}

void fb_scroll(void)
{
    if (!fb.present)
        return;
    shift_rows_up();
    fb.cursor_y = fb.num_rows - 1;
    if (fb.back_buffer)
        fb_flush();
}

void fb_clear(void)
{
    if (!fb.present)
        return;
    uint8_t *ptr = fb_ptr();
    size_t size = (size_t)fb.pitch * fb.height;
    __builtin_memset(ptr, 0, size);
    fb.cursor_x = 0;
    fb.cursor_y = 0;
    mark_dirty(0, 0, fb.width, fb.height);
    if (fb.back_buffer)
        fb_flush();
}

void fb_set_color(uint32_t fg, uint32_t bg)
{
    fb.fg_color = fg;
    fb.bg_color = bg;
}

void fb_get_color(uint32_t *fg, uint32_t *bg)
{
    if (fg) *fg = fb.fg_color;
    if (bg) *bg = fb.bg_color;
}

void fb_set_cursor(uint32_t x, uint32_t y)
{
    if (x < fb.num_cols) fb.cursor_x = x;
    if (y < fb.num_rows) fb.cursor_y = y;
}

void fb_get_cursor(uint32_t *x, uint32_t *y)
{
    if (x) *x = fb.cursor_x;
    if (y) *y = fb.cursor_y;
}

void fb_putchar_at(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg)
{
    if (!fb.present)
        return;
    draw_glyph(c, x, y, fg, bg);
    if (fb.back_buffer)
        fb_flush();
}
