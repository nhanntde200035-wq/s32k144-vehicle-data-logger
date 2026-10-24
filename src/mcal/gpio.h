/*
 * gpio.h - LED trang thai PTD0 (LED RGB xanh duong tren EVB, tich cuc muc thap)
 */
#ifndef GPIO_H
#define GPIO_H

#include "../status.h"

status_t Led_Init(void);
void     Led_On(void);
void     Led_Off(void);
void     Led_Toggle(void);

#endif /* GPIO_H */
