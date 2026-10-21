/*
 * Minimal freestanding 24-bit RGB888 console for Vince (1080x2160, stride 3240).
 */
#include <stdint.h>
#include "console.h"
#include "font.h"

#define FB_BASE   0x90001000UL
#define FB_W      1080
#define FB_H      2160
#define FB_STRIDE 3240
#define FONT_W    8
#define FONT_H    16
#define SCALE     2

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;

static void put_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b)
{
	if (x >= FB_W || y >= FB_H) return;
	volatile uint8_t *fb = (volatile uint8_t *)FB_BASE;
	uint32_t offset = y * FB_STRIDE + x * 3;
	fb[offset + 0] = b;
	fb[offset + 1] = g;
	fb[offset + 2] = r;
}

void console_init(void)
{
	cursor_x = 0;
	cursor_y = 0;
}

void console_putc(char c)
{
	if (c == '\n') {
		cursor_x = 0;
		cursor_y += FONT_H * SCALE;
		return;
	}
	if (c == '\r') {
		cursor_x = 0;
		return;
	}
	const uint8_t *glyph = &font_8x16[(uint8_t)c * 16];
	for (int row = 0; row < FONT_H; row++) {
		uint8_t bits = glyph[row];
		for (int col = 0; col < FONT_W; col++) {
			int on = (bits >> (7 - col)) & 1;
			uint8_t color = on ? 0xff : 0x00;
			for (int sy = 0; sy < SCALE; sy++) {
				for (int sx = 0; sx < SCALE; sx++) {
					put_pixel(cursor_x + col * SCALE + sx,
					          cursor_y + row * SCALE + sy,
					          color, color, color);
				}
			}
		}
	}
	cursor_x += FONT_W * SCALE;
	if (cursor_x + FONT_W * SCALE > FB_W) {
		cursor_x = 0;
		cursor_y += FONT_H * SCALE;
	}
}

void console_puts(const char *s)
{
	while (*s) console_putc(*s++);
}
