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
#include "stdint.h"
/*--------------------------------------------------------------------------------
 *                               Macro Definitions
 -------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------
 *                                Type Definitions
 -------------------------------------------------------------------------------*/
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

	// System Monitoring (Hardware circuit DC Offsets)
	float V_in_DC_Offset;
	float V_out_DC_Offset;
	float I_in_DC_Offset;
	float I_leak_DC_Offset;

	// Emergency fault flag for leakage current (0: OK, 1: TRIP)
	volatile uint8_t leakage_trip_flag;
} MeasureResult_t;
/*--------------------------------------------------------------------------------
 *                              Variable Definitions
 -------------------------------------------------------------------------------*/
extern MeasureResult_t MeasureResult;
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
