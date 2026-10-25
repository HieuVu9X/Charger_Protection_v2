/* Persistent board calib. Save is explicit; new values apply at boot. */
#ifndef CALIB_H_
#define CALIB_H_

#include <stdint.h>
#include "ProtectionParams.h"

/* Per-board tolerance corrections, applied ON TOP OF the existing hardware
 * conversion factors in Measurement. These fields do not replace the
 * resistor/shunt/CT/amplifier gains. Unity gain means no extra correction. */
typedef struct
{
    float vin_gain;
    float vout_gain;
    float current_gain;
    float leakage_gain;
    float temperature_offset_c;
} CalibParams_t;

/* Uncalibrated board: retain nominal hardware scaling and accept component
 * tolerances. No multiplicative correction and no temperature offset. */
#define CALIB_DEFAULT_PARAMS {1.0f, 1.0f, 1.0f, 1.0f, 0.0f}

typedef enum
{
    CALIB_OK = 0,
    CALIB_UNCHANGED,
    CALIB_INVALID,
    CALIB_BUSY,
    CALIB_FLASH_ERROR
} CalibStatus_t;

/* Before MeasurementInit(): load the single valid record, else defaults.
 * Returns 1 for stored parameters, 0 for defaults. Never writes Flash. */
uint8_t Calib_Init(void);
const CalibParams_t *Calib_Get(void);
typedef struct
{
    uint32_t sequence;
    uint8_t active_from_flash;
    uint8_t stored_valid;
    uint8_t reset_required;
} CalibInfo_t;
void Calib_GetInfo(CalibInfo_t *info);
CalibStatus_t Calib_ReadStored(CalibParams_t *params);
CalibStatus_t Calib_RestoreDefaults(void);
/* Gains: 0.25..4; temperature offset: -20..20 C; rejects NaN/Inf/NULL. */
uint8_t Calib_Validate(const CalibParams_t *params);
/* Main-loop only, IRQs enabled. A changed save opens/inhibits the relays and
 * stops acquisition until MCU reset, including on a Flash error. Send the
 * result to the PC before resetting. INVALID/UNCHANGED/BUSY do not enter
 * this service state. No automatic periodic saving; avoid Flash wear.
 * Values are persisted for the next boot, not applied to current results.
 * Uses one Flash page: an interrupted update can erase the old parameters;
 * the next boot uses defaults if the stored record is incomplete/invalid. */
CalibStatus_t Calib_Save(const CalibParams_t *params);

/* Protection shares the same CRC/commit record and Flash service as Calib.
 * v1 records still supply Calib; their Protection values default until saved.
 * Saving/restoring one group preserves the other group's STORED values.
 * All changes apply after reset. Get returns the immutable boot snapshot. */
const ProtectionParams_t *Calib_GetProtection(void);
CalibStatus_t Calib_ReadStoredProtection(ProtectionParams_t *params);
CalibStatus_t Calib_SaveProtection(const ProtectionParams_t *params);
CalibStatus_t Calib_RestoreProtectionDefaults(void);
/* Voltage: 1..400 V, OC: .01..30 A, RMS/peak leak: .001..1 A,
 * temperature: -40..150 C; UV < UV recovery < OV recovery < OV,
 * OT recovery < OT; recovery hold: 1..65535 ms. Rejects NaN/Inf/NULL. */
uint8_t Calib_ValidateProtection(const ProtectionParams_t *params);

#endif
