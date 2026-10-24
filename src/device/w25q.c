/*
 * w25q.c - W25Qxx qua LPSPI0
 *
 * Lenh dung (datasheet Winbond):
 *   0x9F JEDEC ID         0x05 Read Status-1 (bit0 BUSY, bit1 WEL)
 *   0x06 Write Enable     0x20 Sector Erase 4 KB    0xC7 Chip Erase
 *   0x02 Page Program     0x03 Read Data            0xAB Release Power-down
 * Moi lenh ghi deu can Write Enable truoc; xong thi cho BUSY = 0.
 * Page Program khong duoc vuot ranh gioi page 256 byte.
 */
#include <stddef.h>
#include "w25q.h"
#include "../mcal/lpspi.h"
#include "../mcal/lpit.h"
#include "../app_cfg.h"

#define CMD_JEDEC_ID        0x9Fu
#define CMD_READ_SR1        0x05u
#define CMD_WRITE_ENABLE    0x06u
#define CMD_SECTOR_ERASE    0x20u
#define CMD_CHIP_ERASE      0xC7u
#define CMD_PAGE_PROGRAM    0x02u
#define CMD_READ_DATA       0x03u
#define CMD_RELEASE_PD      0xABu

#define SR1_BUSY            0x01u
#define SR1_WEL             0x02u

#define ID_NONE_LOW         0x00u     /* Bus keo xuong / chua co nguon */
#define ID_NONE_HIGH        0xFFu     /* MISO ho                       */
#define CAP_MIN             0x14u     /* 1 MB  (W25Q80)  */
#define CAP_MAX             0x19u     /* 32 MB (W25Q256) */
#define ADDR_BYTES          3u
#define BYTE_SHIFT_16       16u
#define BYTE_SHIFT_8        8u

static w25q_id_t    s_id;
static w25q_probe_t s_last_probe;

/* ------------------------------------------------------------------ */
static status_t SendAddrCmd(uint8_t cmd, uint32_t addr)
{
    uint8_t buf[1u + ADDR_BYTES];

    buf[0] = cmd;
    buf[1] = (uint8_t)(addr >> BYTE_SHIFT_16);
    buf[2] = (uint8_t)(addr >> BYTE_SHIFT_8);
    buf[3] = (uint8_t)addr;
    return Spi_Write(buf, sizeof(buf));
}

static status_t SimpleCmd(uint8_t cmd)
{
    status_t st;

    Spi_CsLow();
    st = Spi_Transfer(cmd, NULL);
    Spi_CsHigh();
    return st;
}

status_t W25q_ReadId(w25q_id_t *id)
{
    uint8_t  b[3];
    status_t st;

    Spi_CsLow();
    st = Spi_Transfer(CMD_JEDEC_ID, NULL);
    if (st == STATUS_OK)
    {
        st = Spi_Read(b, sizeof(b));
    }
    Spi_CsHigh();
    if (st != STATUS_OK)
    {
        return st;
    }

    id->manuf      = b[0];
    id->type       = b[1];
    id->capacity   = b[2];
    id->size_bytes = 0u;
    /* Chap nhan moi hang tuong thich lenh W25Q (Winbond, GigaDevice, Puya, XTX...) */
    if ((b[0] != ID_NONE_LOW) && (b[0] != ID_NONE_HIGH) &&
        (b[2] >= CAP_MIN) && (b[2] <= CAP_MAX))
    {
        id->size_bytes = 1uL << b[2];
        return STATUS_OK;
    }
    return STATUS_ERR_HW;                       /* Doc duoc nhung khong phai W25Q */
}

status_t W25q_Probe(w25q_probe_t *result)
{
    static const spi_pincfg_t cfgs[] = { SPI_PINCFG_NORMAL, SPI_PINCFG_SWAPPED };
    uint32_t i;
    status_t st;

    result->found = false;
    for (i = 0u; i < (sizeof(cfgs) / sizeof(cfgs[0])); i++)
    {
        st = Spi_Init(CFG_SPI_PROBE_SCK_HZ, cfgs[i]);
        if (st != STATUS_OK)
        {
            return st;
        }
        (void)SimpleCmd(CMD_RELEASE_PD);        /* Danh thuc neu chip dang power-down */
        st = W25q_ReadId(&result->id);
        result->pincfg = (uint8_t)cfgs[i];
        if (st == STATUS_OK)
        {
            result->found = true;
            s_id = result->id;
            s_last_probe = *result;
            return Spi_SetSpeed(CFG_SPI_SCK_HZ);
        }
    }
    s_id.size_bytes = 0u;
    s_last_probe = *result;
    (void)Spi_Init(CFG_SPI_PROBE_SCK_HZ, SPI_PINCFG_NORMAL);
    return STATUS_ERR_HW;
}

void W25q_GetLastProbe(w25q_probe_t *result)
{
    *result = s_last_probe;
}

status_t W25q_ReadStatus(uint8_t *sr1)
{
    status_t st;

    Spi_CsLow();
    st = Spi_Transfer(CMD_READ_SR1, NULL);
    if (st == STATUS_OK)
    {
        st = Spi_Transfer(0xFFu, sr1);
    }
    Spi_CsHigh();
    return st;
}

bool W25q_IsBusy(void)
{
    uint8_t sr1 = SR1_BUSY;

    if (W25q_ReadStatus(&sr1) != STATUS_OK)
    {
        return true;
    }
    return ((sr1 & SR1_BUSY) != 0u);
}

status_t W25q_WaitReady(uint32_t timeout_ms)
{
    uint32_t t0 = Tick_GetMs();

    while (W25q_IsBusy())
    {
        if ((Tick_GetMs() - t0) > timeout_ms)
        {
            return STATUS_ERR_TIMEOUT;
        }
    }
    return STATUS_OK;
}

