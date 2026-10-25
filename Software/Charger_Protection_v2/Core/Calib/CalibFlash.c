#include "CalibFlash.h"

#include "main.h"
#include "Protection.h"
#include <string.h>

/* Linker-reserved page 15 (1 KB), outside the firmware FLASH region. */
extern uint8_t __calib_start__[CALIB_PAGE_SIZE * CALIB_PAGE_COUNT];

const void *CalibFlash_Page(uint32_t page)
{
    return &__calib_start__[page * CALIB_PAGE_SIZE];
}

static uint8_t FlashReady(void)
{
    /* Code fetch stalls while Flash is busy. The service state keeps relays
     * open and ADC/DMA stopped; this is intentionally a blocking operation. */
    while ((FLASH->SR & FLASH_SR_BSY) != 0U) {}
    return (FLASH->SR & (FLASH_SR_PGERR | FLASH_SR_WRPRTERR)) == 0U;
}

static uint8_t ProgramHalfWord(uintptr_t address, uint16_t value)
{
    FLASH->CR |= FLASH_CR_PG;
    *(volatile uint16_t *)address = value;
    uint8_t ok = FlashReady();
    FLASH->CR &= ~FLASH_CR_PG;
    return ok && *(volatile const uint16_t *)address == value;
}

CalibStatus_t CalibFlash_Replace(uint32_t page, const void *body,
                                           uint32_t length, uint16_t commit)
{
    if (page >= CALIB_PAGE_COUNT || body == NULL || length == 0U ||
        (length & 1U) != 0U || length > CALIB_PAGE_SIZE - 2U)
        return CALIB_INVALID;
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        (FLASH->SR & FLASH_SR_BSY) != 0U)
        return CALIB_BUSY;

    Protection_EnterCalibMode();
    __disable_irq();
    LL_TIM_DisableCounter(TIM1);
    if (LL_ADC_REG_IsConversionOngoing(ADC1))
    {
        LL_ADC_REG_StopConversion(ADC1);
        while (LL_ADC_REG_IsStopConversionOngoing(ADC1)) {}
    }
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_DisableIT_HT(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_DisableIT_TC(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_DisableIT_TE(DMA1, LL_DMA_CHANNEL_1);
    NVIC_DisableIRQ(DMA1_Channel1_IRQn);
    NVIC_ClearPendingIRQ(DMA1_Channel1_IRQn);

    if ((FLASH->CR & FLASH_CR_LOCK) != 0U)
    {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    FLASH->SR = FLASH_SR_PGERR | FLASH_SR_WRPRTERR | FLASH_SR_EOP;
    uintptr_t address = (uintptr_t)CalibFlash_Page(page);
    FLASH->CR |= FLASH_CR_PER;
    FLASH->AR = (uint32_t)address;
    FLASH->CR |= FLASH_CR_STRT;
    uint8_t ok = FlashReady();
    FLASH->CR &= ~FLASH_CR_PER;
    const uint8_t *bytes = body;
    for (uint32_t offset = 0U; ok && offset < length; offset += 2U)
    {
        uint16_t value = (uint16_t)bytes[offset] |
                         (uint16_t)((uint16_t)bytes[offset + 1U] << 8);
        ok = ProgramHalfWord(address + offset, value);
    }
    /* Never commit a record until its entire payload has been read back. */
    if (ok && memcmp((const void *)address, body, length) == 0)
        ok = ProgramHalfWord(address + length, commit);
    else
        ok = 0U;
    FLASH->CR |= FLASH_CR_LOCK;
    __set_PRIMASK(0U);
    return ok ? CALIB_OK : CALIB_FLASH_ERROR;
}
