#pragma once
#include <stdint.h>

#define FB_BASE		0x90001000UL	/* continuous splash framebuffer for Vince */
#define FB_W		1080
#define FB_H		2160
#define FB_STRIDE	(FB_W * 3)	/* 3240 bytes, RGB888 24-bpp */

#define RED		0x00ff0000U
#define BLUE		0x000000a0U
#define WHITE		0x00ffffffU
#define YELLOW		0x00ffff00U
#define GREEN		0x0000ff00U

extern volatile uint8_t *con_fb;

void con_clear(uint32_t bg);
void con_skip(void);
void con_str(const char *label, const char *s);
void con_str_color(const char *label, const char *s, uint32_t color);
void con_hex(const char *label, uint64_t v);
/* label, then "a=xxxxxxxx b=xxxxxxxx" style pairs of 32-bit values */
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);
