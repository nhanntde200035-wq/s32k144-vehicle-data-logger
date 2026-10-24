/*
 * st7735.h - TFT 1.8" 128x160 ST7735 qua LPSPI0 (chung bus voi W25Q, CS rieng)
 */
#ifndef ST7735_H
#define ST7735_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

/* Mau RGB565 */
#define TFT_BLACK    0x0000u
#define TFT_WHITE    0xFFFFu
#define TFT_RED      0xF800u
#define TFT_GREEN    0x07E0u
#define TFT_BLUE     0x001Fu
#define TFT_CYAN     0x07FFu
#define TFT_YELLOW   0xFFE0u
#define TFT_ORANGE   0xFD20u
#define TFT_GRAY     0x8410u

#define TFT_CHAR_W   6u        /* 5 cot + 1 cot cach */
#define TFT_CHAR_H   8u        /* 7 dong + 1 dong cach */

status_t Tft_Init(void);       /* Co cho ~300 ms (chi 1 lan khi khoi dong) */
bool     Tft_IsReady(void);
void     Tft_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void     Tft_DrawString(uint16_t x, uint16_t y, const char *s,
                        uint16_t fg, uint16_t bg, uint8_t scale);

#endif /* ST7735_H */
