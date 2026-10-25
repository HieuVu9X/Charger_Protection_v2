#include "Protection.h"
#include "main.h"
#include "Rte.h"
#include <math.h>

static volatile uint32_t fault_mask;
static volatile uint16_t led_elapsed_ms;
static volatile uint16_t zero_cross_wait_ms;
static volatile uint8_t trip_pending;
static uint8_t relays_enabled;
static volatile uint16_t recovery_elapsed_ms;
static volatile uint8_t recovery_armed;
static uint8_t calib_mode;

_Static_assert(PROTECTION_RECOVER_HOLD_MS > 0U && PROTECTION_RECOVER_HOLD_MS <= UINT16_MAX,
               "Recovery hold must fit the 1 ms counter");
_Static_assert(PROTECTION_OV_RECOVER_V < PROTECTION_OV_V &&
               PROTECTION_OT_RECOVER_C < PROTECTION_OT_C,
               "Recovery limits must be below trip limits");
_Static_assert(PROTECTION_UV_IN_V < PROTECTION_UV_IN_RECOVER_V &&
               PROTECTION_UV_IN_RECOVER_V < PROTECTION_OV_RECOVER_V,
               "Vin recovery band must lie between the voltage trip limits");

#if PROTECTION_MODE == PROTECTION_MODE_DEBUG
/* Volatile keeps debugger updates observable, including the fast ISR path.
 * This test-only object consumes no RAM in RELEASE mode. */
volatile MeasureResult_t MeasureResult_test;
#define PROTECTION_INPUT MeasureResult_test
#else
#define PROTECTION_INPUT MeasureResult
#endif

static void SetRelays(uint8_t enabled)
{
    if (enabled == PROTECTION_RELAY_ACTIVE_HIGH)
    {
        LL_GPIO_SetOutputPin(RELAY_LINE_CTRL_GPIO_Port, RELAY_LINE_CTRL_Pin);
        LL_GPIO_SetOutputPin(RELAY_NEUTRAL_CTRL_GPIO_Port, RELAY_NEUTRAL_CTRL_Pin);
    }
    else
    {
        LL_GPIO_ResetOutputPin(RELAY_LINE_CTRL_GPIO_Port, RELAY_LINE_CTRL_Pin);
        LL_GPIO_ResetOutputPin(RELAY_NEUTRAL_CTRL_GPIO_Port, RELAY_NEUTRAL_CTRL_Pin);
    }
    relays_enabled = enabled;
}

static void SetLed(uint8_t enabled)
{
    if (enabled == PROTECTION_LED_ACTIVE_HIGH)
        LL_GPIO_SetOutputPin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
    else
        LL_GPIO_ResetOutputPin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
}

static void SetBuzzer(uint8_t enabled)
{
    if (enabled == PROTECTION_BUZZER_ACTIVE_HIGH)
        LL_GPIO_SetOutputPin(BUZZER_CTRL_GPIO_Port, BUZZER_CTRL_Pin);
    else
        LL_GPIO_ResetOutputPin(BUZZER_CTRL_GPIO_Port, BUZZER_CTRL_Pin);
}

/* Call only with interrupts masked, or from the DMA ISR (higher priority than
 * SysTick). The main loop must never close a relay across an ISR trip. */
static void RequestTrip(uint32_t faults)
{
    /* Any fault, including a fast ISR trip, cancels an in-progress recovery. */
    recovery_armed = 0U;
    recovery_elapsed_ms = 0U;
    if (fault_mask == PROTECTION_FAULT_NONE)
    {
        led_elapsed_ms = 0U;
        SetLed(1U);
        SetBuzzer(1U);
        if (relays_enabled != 0U)
        {
            zero_cross_wait_ms = 0U;
            trip_pending = 1U;
        }
    }
    fault_mask |= faults;
}

