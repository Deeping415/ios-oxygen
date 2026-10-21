/*
 * Minimal text console on the ABL splash framebuffer (24-bit RGB888).
 * Stride = 1080 * 3 = 3240 bytes.
 */
#include "console.h"
#include "font.h"

#define SCALE		3
#define CH_W		(8 * SCALE)
#define CH_H		(16 * SCALE)
#define MARGIN		24
#define LABEL_COLS	8

volatile uint8_t *con_fb = (volatile uint8_t *)FB_BASE;
static uint32_t con_bg;
static int row;

void con_clear(uint32_t bg)
{
	uint8_t b = (bg >> 0) & 0xff;
	uint8_t g = (bg >> 8) & 0xff;
	uint8_t r = (bg >> 16) & 0xff;

	for (int y = 0; y < FB_H; y++) {
		volatile uint8_t *line = con_fb + (uint64_t)y * FB_STRIDE;
		for (int x = 0; x < FB_W; x++) {
			line[x * 3 + 0] = b;
			line[x * 3 + 1] = g;
			line[x * 3 + 2] = r;
		}
	}
	con_bg = bg;
	row = 0;
}

static void put_char(int col, char ch, uint32_t fg)
{
	if (ch < 0x20 || ch > 0x7e)
		ch = '?';
	const unsigned char *g = font8x16[ch - 0x20];
	int x0 = MARGIN + col * CH_W, y0 = MARGIN + row * CH_H;
	if (x0 + CH_W > FB_W || y0 + CH_H > FB_H)
		return;

	uint8_t fg_b = (fg >> 0) & 0xff;
	uint8_t fg_g = (fg >> 8) & 0xff;
	uint8_t fg_r = (fg >> 16) & 0xff;

	uint8_t bg_b = (con_bg >> 0) & 0xff;
	uint8_t bg_g = (con_bg >> 8) & 0xff;
	uint8_t bg_r = (con_bg >> 16) & 0xff;

	for (int gy = 0; gy < 16; gy++) {
		for (int gx = 0; gx < 8; gx++) {
			int on = (g[gy] & (0x80 >> gx)) != 0;
			uint8_t b = on ? fg_b : bg_b;
			uint8_t gr = on ? fg_g : bg_g;
			uint8_t r = on ? fg_r : bg_r;

			for (int sy = 0; sy < SCALE; sy++) {
				volatile uint8_t *p = con_fb + (uint64_t)(y0 + gy * SCALE + sy) * FB_STRIDE
						       + (uint64_t)(x0 + gx * SCALE) * 3;
				for (int sx = 0; sx < SCALE; sx++) {
					p[sx * 3 + 0] = b;
					p[sx * 3 + 1] = gr;
					p[sx * 3 + 2] = r;
				}
			}
		}
	}
}

static int put_str(int col, const char *s, uint32_t fg)
{
	while (*s)
		put_char(col++, *s++, fg);
	return col;
}

static void fmt_hex(char *buf, uint64_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < digits; i++)
		buf[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	buf[digits] = 0;
}

void con_skip(void)
{
	row++;
}

void con_str(const char *label, const char *s)
{
	put_str(0, label, YELLOW);
	put_str(LABEL_COLS, s, WHITE);
	row++;
}

void con_str_color(const char *label, const char *s, uint32_t color)
{
	put_str(0, label, color);
	put_str(LABEL_COLS, s, color);
	row++;
}

void con_hex(const char *label, uint64_t v)
{
	char buf[17];
	fmt_hex(buf, v, 16);
	con_str(label, buf);
}

void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb)
{
	char buf[9];
	int col = put_str(0, label, YELLOW);
	col = col < LABEL_COLS ? LABEL_COLS : col + 1;
	col = put_str(col, a, YELLOW);
	fmt_hex(buf, va, 8);
	col = put_str(col, buf, WHITE);
	if (b) {
		col = put_str(col + 1, b, YELLOW);
		fmt_hex(buf, vb, 8);
		put_str(col, buf, WHITE);
	}
	row++;
}
