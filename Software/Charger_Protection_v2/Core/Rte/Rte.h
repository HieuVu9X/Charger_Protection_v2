/*
 * Rte.h
 *
 *  Created on: Jun 8, 2026
 *      Author: Admin
 */

#ifndef RTE_RTE_H_
#define RTE_RTE_H_

/*--------------------------------------------------------------------------------
 *                                 Include Files
 -------------------------------------------------------------------------------*/
#include <com.h>
#include "stdint.h"
#include "Protection.h"
#include "Calib.h"
#include "Scheduler.h"
/* Application provider, declared here so component clients use only RTE. */
void Error_Handler(void);
/*--------------------------------------------------------------------------------
 *                               Macro Definitions
 -------------------------------------------------------------------------------*/
/* Synchronous RTE call mappings: expand directly to the provider, preserving
 * the caller's ISR/task context without an extra wrapper function. Provider
 * declarations are imported above by RTE; callers need only include Rte.h. */
/* SysTick -> scheduler, or Error_Handler polling -> Protection timer. */
#define Rte_Call_Protection_Tick1msISR()  		Protection_Tick1msISR()
/* ADC1 DMA1 channel 1 ISR: Measurement crossing -> Protection. */
#define Rte_Call_Measurement_ZeroCross()  		Protection_ZeroCrossISR()
/* Application error path, before disabling interrupts permanently. */
#define Rte_Call_Protection_EmergencyStop() 	Protection_EmergencyStop()
/* Measurement -> persistent coefficients loaded at startup. */
#define Rte_Call_Calib_Get()                     Calib_Get()
#define Rte_Call_Calib_GetProtection()           Calib_GetProtection()
#define Rte_Call_Calib_SaveProtection(params)    Calib_SaveProtection(params)
#define Rte_Call_Calib_ReadStoredProtection(p)   Calib_ReadStoredProtection(p)
#define Rte_Call_Calib_RestoreProtectionDefaults() Calib_RestoreProtectionDefaults()
/* Measurement -> application fatal acquisition-error path. */
#define Rte_Call_Application_ErrorHandler()      Error_Handler()
#define RTE_Call_Protection_EnterCalibMode()      Protection_EnterCalibMode()
#define Rte_Call_Calib_Save(params)              Calib_Save(params)
#define Rte_Call_Calib_ReadStored(params)        Calib_ReadStored(params)
#define Rte_Call_Calib_GetInfo(info)              Calib_GetInfo(info)
#define Rte_Call_Calib_RestoreDefaults()         Calib_RestoreDefaults()
#define Rte_Call_Protection_GetFaultMask()        Protection_GetFaultMask()
#define Rte_Call_Scheduler_GetTimeMs()           Scheduler_GetTimeMs()
#define Rte_Call_Com_MainFunction()              Com_MainFunction()

/*--------------------------------------------------------------------------------
 *                                Type Definitions
 -------------------------------------------------------------------------------*/
/* ADC clipping flags for the latest measurement window. Multiple flags can
 * be combined with bitwise OR; NONE means no near-rail samples were observed.
 * This is a bit mask, not a channel-index enum. */
typedef enum
{
    ADC_CLIPPED_NONE    = 0U,
    ADC_CLIPPED_VAC_IN  = 1U << 0,
    ADC_CLIPPED_VAC_OUT = 1U << 1,
    ADC_CLIPPED_IAC     = 1U << 2,
    ADC_CLIPPED_ILEAK   = 1U << 3,
    ADC_CLIPPED_TEMP    = 1U << 4,
    ADC_CLIPPED_ALL     = ADC_CLIPPED_VAC_IN | ADC_CLIPPED_VAC_OUT |
                          ADC_CLIPPED_IAC | ADC_CLIPPED_ILEAK | ADC_CLIPPED_TEMP
} AdcClippedMask_t;

/* Measurement publishes results and diagnostics here. Main-loop consumers
 * should read after Measurement_MainFunction() completes. This structure is
 * not an atomic snapshot for concurrent ISR/task readers. Only the latched
 * leakage_trip_flag is written independently by the DMA ISR. */
typedef struct {
	// AC Input Channel
	float V_in_rms;
	float I_in_rms;
	float P_in_active;
	float S_in_apparent;
	float PowerFactor_in;
	float Frequency_in;

	// AC Output Channel (Removed Frequency_out)
	float V_out_rms;

	// Leakage Current & Temperature
	float I_leak_rms;
	float Temperature;

	// ADC-pin DC offsets [V], measured in the same window and used by RMS/power.
	float V_in_DC_Offset;
	float V_out_DC_Offset;
	float I_in_DC_Offset;
	float I_leak_DC_Offset;

	// Emergency fault flag for leakage current (0: OK, 1: TRIP)
	volatile uint8_t leakage_trip_flag;

	// Set after the first complete measurement window; not a freshness flag.
	uint8_t startup_complete;
	// Near-rail/invalid NTC conversion; Temperature is NAN when set.
	uint8_t temperature_fault;
	// DMA sets after a stable leakage baseline; occupies the existing padding.
	volatile uint8_t fast_leakage_ready;

	// Diagnostic ADC counts before conversion to physical units.
	float leakage_adc_rms;
	float temperature_adc_average;
	// Per-window near-rail channels, using ADC_CLIPPED_* flags above.
	AdcClippedMask_t adc_clipped_mask;
	// Maxima in A, retained until MCU reset. Peak uses filtered fast-path bias
	// and starts only after fast_leakage_ready; RMS includes the first window.
	volatile float I_leak_peak_max;
	volatile float I_leak_rms_max;
	// Main-loop mirror of the ISR's retained longest fast-threshold sequence.
	volatile uint8_t leakage_over_count_max;
} MeasureResult_t;
/*--------------------------------------------------------------------------------
 *                              Variable Definitions
 -------------------------------------------------------------------------------*/
extern MeasureResult_t MeasureResult;
/* Main-loop diagnostic snapshot; float fields have a single main-loop writer.
 * The independent fast leakage flag can change between successive snapshots. */
void Rte_Read_MeasureResult(MeasureResult_t *result);
/*--------------------------------------------------------------------------------
 *                           Inline Function Prototypes
 -------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------
 *                           Public Function Prototypes
 -------------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------------
 *                           Inline Function Definitions
 -------------------------------------------------------------------------------*/

#endif /* RTE_RTE_H_ */