void ProtectionInit(void)
{
    calib_mode = 0U;
    SetRelays(0U);
    SetLed(1U);
    SetBuzzer(0U);
    fault_mask = PROTECTION_FAULT_NONE;
    led_elapsed_ms = 0U;
    zero_cross_wait_ms = 0U;
    trip_pending = 0U;
    recovery_armed = 0U;
    recovery_elapsed_ms = 0U;
}

void Protection_MainFunction(void)
{
    if (calib_mode != 0U) return;
    const ProtectionParams_t *cfg = Rte_Call_Calib_GetProtection();
    uint32_t faults = PROTECTION_FAULT_NONE;

    /* RELEASE reads results published in the main loop. DEBUG reads the
     * debugger-editable test input. ISRs consume only the fast leakage flag. */
    if (PROTECTION_INPUT.startup_complete != 0U)
    {
        if (PROTECTION_INPUT.V_in_rms >= cfg->ov_v)
            faults |= PROTECTION_FAULT_OV_IN;
        /* Negative/non-finite RMS is a measurement fault, not a valid UV input. */
        if (PROTECTION_INPUT.V_in_rms >= 0.0f &&
            PROTECTION_INPUT.V_in_rms <= cfg->uv_in_v)
            faults |= PROTECTION_FAULT_UV_IN;
        if (PROTECTION_INPUT.V_out_rms >= cfg->ov_v)
            faults |= PROTECTION_FAULT_OV_OUT;
        if (PROTECTION_INPUT.I_in_rms >= cfg->oc_a)
            faults |= PROTECTION_FAULT_OC;
        if (PROTECTION_INPUT.I_leak_rms >= cfg->leak_a)
            faults |= PROTECTION_FAULT_LEAK;
        if (PROTECTION_INPUT.Temperature >= cfg->ot_c)
            faults |= PROTECTION_FAULT_OT;

        if (PROTECTION_INPUT.temperature_fault != 0U ||
			PROTECTION_INPUT.adc_clipped_mask != ADC_CLIPPED_NONE ||
            !isfinite(PROTECTION_INPUT.V_in_rms) || PROTECTION_INPUT.V_in_rms < 0.0f ||
            !isfinite(PROTECTION_INPUT.V_out_rms) || PROTECTION_INPUT.V_out_rms < 0.0f ||
            !isfinite(PROTECTION_INPUT.I_in_rms) || PROTECTION_INPUT.I_in_rms < 0.0f ||
            !isfinite(PROTECTION_INPUT.I_leak_rms) || PROTECTION_INPUT.I_leak_rms < 0.0f ||
            !isfinite(PROTECTION_INPUT.Temperature))
            faults |= PROTECTION_FAULT_MEASUREMENT;
    }

    /* Keep float checks outside the critical section. The validity checks above
     * and the fast flag below must also pass before any automatic reclosure. */
    uint8_t recovery_values_ok = PROTECTION_INPUT.startup_complete != 0U &&
        PROTECTION_INPUT.fast_leakage_ready != 0U &&
        PROTECTION_INPUT.V_in_rms >= cfg->uv_in_recover_v &&
        PROTECTION_INPUT.V_in_rms <= cfg->ov_recover_v &&
        PROTECTION_INPUT.V_out_rms <= cfg->ov_recover_v &&
        PROTECTION_INPUT.Temperature <= cfg->ot_recover_c;

    /* Only serialize fault/GPIO changes, leaving the float checks interruptible. */
    uint32_t saved_primask = __get_PRIMASK();
    __disable_irq();
    if (PROTECTION_INPUT.leakage_trip_flag != 0U)
        faults |= PROTECTION_FAULT_LEAK;
    if (faults != PROTECTION_FAULT_NONE)
        RequestTrip(faults);
    else
    {
        if (fault_mask != PROTECTION_FAULT_NONE &&
            (fault_mask & ~PROTECTION_RECOVERABLE_FAULTS) == 0U &&
            recovery_values_ok != 0U && trip_pending == 0U && relays_enabled == 0U)
        {
            if (recovery_armed == 0U)
            {
                recovery_elapsed_ms = 0U;
                recovery_armed = 1U;
            }
            else if (recovery_elapsed_ms >= cfg->recover_hold_ms)
            {
                fault_mask = PROTECTION_FAULT_NONE;
                recovery_armed = 0U;
                recovery_elapsed_ms = 0U;
                led_elapsed_ms = 0U;
            }
        }
        else
        {
            recovery_armed = 0U;
            recovery_elapsed_ms = 0U;
        }

        if (fault_mask == PROTECTION_FAULT_NONE)
        {
            uint8_t ready = PROTECTION_INPUT.startup_complete != 0U &&
                            PROTECTION_INPUT.fast_leakage_ready != 0U;
            SetRelays(ready);
            SetLed(1U);
            SetBuzzer(0U);
        }
    }
    __set_PRIMASK(saved_primask);
}

