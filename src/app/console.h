/*
 * console.h - Console lenh qua UART (khong chan) + luong log dinh ky
 *
 * Lenh (khong phan biet hoa/thuong, ket thuc bang Enter):
 *   HELP                  Liet ke lenh
 *   STATUS                Trang thai he thong + thong ke UART
 *   START / STOP          Bat dau / dung phien ghi
 *   SET LOG <ms>          Doi chu ky log (100..60000 ms)
 *   LOG ON | LOG OFF      Bat / tat luong log dinh ky
 *   DUMP                  Xuat du lieu da ghi (giai doan sau)
 *   ERASE                 Xoa bo nho (giai doan sau)
 *   CLR                   Xoa bo dem thong ke UART
 *   TEST <n>              Nhan n byte tho, tra ve CRC32 -> chung minh khong mat byte
 *
 * Phan hoi: "OK ..." hoac "ERR <ma> <mo ta>"
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "../status.h"

status_t Console_Init(void);
void     Console_Poll(void);       /* Goi lien tuc trong super-loop */
void     Console_LogTask(void);    /* Goi lien tuc; tu kiem tra chu ky */

#endif /* CONSOLE_H */
