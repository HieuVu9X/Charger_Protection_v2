# README

Project layout (final) — full rewrite to accept full datetime (DD/MM/YYYY hh:mm:ss) and store epoch (including hour/min/sec) into flash using wear-leveling.

Folders: `flash_drive`, `rtc_hw`, `uart_process`.

Behavior summary
- On boot: read latest epoch from flash (wear-leveling slots). If found and != 0, set RTC from that epoch. Otherwise set RTC from DEFAULT_EPOCH.
- UART input: non-blocking DMA + IDLE. When a full line ending with `
` is received, parser accepts formats:
    - `DD/MM/YYYY hh:mm:ss`  e.g. `31/08/2025 14:35:12`  (preferred)
    - `DD/MM/YYYY` (assumes `00:00:00`)
  Parser validates ranges, converts to Unix epoch (seconds since 1970-01-01 UTC), writes epoch to flash using wear-leveling (rotate slots), then immediately updates RTC with the new epoch.

Notes before flashing
- Ensure the last N flash pages (slots) are free (not containing code/bootloader). `flash_storage_wear_init(slot_count)` chooses pages ending at flash end. Default example uses 4 slots.
- Uses `HAL_FLASHEx_Erase` for page erase (STM32Cube HAL). Adjust if your HAL differs.
- UART DMA requires proper CubeMX configuration of `huart1` and `hdma_usart1_rx`. In `USARTx_IRQHandler` call IDLE handler as shown in example.

--- file: flash_drive/flash_storage.h ---

#ifndef __FLASH_STORAGE_H
#define __FLASH_STORAGE_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize flash storage (optional) */
void flash_storage_init(void);

/* Wear-leveling init: reserve slot_count pages (call early). */
int flash_storage_wear_init(uint8_t slot_count);

/* Save raw epoch into flash using wear-leveling. */
HAL_StatusTypeDef flash_storage_save_epoch_wearlevel(uint32_t epoch);

/* Read latest epoch from wear-leveling storage. Returns 0 on success. */
int flash_storage_read_epoch_wearlevel(uint32_t *epoch_out);

#ifdef __cplusplus
}
#endif

#endif // __FLASH_STORAGE_H

--- file: flash_drive/flash_storage.c ---

#include "flash_storage.h"
#include <string.h>

#define FLASH_MAGIC_VALUE    0xA5A55A5AU
#define FLASH_BASE_ADDR      0x08000000U
#define FLASH_SIZE_WORD_ADDR 0x1FFFF7E0U  /* word with flash size in KB */

/* Each slot/page layout (words):
   [0] magic
   [1] epoch (uint32)
   [2] reserved (could store low bytes of Y/M/D/H/M/S if desired)
   [3] seq (monotonic increasing)
*/

static uint8_t g_slot_count = 0;
static uint32_t g_slots_base_addr = 0;
static int g_latest_slot = -1;
static uint32_t g_latest_seq = 0;

static uint32_t page_size_bytes(void)
{
    uint16_t flash_kb = *(uint16_t*)FLASH_SIZE_WORD_ADDR;
    if (flash_kb == 0xFFFFU || flash_kb == 0x0000U) flash_kb = 64U;
    return (flash_kb < 128U) ? 1024U : 2048U;
}

static uint32_t compute_last_page_start(void)
{
    uint16_t flash_kb = *(uint16_t*)FLASH_SIZE_WORD_ADDR;
    if (flash_kb == 0xFFFFU || flash_kb == 0x0000U) flash_kb = 64U;
    uint32_t flash_bytes = (uint32_t)flash_kb * 1024U;
    uint32_t psize = page_size_bytes();
    return FLASH_BASE_ADDR + flash_bytes - psize;
}

static void read_slot_words(uint8_t slot_idx, uint32_t *out_words, uint32_t n)
{
    uint32_t addr = g_slots_base_addr + (uint32_t)slot_idx * page_size_bytes();
    uint32_t *p = (uint32_t*)addr;
    for (uint32_t i=0;i<n;i++) out_words[i] = p[i];
}

static void scan_slots(void)
{
    g_latest_slot = -1; g_latest_seq = 0;
    if (g_slot_count==0) return;
    for (uint8_t i=0;i<g_slot_count;i++) {
        uint32_t w[4];
        read_slot_words(i,w,4);
        if (w[0]==FLASH_MAGIC_VALUE && w[3]!=0xFFFFFFFFU) {
            if (g_latest_slot==-1 || w[3] > g_latest_seq) {
                g_latest_slot = i; g_latest_seq = w[3];
            }
        }
    }
}

int flash_storage_init(void)
{
    /* noop for now */
    return 0;
}

