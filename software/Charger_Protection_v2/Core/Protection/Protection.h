/* Relay protection. Physical units: V RMS, A RMS, degrees Celsius. */
#ifndef PROTECTION_PROTECTION_H_
#define PROTECTION_PROTECTION_H_

#include <stdint.h>
#include "Protection_Cfg.h"
#include "../Calib/ProtectionParams.h"

/* Auto-reclose for voltage/temperature faults, with hysteresis.
 * Vin must be within [UV_RECOVER, OV_RECOVER]; Vout and temperature must stay
 * <= their recovery limits for the hold time after opening. */
/* LED and buzzer share a 250 ms ON + 250 ms OFF = 0.5 s full cycle. */
#define PROTECTION_LED_PERIOD_MS        500U
#define PROTECTION_LED_HALF_PERIOD_MS   (PROTECTION_LED_PERIOD_MS / 2U)
/* First tick counts conservatively, so timeout never exceeds 30 tick periods. */
#define PROTECTION_ZERO_CROSS_TIMEOUT_MS 30U

/* Board assumption: HIGH energizes each normally-open relay, lights LED,
 * and enables an active buzzer (steady GPIO level produces the sound).
 * Set to 0 if the fitted board uses an active-low driver instead. */
#define PROTECTION_RELAY_ACTIVE_HIGH    1U
#define PROTECTION_LED_ACTIVE_HIGH      1U
#define PROTECTION_BUZZER_ACTIVE_HIGH   1U

typedef enum
{
    PROTECTION_FAULT_NONE        = 0U,
    PROTECTION_FAULT_OV_IN       = 1U << 0,
    PROTECTION_FAULT_OV_OUT      = 1U << 1,
    PROTECTION_FAULT_OC          = 1U << 2,
    PROTECTION_FAULT_LEAK        = 1U << 3,
    PROTECTION_FAULT_OT          = 1U << 4,
    PROTECTION_FAULT_MEASUREMENT = 1U << 5,
    PROTECTION_FAULT_UV_IN       = 1U << 6
} ProtectionFault_t;

#define PROTECTION_RECOVERABLE_FAULTS   (PROTECTION_FAULT_OV_IN | \
                                        PROTECTION_FAULT_OV_OUT | PROTECTION_FAULT_OT | \
                                        PROTECTION_FAULT_UV_IN)

/* Once at boot, after GPIO/clock initialization and before MeasurementInit.
 * Keeps both relays open until the first valid measurement. No-fault status:
 * LED continuously on, buzzer off, including during startup. Scheduler_Init()
 * enables the 1 ms SysTick interrupt after component initialization.
 * Only OV_IN/OV_OUT/UV_IN/OT can auto-recover. Other faults require MCU reset.
 * Input selection is controlled by PROTECTION_MODE in Protection_Cfg.h. */
void ProtectionInit(void);
/* Main-loop Flash service: immediately open relays and inhibit all reclosure
 * until MCU reset. Acquisition is stopped by the Flash storage port. */
void Protection_EnterCalibMode(void);
/* Main loop only, immediately after Measurement_MainFunction(). Latch a trip
 * request at >= upper thresholds or Vin <= the undervoltage threshold.
 * RMS/temperature detection latency includes the measurement window (200 ms).
 * Relays open at the next qualified Vin zero-crossing notification, or after
 * at most 30 ms if it is missing. Invalid measurements also request a trip.
 * Auto-recovery is committed here after the 1 ms hook counts the hold time;
 * any latched non-recoverable fault prevents automatic reclosure. */
void Protection_MainFunction(void);
/* Called directly by DMA1_Channel1_IRQHandler() after Measurement processing,
 * and by Protection_ZeroCrossISR(), both in ADC1 DMA1 channel 1 ISR context.
 * Consumes the existing latched PEAK
 * leakage flag to request a synchronized trip, independently of RMS calculation.
 * This extra path uses Measurement's peak threshold, not a 30 mA RMS test. */
void Protection_FastLeakageISR(void);
/* Called only through Rte_Call_Measurement_ZeroCross in the DMA ISR. Consumes a
 * fast leakage flag that may have appeared earlier in the same DMA batch. */
void Protection_ZeroCrossISR(void);
/* Fatal acquisition/application error: open immediately before interrupts are
 * disabled, since neither zero-cross callbacks nor the timeout can then run. */
void Protection_EmergencyStop(void);
/* Once per 1 ms SysTick interrupt, or per polled tick in Error_Handler while
 * IRQs are disabled. Keeps fault LED/buzzer in phase; no floating-point work. */
void Protection_Tick1msISR(void);
/* Latched faults; voltage/temperature bits clear on successful auto-recovery.
 * Other bits remain until reset. Safe for debugger inspection. */
uint32_t Protection_GetFaultMask(void);
/* 1 between detection of a fault and the synchronized/timeout relay command. */
uint8_t Protection_IsTripPending(void);

#endif /* PROTECTION_PROTECTION_H_ */
