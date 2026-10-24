/*
 * lpspi.h - LPSPI0 master, mode 0, 8 bit, CS bang GPIO (polling theo byte, co timeout)
 */
#ifndef LPSPI_H
#define LPSPI_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

typedef enum
{
    SPI_PINCFG_NORMAL  = 0u,    /* SIN = vao, SOUT = ra                */
    SPI_PINCFG_SWAPPED = 3u     /* SOUT = vao, SIN = ra (dao 2 day)    */
} spi_pincfg_t;

status_t Spi_Init(uint32_t sck_hz, spi_pincfg_t pincfg);
status_t Spi_SetSpeed(uint32_t sck_hz);
uint32_t Spi_GetSpeed(void);
spi_pincfg_t Spi_GetPinCfg(void);

void     Spi_CsLow(void);
void     Spi_CsHigh(void);
status_t Spi_Transfer(uint8_t tx, uint8_t *rx);
status_t Spi_Write(const uint8_t *data, uint32_t len);
status_t Spi_Read(uint8_t *data, uint32_t len);   /* Gui 0xFF, nhan du lieu */

#endif /* LPSPI_H */
