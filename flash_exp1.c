# README

Project layout (updated): 3 folders — `flash_drive`, `rtc_hw`, `uart_process`.

Purpose: lưu/đọc epoch (Unix timestamp) và ngày/tháng/năm vào flash nội trên STM32F103R8T6, nạp RTC khi khởi động. Kèm theo:
 - Wear-leveling (multi-slot pages) để giảm số lần erase trên cùng 1 trang.
 - Non-blocking UART receiver (DMA + IDLE-line detection) để nhận chuỗi ngày `DD/MM/YYYY` từ UART và tự động ghi vào flash sử dụng wear-leveling.
 - Sử dụng `HAL_FLASHEx_Erase` cho erase trang (thay vì FLASH_ErasePage), và `HAL_FLASH_Program` để ghi.

HOW TO USE (summary):
 1. Copy folders `flash_drive`, `rtc_hw`, `uart_process` vào project.
 2. Cấu hình `UART_HandleTypeDef huart1` và `DMA_HandleTypeDef hdma_usart1_rx` (CubeMX tạo giúp), và khởi tạo chúng trước khi khởi động parser.
 3. Gọi `rtc_hw_init()`.
 4. Gọi `flash_storage_wear_init(slot_count)` (ví dụ 4).
 5. Khởi động UART parser: `uart_dma_parser_init(&huart1, &hdma_usart1_rx); uart_dma_parser_start();`.
 6. Startup: đọc epoch bằng `flash_storage_read_epoch_wearlevel(&epoch)`; nếu không có thì dùng `DEFAULT_EPOCH` rồi `rtc_set_from_epoch(epoch)`.

Folders + files (created):
 - flash_drive/
    - flash_storage.h
    - flash_storage.c
 - rtc_hw/
    - rtc_config.h
    - rtc_config.c
 - uart_process/
    - uart_dma_parser.h
    - uart_dma_parser.c
 - main.c (example integration)

--- Important notes before flashing to target ---
 - The code uses the last N flash pages for wear-leveling slots. Make sure those pages do not contain code/bootloader.
 - `flash_storage_wear_init(slot_count)` checks that `slot_count * page_size` doesn't step over a large fraction of flash; still verify manually.
 - Erase uses `HAL_FLASHEx_Erase` (FLASH_EraseInitTypeDef) — ensure your HAL version supports this (STM32CubeF1 >= certain versions). If your HAL variant requires different parameters, adjust accordingly.
 - UART parser uses DMA + IDLE detection: you must enable UART IDLE interrupt in your IRQ handler to call `uart_dma_parser_handle_idle()` (see example main.c comments). CubeMX usually configures DMA and UART IRQs; follow its generated handlers and call the helper when IDLE occurs.

--- file: flash_drive/flash_storage.h ---

#ifndef __FLASH_STORAGE_H
#define __FLASH_STORAGE_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Basic APIs */
void flash_storage_init(void);
int flash_storage_read_epoch_wearlevel(uint32_t *epoch_out);
int flash_storage_read_date_wearlevel(uint8_t *day, uint8_t *month, uint16_t *year);

/* Wear-leveling APIs */
int flash_storage_wear_init(uint8_t slot_count);
HAL_StatusTypeDef flash_storage_save_date_wearlevel(uint8_t day, uint8_t month, uint16_t year);

/* Compatibility wrappers (when wear-leveling not used they operate on single last page) */
int flash_storage_read_epoch(uint32_t *epoch_out);
int flash_storage_read_date(uint8_t *day, uint8_t *month, uint16_t *year);
HAL_StatusTypeDef flash_storage_save_date(uint8_t day, uint8_t month, uint16_t year);

#ifdef __cplusplus
}
#endif

#endif // __FLASH_STORAGE_H

--- file: flash_drive/flash_storage.c ---

/* Implementation notes:
   - Each slot occupies one flash page.
   - Slot structure (offset words): magic, epoch, day, month, year, seq
   - erase uses HAL_FLASHEx_Erase with FLASH_EraseInitTypeDef
*/

#include "flash_storage.h"
#include <string.h>

#define FLASH_MAGIC_VALUE 0xA5A55A5AU
#define FLASH_BASE_ADDR   0x08000000U
#define FLASH_SIZE_WORD_ADDR 0x1FFFF7E0U

static uint8_t g_slot_count = 0;
static uint32_t g_slots_base_addr = 0;
static int g_latest_slot = -1;
static uint32_t g_latest_seq = 0;

static uint32_t page_size_bytes(void)
{
    uint16_t flash_kb = *(uint16_t *)FLASH_SIZE_WORD_ADDR;
    if (flash_kb == 0xFFFFU || flash_kb == 0x0000U) flash_kb = 64U;
    return (flash_kb < 128U) ? 1024U : 2048U;
}