/* Called directly after Measurement_DMA_IRQHandler() by
 * DMA1_Channel1_IRQHandler() (ADC1 DMA1 channel 1 HT/TC/TE interrupt).
 * Also called by Protection_ZeroCrossISR() inside the same ADC DMA context. */
void Protection_FastLeakageISR(void)
{
    if (PROTECTION_INPUT.leakage_trip_flag != 0U)
        RequestTrip(PROTECTION_FAULT_LEAK);
}

void Protection_ZeroCrossISR(void)
{
    /* Preserve sample order: leakage detected before this crossing may use it;
     * a crossing processed before a later leakage fault cannot open a relay. */
    Protection_FastLeakageISR();
    if (trip_pending != 0U)
    {
        SetRelays(0U);
        trip_pending = 0U;
    }
}

void Protection_Tick1msISR(void)
{
    /* DMA can preempt SysTick. Serialize timeout and GPIO writes so a new trip
     * request cannot lose a tick or race with a crossing. No float work here. */
    uint32_t saved_primask = __get_PRIMASK();
    __disable_irq();
    if (trip_pending != 0U)
    {
        zero_cross_wait_ms++;
        if (zero_cross_wait_ms >= PROTECTION_ZERO_CROSS_TIMEOUT_MS)
        {
            SetRelays(0U);
            trip_pending = 0U;
        }
    }
    if (fault_mask != PROTECTION_FAULT_NONE)
    {
        led_elapsed_ms++;
        if (led_elapsed_ms >= PROTECTION_LED_HALF_PERIOD_MS)
        {
            led_elapsed_ms = 0U;
            LL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
            LL_GPIO_TogglePin(BUZZER_CTRL_GPIO_Port, BUZZER_CTRL_Pin);
        }
    }
    /* Saturate: a stalled main loop must not wrap the recovery countdown.
     * Only the main loop validates readings, clears faults and closes relays. */
    if (recovery_armed != 0U &&
        recovery_elapsed_ms < Rte_Call_Calib_GetProtection()->recover_hold_ms)
        recovery_elapsed_ms++;
    __set_PRIMASK(saved_primask);
}

void Protection_EmergencyStop(void)
{
    uint32_t saved_primask = __get_PRIMASK();
    __disable_irq();
    RequestTrip(PROTECTION_FAULT_MEASUREMENT);
    SetRelays(0U);
    trip_pending = 0U;
    __set_PRIMASK(saved_primask);
}

uint32_t Protection_GetFaultMask(void)
{
    return fault_mask;
}

void Protection_EnterCalibMode(void)
{
    uint32_t saved_primask = __get_PRIMASK();
    __disable_irq();
    calib_mode = 1U;
    recovery_armed = 0U;
    recovery_elapsed_ms = 0U;
    SetRelays(0U);
    trip_pending = 0U;
    __set_PRIMASK(saved_primask);
}

uint8_t Protection_IsTripPending(void)
{
    return trip_pending;
}
