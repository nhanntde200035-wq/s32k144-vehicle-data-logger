/*
 * w25q.h - Driver Flash SPI Winbond W25Qxx (W25Q16/32/64/128...)
 */
#ifndef W25Q_H
#define W25Q_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

typedef struct
{
    uint8_t  manuf;          /* 0xEF = Winbond                      */
    uint8_t  type;           /* 0x40 (SPI) / 0x70 (QPI/DTR)         */
    uint8_t  capacity;       /* 0x15..0x18 -> 2^cap byte            */
    uint32_t size_bytes;     /* 0 neu ID khong hop le               */
} w25q_id_t;

typedef struct
{
    bool     found;
    uint8_t  pincfg;         /* 0 = binh thuong, 3 = dao SIN/SOUT   */
    w25q_id_t id;
} w25q_probe_t;

status_t W25q_Probe(w25q_probe_t *result);          /* Thu PINCFG 0 va 3, doc JEDEC ID */
void     W25q_GetLastProbe(w25q_probe_t *result);
status_t W25q_ReadId(w25q_id_t *id);
status_t W25q_ReadStatus(uint8_t *sr1);
bool     W25q_IsBusy(void);
status_t W25q_WaitReady(uint32_t timeout_ms);
status_t W25q_EraseSector(uint32_t addr);           /* Cho xong (toi da 1 s)     */
status_t W25q_EraseSectorStart(uint32_t addr);      /* Khong cho - dung cho logger */
status_t W25q_EraseChipStart(void);                 /* Khong cho                 */
status_t W25q_ProgramPage(uint32_t addr, const uint8_t *data, uint32_t len);   /* Cho xong */
status_t W25q_ProgramStart(uint32_t addr, const uint8_t *data, uint32_t len);  /* Khong cho */
status_t W25q_Read(uint32_t addr, uint8_t *data, uint32_t len);
uint32_t W25q_GetSize(void);
const char *W25q_GetName(void);     /* Dung luong, vd "64Mbit" */
const char *W25q_GetVendor(void);   /* Hang, vd "Winbond", "Puya" */

#endif /* W25Q_H */