int flash_storage_wear_init(uint8_t slot_count)
{
    if (slot_count==0) return -1;
    uint32_t last_page = compute_last_page_start();
    uint32_t psize = page_size_bytes();
    uint32_t needed = (uint32_t)slot_count * psize;
    uint16_t flash_kb = *(uint16_t*)FLASH_SIZE_WORD_ADDR;
    if (flash_kb == 0xFFFFU || flash_kb == 0x0000U) flash_kb = 64U;
    uint32_t flash_bytes = (uint32_t)flash_kb * 1024U;
    if (needed > flash_bytes/2U) return -1; /* safety check */
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
    return HAL_FLASHEx_Erase(&EraseInit, &PageError);
}

static HAL_StatusTypeDef program_word(uint32_t addr, uint32_t data)
{
    HAL_StatusTypeDef st;
    HAL_FLASH_Unlock();
    st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint64_t)data);
    HAL_FLASH_Lock();
    return st;
}

static HAL_StatusTypeDef write_slot(uint8_t slot_idx, uint32_t epoch, uint32_t seq)
{
    uint32_t base = g_slots_base_addr + (uint32_t)slot_idx * page_size_bytes();
    if (erase_page(base) != HAL_OK) return HAL_ERROR;
    if (program_word(base + 0, FLASH_MAGIC_VALUE) != HAL_OK) return HAL_ERROR;
    if (program_word(base + 4, epoch) != HAL_OK) return HAL_ERROR;
    if (program_word(base + 8, 0U) != HAL_OK) return HAL_ERROR; /* reserved */
    if (program_word(base + 12, seq) != HAL_OK) return HAL_ERROR;
    return HAL_OK;
}

HAL_StatusTypeDef flash_storage_save_epoch_wearlevel(uint32_t epoch)
{
    if (g_slot_count==0) return HAL_ERROR;
    uint8_t next_slot = (g_latest_slot==-1) ? 0 : (uint8_t)((g_latest_slot+1)%g_slot_count);
    uint32_t next_seq = g_latest_seq + 1U;
    if (write_slot(next_slot, epoch, next_seq) != HAL_OK) return HAL_ERROR;
    g_latest_slot = next_slot; g_latest_seq = next_seq;
    return HAL_OK;
}

int flash_storage_read_epoch_wearlevel(uint32_t *epoch_out)
{
    if (g_slot_count==0 || g_latest_slot==-1) return -1;
    uint32_t w[4]; read_slot_words(g_latest_slot,w,4);
    if (w[0] != FLASH_MAGIC_VALUE) return -1;
    *epoch_out = w[1];
    return 0;
}

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
uint32_t ymd_hms_to_epoch(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);

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

