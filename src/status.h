/*
 * status.h - Ma trang thai dung chung cho moi ham khoi tao / driver
 */
#ifndef STATUS_H
#define STATUS_H

typedef enum
{
    STATUS_OK          = 0,
    STATUS_ERR_PARAM   = 1,   /* Tham so khong hop le            */
    STATUS_ERR_TIMEOUT = 2,   /* Phan cung khong phan hoi        */
    STATUS_ERR_HW      = 3,   /* Phan cung bao loi / sai cau hinh */
    STATUS_ERR_BUSY    = 4,   /* Tai nguyen dang ban             */
    STATUS_ERR_FULL    = 5,   /* Bo dem day                      */
    STATUS_ERR_EMPTY   = 6    /* Bo dem rong                     */
} status_t;

#endif /* STATUS_H */
