/*
 * lpi2c.h - LPI2C0 master muc thanh ghi (polling co timeout), 100/400 kHz
 */
#ifndef LPI2C_H
#define LPI2C_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

typedef struct
{
    uint32_t transfers;
    uint32_t nack;            /* Thiet bi khong tra loi ACK       */
    uint32_t timeout;
    uint32_t arb_lost;
} i2c_stats_t;

status_t I2c_Init(uint32_t speed_khz);
uint32_t I2c_GetSpeedKhz(void);
status_t I2c_Probe(uint8_t addr);                                       /* ACK? */
status_t I2c_WriteReg(uint8_t addr, uint8_t reg, const uint8_t *data, uint32_t len);
status_t I2c_ReadReg(uint8_t addr, uint8_t reg, uint8_t *data, uint32_t len);
void     I2c_GetStats(i2c_stats_t *st);

#endif /* LPI2C_H */