/* convert epoch -> rtc structs */
static void epoch_to_rtc(uint32_t epoch, RTC_TimeTypeDef *sTime, RTC_DateTypeDef *sDate)
{
    uint32_t seconds = epoch % 60U;
    uint32_t minutes = (epoch / 60U) % 60U;
    uint32_t hours = (epoch / 3600U) % 24U;
    uint32_t days = epoch / 86400U;

    uint32_t y = 1970U;
    while (1) {
        uint32_t leap = (((y%4==0) && (y%100!=0)) || (y%400==0)) ? 1U : 0U;
        uint32_t diy = 365U + leap;
        if (days >= diy) { days -= diy; y++; } else break;
    }
    static const uint8_t mdays_norm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t mon = 0;
    while (1) {
        uint8_t dim = mdays_norm[mon];
        if (mon==1) {
            uint32_t leap = (((y%4==0) && (y%100!=0)) || (y%400==0)) ? 1U : 0U;
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

/* Convert Y/M/D H:M:S -> unix epoch (UTC) */
uint32_t ymd_hms_to_epoch(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    if (year < 1970) return 0U;
    uint32_t days = 0;
    for (uint32_t y=1970; y < (uint32_t)year; ++y) {
        int leap = (((y%4==0) && (y%100!=0)) || (y%400==0)) ? 1 : 0;
        days += 365 + (leap?1:0);
    }
    static const uint8_t mdays_norm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    for (uint32_t m=1; m < (uint32_t)month; ++m) {
        uint32_t dim = mdays_norm[m-1];
        if (m==2) {
            int leap = (((year%4==0) && (year%100!=0)) || (year%400==0)) ? 1 : 0;
            if (leap) dim = 29;
        }
        days += dim;
    }
    days += (uint32_t)(day - 1U);
    uint32_t epoch = days * 86400U + (uint32_t)hour * 3600U + (uint32_t)minute * 60U + (uint32_t)second;
    return epoch;
}

--- file: uart_process/uart_dma_parser.h ---

#ifndef __UART_DMA_PARSER_H
#define __UART_DMA_PARSER_H

#include "stm32f1xx_hal.h"

void uart_dma_parser_init(UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdmarx);
HAL_StatusTypeDef uart_dma_parser_start(void);
void uart_dma_parser_handle_idle(void);
int uart_dma_parser_get_status(void);

#endif

--- file: uart_process/uart_dma_parser.c ---

#include "uart_dma_parser.h"
#include "flash_storage.h"
#include "rtc_hw/rtc_config.h"
#include <string.h>
#include <stdio.h>

#define UART_BUF_SIZE 256
static UART_HandleTypeDef *g_huart = NULL;
static DMA_HandleTypeDef *g_hdma = NULL;
static uint8_t g_uart_buf[UART_BUF_SIZE];
static volatile int g_status = 0; /* 0 none, 1 saved ok, 2 parse/flash error */

void uart_dma_parser_init(UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdmarx)
{
    g_huart = huart;
    g_hdma = hdmarx;
    memset(g_uart_buf,0,sizeof(g_uart_buf));
}

HAL_StatusTypeDef uart_dma_parser_start(void)
{
    if (g_huart==NULL || g_hdma==NULL) return HAL_ERROR;
    HAL_UART_Receive_DMA(g_huart, g_uart_buf, UART_BUF_SIZE);
    __HAL_UART_ENABLE_IT(g_huart, UART_IT_IDLE);
    return HAL_OK;
}

static void process_buffer(uint8_t *data, uint32_t len)
{
    static char line[128]; static uint32_t li = 0;
    for (uint32_t i=0;i<len;i++) {
        char ch = (char)data[i];
        if (ch == '
') continue;
        line[li++] = ch;
        if (li >= sizeof(line)-1) li = sizeof(line)-1;
        if (ch == '
') {
            line[li] = 'NULL';
            /* try parse: either DD/MM/YYYY hh:mm:ss or DD/MM/YYYY */
            unsigned int d=0,m=0,y=0, hh=0, mm=0, ss=0;
            int parsed = 0;
            if (sscanf(line, "%u%*[^0-9]%u%*[^0-9]%u %u:%u:%u", &d,&m,&y,&hh,&mm,&ss) == 6) parsed = 6;
            else if (sscanf(line, "%u%*[^0-9]%u%*[^0-9]%u", &d,&m,&y) == 3) { parsed = 3; hh=0; mm=0; ss=0; }

            if (parsed == 6 || parsed == 3) {
                /* validate ranges */
                if (y < 1970 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31 || hh > 23 || mm > 59 || ss > 59) {
                    g_status = 2; /* validation error */
                } else {
                    uint32_t epoch = ymd_hms_to_epoch((uint16_t)y, (uint8_t)m, (uint8_t)d, (uint8_t)hh, (uint8_t)mm, (uint8_t)ss);
                    if (epoch == 0U) { g_status = 2; }
                    else {
                        if (flash_storage_save_epoch_wearlevel(epoch) == HAL_OK) {
                            /* also immediately set RTC */
                            rtc_set_from_epoch(epoch);
                            g_status = 1;
                        } else {
                            g_status = 2;
                        }
                    }
                }
            } else {
                g_status = 2; /* parse fail */
            }
            li = 0;
        }
    }
}

void uart_dma_parser_handle_idle(void)
{
    if (g_huart==NULL || g_hdma==NULL) return;
    HAL_UART_DMAStop(g_huart);
    uint32_t received = UART_BUF_SIZE - __HAL_DMA_GET_COUNTER(g_hdma);
    if (received) process_buffer(g_uart_buf, received);
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

/* provide these in your project (CubeMX) */
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_rx;

void SystemClock_Config(void);

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* init RTC */
    rtc_hw_init();

    /* init flash wear-leveling (4 slots) */
    if (flash_storage_wear_init(4) != 0) {
        /* handle error */
    }

    /* start uart DMA parser */
    uart_dma_parser_init(&huart1, &hdma_usart1_rx);
    uart_dma_parser_start();

    /* on startup, try read epoch */
    uint32_t epoch = 0;
    if (flash_storage_read_epoch_wearlevel(&epoch) != 0 || epoch == 0U) epoch = DEFAULT_EPOCH;
    rtc_set_from_epoch(epoch);

    while (1) {
        int st = uart_dma_parser_get_status();
        if (st == 1) {
            /* saved and RTC updated */
        } else if (st == 2) {
            /* parse or flash error */
        }
        HAL_Delay(200);
    }
}

/* In your USART IRQ handler add IDLE handling. Example: */
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