/*
 * adc.h - ADC0 muc thanh ghi: quet nhieu kenh bang ngat (khong chan CPU)
 *
 * Adc_StartScan() kich kenh dau tien; moi lan COCO, ISR luu ket qua va kich
 * kenh tiep theo. Quet xong -> goi callback (neu co) trong ngu canh ngat.
 */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>
#include <stdbool.h>
#include "../status.h"

#define ADC_MAX_CHANNELS   8u

typedef void (*adc_scan_cb_t)(const uint16_t *raw, uint32_t count);

status_t Adc_Init(const uint8_t *channels, uint32_t count, adc_scan_cb_t cb);
status_t Adc_StartScan(void);         /* STATUS_ERR_BUSY neu lan quet truoc chua xong */
bool     Adc_IsBusy(void);
uint32_t Adc_GetOverrunCount(void);   /* So lan bi kich khi dang ban */

#endif /* ADC_H */
