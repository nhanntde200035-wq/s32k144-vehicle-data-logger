/*
 * st7735.c - Driver ST7735 (module do "1.8 TFT SPI 128*160 V1.1")
 *
 * Chan: CS = PTE0, A0/DC = PTD17 (0 = lenh, 1 = du lieu), RESET = PTD14.
 * SPI : dung chung LPSPI0 (SCK/MOSI) voi Flash W25Q. Moi giao dich TFT:
 *       doi toc do SPI -> 8 MHz, keo CS TFT xuong, ..., tra lai toc do Flash.
 *       Tat ca chay trong super-loop nen khong bao gio tranh chap voi Flash.
 * Ve chu: font 5x7 (cot, bit0 = dong tren), phong to scale lan.
 */
#include <stddef.h>
#include "S32K144.h"
#include "st7735.h"
#include "../mcal/lpspi.h"
#include "../mcal/lpit.h"
#include "../app_cfg.h"

/* ---- Lenh ST7735 ---- */
#define CMD_SWRESET  0x01u
#define CMD_SLPOUT   0x11u
#define CMD_NORON    0x13u
#define CMD_INVOFF   0x20u
#define CMD_DISPON   0x29u
#define CMD_CASET    0x2Au
#define CMD_RASET    0x2Bu
#define CMD_RAMWR    0x2Cu
#define CMD_MADCTL   0x36u
#define CMD_COLMOD   0x3Au
#define CMD_FRMCTR1  0xB1u
#define CMD_FRMCTR2  0xB2u
#define CMD_FRMCTR3  0xB3u
#define CMD_INVCTR   0xB4u
#define CMD_PWCTR1   0xC0u
#define CMD_PWCTR2   0xC1u
#define CMD_PWCTR3   0xC2u
#define CMD_PWCTR4   0xC3u
#define CMD_PWCTR5   0xC4u
#define CMD_VMCTR1   0xC5u
#define CMD_GMCTRP1  0xE0u
#define CMD_GMCTRN1  0xE1u

#define MADCTL_PORTRAIT  0xC0u   /* MY | MX, thu tu RGB (module nay khong dung BGR) */
#define COLMOD_16BIT     0x05u
#define PORT_MUX_GPIO    1u
#define FONT_FIRST       0x20
#define FONT_LAST        0x7E
#define FONT_COLS        5u
#define RESET_PULSE_MS   10u
#define SWRESET_WAIT_MS  150u
#define SLPOUT_WAIT_MS   150u
#define NORON_WAIT_MS    10u
#define DISPON_WAIT_MS   100u

#define TFT_COL_OFFSET   0u      /* Chinh neu man bi lech (module "green tab": 2) */
#define TFT_ROW_OFFSET   0u      /* (module "green tab": 1) */

static bool s_ready;