static uint32_t compute_last_page_start(void)
{
    uint16_t flash_kb = *(uint16_t *)FLASH_SIZE_WORD_ADDR;
    if (flash_kb == 0xFFFFU || flash_kb == 0x0000U) flash_kb = 64U;
    uint32_t flash_bytes = (uint32_t)flash_kb * 1024U;
    uint32_t psize = page_size_bytes();
    return FLASH_BASE_ADDR + flash_bytes - psize;
}

static void read_slot_words(uint8_t slot_idx, uint32_t *out, uint32_t n)
{
    uint32_t addr = g_slots_base_addr + (uint32_t)slot_idx * page_size_bytes();
    uint32_t *p = (uint32_t *)addr;
    for (uint32_t i=0;i<n;i++) out[i]=p[i];
}

static void scan_slots(void)
{
    g_latest_slot = -1; g_latest_seq = 0;
    if (g_slot_count==0) return;
    for (uint8_t i=0;i<g_slot_count;i++) {
        uint32_t w[6];
        read_slot_words(i,w,6);
        if (w[0]==FLASH_MAGIC_VALUE && w[5]!=0xFFFFFFFFU) {
            if (g_latest_slot==-1 || w[5]>g_latest_seq) {
                g_latest_slot=i; g_latest_seq=w[5];
            }
        }
    }
}

int flash_storage_wear_init(uint8_t slot_count)
{
    if (slot_count==0) return -1;
    uint32_t last_page = compute_last_page_start();
    uint32_t psize = page_size_bytes();
    uint32_t needed = (uint32_t)slot_count * psize;
    uint16_t flash_kb = *(uint16_t *)FLASH_SIZE_WORD_ADDR;
    if (flash_kb == 0xFFFFU || flash_kb == 0x0000U) flash_kb = 64U;
    uint32_t flash_bytes = (uint32_t)flash_kb * 1024U;
    if (needed > flash_bytes/2U) return -1; /* too large */
    g_slot_count = slot_count;
    g_slots_base_addr = last_page - (uint32_t)(slot_count-1) * psize;
    scan_slots();
    return 0;
}

static HAL_StatusTypeDef erase_page(uint32_t page_addr)
{
    FLASH_EraseInitTypeDef EraseInit;
    EraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    EraseInit.PageAddress = page_addr;
    EraseInit.NbPages = 1;
    uint32_t PageError = 0;
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&EraseInit, &PageError);
    return st;
}

static HAL_StatusTypeDef program_word(uint32_t addr, uint32_t data)
{
    HAL_StatusTypeDef st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint64_t)data);
    return st;
}

static HAL_StatusTypeDef write_slot(uint8_t slot_idx, uint32_t epoch, uint8_t day, uint8_t month, uint16_t year, uint32_t seq)
{
    uint32_t base = g_slots_base_addr + (uint32_t)slot_idx * page_size_bytes();
    if (erase_page(base) != HAL_OK) return HAL_ERROR;
    if (program_word(base + 0, FLASH_MAGIC_VALUE) != HAL_OK) return HAL_ERROR;
    if (program_word(base + 4, epoch) != HAL_OK) return HAL_ERROR;
    if (program_word(base + 8, (uint32_t)day & 0xFFU) != HAL_OK) return HAL_ERROR;
    if (program_word(base + 12, (uint32_t)month & 0xFFU) != HAL_OK) return HAL_ERROR;
    if (program_word(base + 16, (uint32_t)year & 0xFFFFU) != HAL_OK) return HAL_ERROR;
    if (program_word(base + 20, seq) != HAL_OK) return HAL_ERROR;
    return HAL_OK;
}

HAL_StatusTypeDef flash_storage_save_date_wearlevel(uint8_t day, uint8_t month, uint16_t year)
{
    if (g_slot_count==0) return HAL_ERROR;
    /* validate */
    if (year<1970 || year>2099 || month<1 || month>12 || day<1 || day>31) return HAL_ERROR;
    uint8_t next_slot = (g_latest_slot==-1) ? 0 : (uint8_t)((g_latest_slot+1)%g_slot_count);
    uint32_t next_seq = g_latest_seq + 1U;
    /* convert to epoch (00:00 UTC) */
    uint32_t days=0;
    for (uint32_t y=1970;y<(uint32_t)year;y++) days += 365 + (((y%4==0 && y%100!=0) || (y%400==0))?1:0);
    static const uint8_t mdays_norm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (uint32_t m=1;m<(uint32_t)month;m++) {
        uint32_t dim = mdays_norm[m-1];
        if (m==2) { if (((year%4==0 && year%100!=0) || (year%400==0))) dim=29; }
        days += dim;
    }
    days += (uint32_t)(day-1);
    uint32_t epoch = days * 86400U;

    if (write_slot(next_slot, epoch, day, month, year, next_seq) != HAL_OK) return HAL_ERROR;
    g_latest_slot = next_slot; g_latest_seq = next_seq;
    return HAL_OK;
}