static status_t WriteEnable(void)
{
    uint8_t  sr1 = 0u;
    status_t st  = SimpleCmd(CMD_WRITE_ENABLE);

    if (st != STATUS_OK)
    {
        return st;
    }
    st = W25q_ReadStatus(&sr1);
    if ((st == STATUS_OK) && ((sr1 & SR1_WEL) == 0u))
    {
        return STATUS_ERR_HW;                   /* WEL khong len: WP dang khoa? */
    }
    return st;
}

static bool AddrValid(uint32_t addr, uint32_t len)
{
    return (s_id.size_bytes != 0u) && (addr < s_id.size_bytes) &&
           (len <= (s_id.size_bytes - addr));
}

/* ------------------------------------------------------------------ */
status_t W25q_EraseSectorStart(uint32_t addr)
{
    status_t st;

    if (!AddrValid(addr, CFG_FLASH_SECTOR_SIZE) || ((addr % CFG_FLASH_SECTOR_SIZE) != 0u))
    {
        return STATUS_ERR_PARAM;
    }
    if (W25q_IsBusy())
    {
        return STATUS_ERR_BUSY;
    }
    st = WriteEnable();
    if (st != STATUS_OK)
    {
        return st;
    }
    Spi_CsLow();
    st = SendAddrCmd(CMD_SECTOR_ERASE, addr);
    Spi_CsHigh();
    return st;
}

status_t W25q_EraseSector(uint32_t addr)
{
    status_t st = W25q_EraseSectorStart(addr);

    return (st == STATUS_OK) ? W25q_WaitReady(CFG_FLASH_TIMEOUT_SECTOR_MS) : st;
}

status_t W25q_EraseChipStart(void)
{
    status_t st;

    if (s_id.size_bytes == 0u)
    {
        return STATUS_ERR_HW;
    }
    if (W25q_IsBusy())
    {
        return STATUS_ERR_BUSY;
    }
    st = WriteEnable();
    return (st == STATUS_OK) ? SimpleCmd(CMD_CHIP_ERASE) : st;
}

status_t W25q_ProgramPage(uint32_t addr, const uint8_t *data, uint32_t len)
{
    status_t st;

    if ((data == NULL) || (len == 0u) || (!AddrValid(addr, len)) ||
        (((addr % CFG_FLASH_PAGE_SIZE) + len) > CFG_FLASH_PAGE_SIZE))
    {
        return STATUS_ERR_PARAM;                 /* Khong duoc vuot ranh gioi page */
    }
    st = W25q_WaitReady(CFG_FLASH_TIMEOUT_PAGE_MS);
    if (st != STATUS_OK)
    {
        return st;
    }
    st = WriteEnable();
    if (st != STATUS_OK)
    {
        return st;
    }
    Spi_CsLow();
    st = SendAddrCmd(CMD_PAGE_PROGRAM, addr);
    if (st == STATUS_OK)
    {
        st = Spi_Write(data, len);
    }
    Spi_CsHigh();
    if (st != STATUS_OK)
    {
        return st;
    }
    return W25q_WaitReady(CFG_FLASH_TIMEOUT_PAGE_MS);
}

/* Ra lenh ghi roi tra ve ngay (khong cho BUSY) - dung cho logger khong chan */
status_t W25q_ProgramStart(uint32_t addr, const uint8_t *data, uint32_t len)
{
    status_t st;

    if ((data == NULL) || (len == 0u) || (!AddrValid(addr, len)) ||
        (((addr % CFG_FLASH_PAGE_SIZE) + len) > CFG_FLASH_PAGE_SIZE))
    {
        return STATUS_ERR_PARAM;
    }
    if (W25q_IsBusy())
    {
        return STATUS_ERR_BUSY;
    }
    st = WriteEnable();
    if (st != STATUS_OK)
    {
        return st;
    }
    Spi_CsLow();
    st = SendAddrCmd(CMD_PAGE_PROGRAM, addr);
    if (st == STATUS_OK)
    {
        st = Spi_Write(data, len);
    }
    Spi_CsHigh();
    return st;
}

status_t W25q_Read(uint32_t addr, uint8_t *data, uint32_t len)
{
    status_t st;

    if ((data == NULL) || (!AddrValid(addr, len)))
    {
        return STATUS_ERR_PARAM;
    }
    if (W25q_IsBusy())
    {
        return STATUS_ERR_BUSY;
    }
    Spi_CsLow();
    st = SendAddrCmd(CMD_READ_DATA, addr);
    if (st == STATUS_OK)
    {
        st = Spi_Read(data, len);
    }
    Spi_CsHigh();
    return st;
}

uint32_t W25q_GetSize(void)
{
    return s_id.size_bytes;
}

const char *W25q_GetVendor(void)
{
    switch (s_id.manuf)
    {
        case 0xEFu: return "Winbond";
        case 0xC8u: return "GigaDevice";
        case 0x85u: return "Puya";
        case 0x0Bu: return "XTX";
        case 0x68u: return "Boya";
        case 0x20u: return "XMC/Micron";
        case 0x5Eu: return "Zbit";
        case 0x1Cu: return "EON";
        case 0x9Du: return "ISSI";
        default:    return "Khac";
    }
}

const char *W25q_GetName(void)
{
    switch (s_id.capacity)
    {
        case 0x14u: return "8Mbit";
        case 0x15u: return "16Mbit";
        case 0x16u: return "32Mbit";
        case 0x17u: return "64Mbit";
        case 0x18u: return "128Mbit";
        case 0x19u: return "256Mbit";
        default:    return "UNKNOWN";
    }
}