/* Font 5x7 ASCII 0x20..0x7E */
static const uint8_t s_font[][FONT_COLS] =
{
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},{0x3E,0x41,0x41,0x51,0x32},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},
    {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x00,0x7F,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},{0x41,0x41,0x7F,0x00,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
    {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
    {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x00,0x7F,0x10,0x28,0x44},
    {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
    {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
    {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x02,0x01,0x02,0x04,0x02}
};

/* ---------------------- Muc thap ---------------------------------- */
static void Wait(uint32_t ms)
{
    uint32_t t0 = Tick_GetMs();          /* Chi dung khi khoi dong */
    while ((Tick_GetMs() - t0) < ms)
    {
    }
}

static void Begin(void)
{
    (void)Spi_SetSpeed(CFG_TFT_SCK_HZ);
    PTE->PCOR = (1u << CFG_TFT_CS_PIN);
}

static void End(void)
{
    PTE->PSOR = (1u << CFG_TFT_CS_PIN);
    (void)Spi_SetSpeed(CFG_SPI_SCK_HZ);  /* Tra lai toc do cua Flash */
}

static void DcCommand(void) { PTD->PCOR = (1u << CFG_TFT_DC_PIN); }
static void DcData(void)    { PTD->PSOR = (1u << CFG_TFT_DC_PIN); }

static void WriteCmd(uint8_t cmd, const uint8_t *data, uint32_t len)
{
    Begin();
    DcCommand();
    (void)Spi_Transfer(cmd, NULL);
    if (len != 0u)
    {
        DcData();
        (void)Spi_Write(data, len);
    }
    End();
}

/* Dat cua so ghi va bat dau RAMWR (CS van giu thap de day pixel) */
static void SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t ca[4];
    uint8_t ra[4];

    x0 = (uint16_t)(x0 + TFT_COL_OFFSET); x1 = (uint16_t)(x1 + TFT_COL_OFFSET);
    y0 = (uint16_t)(y0 + TFT_ROW_OFFSET); y1 = (uint16_t)(y1 + TFT_ROW_OFFSET);
    ca[0] = (uint8_t)(x0 >> 8); ca[1] = (uint8_t)x0; ca[2] = (uint8_t)(x1 >> 8); ca[3] = (uint8_t)x1;
    ra[0] = (uint8_t)(y0 >> 8); ra[1] = (uint8_t)y0; ra[2] = (uint8_t)(y1 >> 8); ra[3] = (uint8_t)y1;

    DcCommand(); (void)Spi_Transfer(CMD_CASET, NULL);
    DcData();    (void)Spi_Write(ca, sizeof(ca));
    DcCommand(); (void)Spi_Transfer(CMD_RASET, NULL);
    DcData();    (void)Spi_Write(ra, sizeof(ra));
    DcCommand(); (void)Spi_Transfer(CMD_RAMWR, NULL);
    DcData();
}

static void PushColor(uint16_t c)
{
    (void)Spi_Transfer((uint8_t)(c >> 8), NULL);
    (void)Spi_Transfer((uint8_t)c, NULL);
}

/* ---------------------- Khoi tao ---------------------------------- */
status_t Tft_Init(void)
{
    static const uint8_t frmctr[]  = { 0x01u, 0x2Cu, 0x2Du };
    static const uint8_t frmctr3[] = { 0x01u, 0x2Cu, 0x2Du, 0x01u, 0x2Cu, 0x2Du };
    static const uint8_t invctr[]  = { 0x07u };
    static const uint8_t pwctr1[]  = { 0xA2u, 0x02u, 0x84u };
    static const uint8_t pwctr2[]  = { 0xC5u };
    static const uint8_t pwctr3[]  = { 0x0Au, 0x00u };
    static const uint8_t pwctr4[]  = { 0x8Au, 0x2Au };
    static const uint8_t pwctr5[]  = { 0x8Au, 0xEEu };
    static const uint8_t vmctr1[]  = { 0x0Eu };
    static const uint8_t madctl[]  = { MADCTL_PORTRAIT };
    static const uint8_t colmod[]  = { COLMOD_16BIT };
    static const uint8_t gamp[]    = { 0x02u, 0x1Cu, 0x07u, 0x12u, 0x37u, 0x32u, 0x29u, 0x2Du,
                                       0x29u, 0x25u, 0x2Bu, 0x39u, 0x00u, 0x01u, 0x03u, 0x10u };
    static const uint8_t gamn[]    = { 0x03u, 0x1Du, 0x07u, 0x06u, 0x2Eu, 0x2Cu, 0x29u, 0x2Du,
                                       0x2Eu, 0x2Eu, 0x37u, 0x3Fu, 0x00u, 0x00u, 0x02u, 0x10u };

    /* GPIO: CS (PTE0), DC (PTD17), RST (PTD14) - ra, muc cao */
    PCC->PCCn[PCC_PORTE_INDEX] |= PCC_PCCn_CGC_MASK;
    PCC->PCCn[PCC_PORTD_INDEX] |= PCC_PCCn_CGC_MASK;
    PORTE->PCR[CFG_TFT_CS_PIN]  = PORT_PCR_MUX(PORT_MUX_GPIO);
    PORTD->PCR[CFG_TFT_DC_PIN]  = PORT_PCR_MUX(PORT_MUX_GPIO);
    PORTD->PCR[CFG_TFT_RST_PIN] = PORT_PCR_MUX(PORT_MUX_GPIO);
    PTE->PSOR  = (1u << CFG_TFT_CS_PIN);
    PTE->PDDR |= (1u << CFG_TFT_CS_PIN);
    PTD->PSOR  = (1u << CFG_TFT_DC_PIN) | (1u << CFG_TFT_RST_PIN);
    PTD->PDDR |= (1u << CFG_TFT_DC_PIN) | (1u << CFG_TFT_RST_PIN);

    /* Reset cung */
    PTD->PCOR = (1u << CFG_TFT_RST_PIN);
    Wait(RESET_PULSE_MS);
    PTD->PSOR = (1u << CFG_TFT_RST_PIN);
    Wait(RESET_PULSE_MS);

    WriteCmd(CMD_SWRESET, NULL, 0u);  Wait(SWRESET_WAIT_MS);
    WriteCmd(CMD_SLPOUT,  NULL, 0u);  Wait(SLPOUT_WAIT_MS);
    WriteCmd(CMD_FRMCTR1, frmctr,  sizeof(frmctr));
    WriteCmd(CMD_FRMCTR2, frmctr,  sizeof(frmctr));
    WriteCmd(CMD_FRMCTR3, frmctr3, sizeof(frmctr3));
    WriteCmd(CMD_INVCTR,  invctr,  sizeof(invctr));
    WriteCmd(CMD_PWCTR1,  pwctr1,  sizeof(pwctr1));
    WriteCmd(CMD_PWCTR2,  pwctr2,  sizeof(pwctr2));
    WriteCmd(CMD_PWCTR3,  pwctr3,  sizeof(pwctr3));
    WriteCmd(CMD_PWCTR4,  pwctr4,  sizeof(pwctr4));
    WriteCmd(CMD_PWCTR5,  pwctr5,  sizeof(pwctr5));
    WriteCmd(CMD_VMCTR1,  vmctr1,  sizeof(vmctr1));
    WriteCmd(CMD_INVOFF,  NULL, 0u);
    WriteCmd(CMD_MADCTL,  madctl,  sizeof(madctl));
    WriteCmd(CMD_COLMOD,  colmod,  sizeof(colmod));
    WriteCmd(CMD_GMCTRP1, gamp,    sizeof(gamp));
    WriteCmd(CMD_GMCTRN1, gamn,    sizeof(gamn));
    WriteCmd(CMD_NORON,   NULL, 0u);  Wait(NORON_WAIT_MS);
    WriteCmd(CMD_DISPON,  NULL, 0u);  Wait(DISPON_WAIT_MS);

    s_ready = true;
    Tft_FillRect(0u, 0u, CFG_TFT_WIDTH, CFG_TFT_HEIGHT, TFT_BLACK);
    return STATUS_OK;
}

bool Tft_IsReady(void)
{
    return s_ready;
}

/* ---------------------- Ve ---------------------------------------- */
void Tft_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint32_t n;

    if ((!s_ready) || (w == 0u) || (h == 0u) || (x >= CFG_TFT_WIDTH) || (y >= CFG_TFT_HEIGHT))
    {
        return;
    }
    if ((x + w) > CFG_TFT_WIDTH)  { w = (uint16_t)(CFG_TFT_WIDTH - x); }
    if ((y + h) > CFG_TFT_HEIGHT) { h = (uint16_t)(CFG_TFT_HEIGHT - y); }

    Begin();
    SetWindow(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
    for (n = (uint32_t)w * h; n > 0u; n--)
    {
        PushColor(color);
    }
    End();
}

static void DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    const uint8_t *glyph;
    uint32_t row;
    uint32_t col;
    uint32_t sy;
    uint32_t sx;
    uint8_t  bits;

    if ((c < FONT_FIRST) || (c > FONT_LAST))
    {
        c = '?';
    }
    glyph = s_font[(uint32_t)(c - FONT_FIRST)];

    SetWindow(x, y, (uint16_t)(x + (TFT_CHAR_W * scale) - 1u),
              (uint16_t)(y + (TFT_CHAR_H * scale) - 1u));
    for (row = 0u; row < TFT_CHAR_H; row++)
    {
        for (sy = 0u; sy < scale; sy++)
        {
            for (col = 0u; col < TFT_CHAR_W; col++)
            {
                bits = (col < FONT_COLS) ? glyph[col] : 0u;
                for (sx = 0u; sx < scale; sx++)
                {
                    PushColor(((bits >> row) & 1u) ? fg : bg);
                }
            }
        }
    }
}

void Tft_DrawString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t step;

    if ((!s_ready) || (s == NULL) || (scale == 0u))
    {
        return;
    }
    step = (uint16_t)(TFT_CHAR_W * scale);
    Begin();
    while ((*s != '\0') && ((x + step) <= CFG_TFT_WIDTH) &&
           ((y + (TFT_CHAR_H * scale)) <= CFG_TFT_HEIGHT))
    {
        DrawChar(x, y, *s, fg, bg, scale);
        x = (uint16_t)(x + step);
        s++;
    }
    End();
}