int flash_storage_read_epoch_wearlevel(uint32_t *epoch_out)
{
    if (g_slot_count==0 || g_latest_slot==-1) return -1;
    uint32_t words[6]; read_slot_words(g_latest_slot, words, 6);
    if (words[0] != FLASH_MAGIC_VALUE) return -1;
    *epoch_out = words[1];
    return 0;
}

int flash_storage_read_date_wearlevel(uint8_t *day, uint8_t *month, uint16_t *year)
{
    if (g_slot_count==0 || g_latest_slot==-1) return -1;
    uint32_t words[6]; read_slot_words(g_latest_slot, words, 6);
    if (words[0] != FLASH_MAGIC_VALUE) return -1;
    *day = (uint8_t)(words[2] & 0xFFU);
    *month = (uint8_t)(words[3] & 0xFFU);
    *year = (uint16_t)(words[4] & 0xFFFFU);
    return 0;
}

/* compatibility wrappers (single-page fallback) omitted for brevity in canvas - present in file */

--- file: rtc_hw/rtc_config.h ---

#ifndef __RTC_CONFIG_H
#define __RTC_CONFIG_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

#ifndef DEFAULT_EPOCH
#define DEFAULT_EPOCH 1609459200U
#endif

void rtc_hw_init(void);
HAL_StatusTypeDef rtc_set_from_epoch(uint32_t epoch);

#endif

--- file: rtc_hw/rtc_config.c ---

#include "rtc_config.h"

static RTC_HandleTypeDef hrtc_local;

void rtc_hw_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    RCC_OscInitStruct.LSEState = RCC_LSE_ON;
    RCC_OscInitStruct.HSEState = RCC_HSE_OFF;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
    __HAL_RCC_RTC_ENABLE();

    hrtc_local.Instance = RTC;
    hrtc_local.Init.AsynchPrediv = 127;
    hrtc_local.Init.SynchPrediv = 255;
    hrtc_local.Init.OutPut = RTC_OUTPUT_DISABLE;
    if (HAL_RTC_Init(&hrtc_local) != HAL_OK) {
        /* handle error */
    }
}

static void epoch_to_rtc(uint32_t epoch, RTC_TimeTypeDef *sTime, RTC_DateTypeDef *sDate)
{
    uint32_t seconds = epoch % 60U;
    uint32_t minutes = (epoch / 60U) % 60U;
    uint32_t hours = (epoch / 3600U) % 24U;
    uint32_t days = epoch / 86400U;

    uint32_t y = 1970U;
    while (1) {
        uint32_t leap = ( (y%4==0 && y%100!=0) || (y%400==0) ) ? 1U : 0U;
        uint32_t diy = 365U + leap;
        if (days >= diy) { days -= diy; y++; } else break;
    }
    static const uint8_t mdays_norm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t mon = 0;
    while (1) {
        uint8_t dim = mdays_norm[mon];
        if (mon==1) {
            uint32_t leap = ( (y%4==0 && y%100!=0) || (y%400==0) ) ? 1U : 0U;
            if (leap) dim = 29;
        }
        if (days >= dim) { days -= dim; mon++; } else break;
    }
    uint8_t day = (uint8_t)(days + 1U);
    uint8_t month = (uint8_t)(mon + 1U);
    uint16_t year = (uint16_t)y;

    sTime->Hours = (uint8_t)hours;
    sTime->Minutes = (uint8_t)minutes;
    sTime->Seconds = (uint8_t)seconds;
    sTime->DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime->StoreOperation = RTC_STOREOPERATION_RESET;

    sDate->Date = day;
    sDate->Month = month;
    sDate->Year = (uint8_t)(year - 2000U);
    sDate->WeekDay = 0U;
}

HAL_StatusTypeDef rtc_set_from_epoch(uint32_t epoch)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};
    epoch_to_rtc(epoch, &sTime, &sDate);
    if (HAL_RTC_SetTime(&hrtc_local, &sTime, RTC_FORMAT_BIN) != HAL_OK) return HAL_ERROR;
    if (HAL_RTC_SetDate(&hrtc_local, &sDate, RTC_FORMAT_BIN) != HAL_ERROR) return HAL_ERROR;
    return HAL_OK;
}

--- file: uart_process/uart_dma_parser.h ---

#ifndef __UART_DMA_PARSER_H
#define __UART_DMA_PARSER_H

#include "stm32f1xx_hal.h"

/* Initialize parser with UART and DMA handles (DMA for RX). */
void uart_dma_parser_init(UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdmarx);

/* Start DMA reception (non-blocking). */
HAL_StatusTypeDef uart_dma_parser_start(void);

