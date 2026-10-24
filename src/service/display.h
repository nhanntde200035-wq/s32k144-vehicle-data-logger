/*
 * display.h - Man hinh trang thai tren TFT: ngay gio, REC/IDLE, nhiet do, %, VDD, so ban ghi
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include "../status.h"

status_t Display_Init(void);     /* Goi sau Tft_Init */
void     Display_Task(void);     /* Moi lan goi ve toi da 1 dong da thay doi */

#endif /* DISPLAY_H */