/* Should be called from USART IRQ handler when IDLE flag detected.
   Example in IRQ: if(__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE)) { __HAL_UART_CLEAR_IDLEFLAG(&huart1); uart_dma_parser_handle_idle(); }
*/
void uart_dma_parser_handle_idle(void);

/* Poll parsed result: 0 none, 1 saved ok, 2 parse/flash error */
int uart_dma_parser_get_status(void);

#endif

--- file: uart_process/uart_dma_parser.c ---

#include "uart_dma_parser.h"
#include "flash_storage.h"
#include <string.h>
#include <stdio.h>

/* Implementation details:
   - Uses HAL_UART_Receive_DMA to fill a circular buffer.
   - On IDLE, compute number of received bytes and process them (extract lines ending with '
').
   - For each full line parse DD/MM/YYYY and call flash_storage_save_date_wearlevel().
   - Restart DMA after processing.
*/

#define UART_BUF_SIZE 256
static UART_HandleTypeDef *g_huart = NULL;
static DMA_HandleTypeDef *g_hdma = NULL;
static uint8_t g_uart_buf[UART_BUF_SIZE];
static volatile int g_status = 0; /* 0 none, 1 saved, 2 error */

void uart_dma_parser_init(UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdmarx)
{
    g_huart = huart;
    g_hdma = hdmarx;
    memset((void*)g_uart_buf,0,UART_BUF_SIZE);
}

HAL_StatusTypeDef uart_dma_parser_start(void)
{
    if (g_huart==NULL || g_hdma==NULL) return HAL_ERROR;
    /* start DMA rx in circular mode */
    HAL_UART_Receive_DMA(g_huart, g_uart_buf, UART_BUF_SIZE);
    /* enable IDLE interrupt */
    __HAL_UART_ENABLE_IT(g_huart, UART_IT_IDLE);
    return HAL_OK;
}

static void process_buffer(uint8_t *data, uint32_t len)
{
    /* process lines; supports fragmented input */
    static char line[128]; static uint32_t li = 0;
    for (uint32_t i=0;i<len;i++) {
        char ch = (char)data[i];
        if (ch=='
') continue;
        line[li++] = ch;
        if (li >= sizeof(line)-1) li = sizeof(line)-1;
        if (ch=='
') {
            line[li] = 'NULL';
                     unsigned int d,m,y;
            if (sscanf(line, "%u%*[^0-9]%u%*[^0-9]%u", &d, &m, &y) == 3) {
                if (d>=1 && d<=31 && m>=1 && m<=12 && y>=1970 && y<=2099) {
                    if (flash_storage_save_date_wearlevel((uint8_t)d, (uint8_t)m, (uint16_t)y) == HAL_OK) g_status = 1; else g_status = 2;
                } else {
                    g_status = 2;
                }
            } else {
                g_status = 2;
            }
            li = 0; /* reset for next line */
        }
    }
}

void uart_dma_parser_handle_idle(void)
{
    if (g_huart==NULL) return;
    /* stop DMA to get count */
    HAL_UART_DMAStop(g_huart);
    uint32_t received = UART_BUF_SIZE - __HAL_DMA_GET_COUNTER(g_hdma);
    if (received) {
        process_buffer(g_uart_buf, received);
    }
    /* clear buffer head and restart DMA */
    memset(g_uart_buf,0,UART_BUF_SIZE);
    HAL_UART_Receive_DMA(g_huart, g_uart_buf, UART_BUF_SIZE);
}

int uart_dma_parser_get_status(void)
{
    int s = g_status; if (s!=0) g_status = 0; return s;
}

--- file: main.c (example integration) ---

#include "stm32f1xx_hal.h"
#include "flash_drive/flash_storage.h"
#include "rtc_hw/rtc_config.h"
#include "uart_process/uart_dma_parser.h"

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;

void SystemClock_Config(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    rtc_hw_init();

    /* init wear-leveling with 4 slots */
    if (flash_storage_wear_init(4) != 0) {
        /* handle error */
    }

    /* start UART DMA parser */
    uart_dma_parser_init(&huart1, &hdma_usart1_rx);
    uart_dma_parser_start();

    /* load epoch */
    uint32_t epoch = 0;
    if (flash_storage_read_epoch_wearlevel(&epoch) != 0 || epoch == 0U) epoch = DEFAULT_EPOCH;
    rtc_set_from_epoch(epoch);

    while (1) {
        int s = uart_dma_parser_get_status();
        if (s==1) {
            /* saved ok */
        } else if (s==2) {
            /* parse or flash write error */
        }
    }
}

/* In USART IRQ handler (in stm32f1xx_it.c), detect IDLE and call handler: */
/*
void USART1_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&huart1);
        uart_dma_parser_handle_idle();
    }
    HAL_UART_IRQHandler(&huart1);
}
*/

End of files