#ifndef PROTECTION_PARAMS_H_
#define PROTECTION_PARAMS_H_

#include <stdint.h>

/* Factory defaults: retain the thresholds used before persistent configuration. */
#define PROTECTION_OV_V                 240.0f
#define PROTECTION_UV_IN_V              190.0f
#define PROTECTION_OC_A                 2.8f
#define PROTECTION_LEAK_A               0.030f
#define PROTECTION_OT_C                 70.0f
#define PROTECTION_OV_RECOVER_V         230.0f
#define PROTECTION_UV_IN_RECOVER_V      200.0f
#define PROTECTION_OT_RECOVER_C         65.0f
#define PROTECTION_RECOVER_HOLD_MS      1000U
#define PROTECTION_LEAK_FAST_PEAK_A     0.030f

/* Wire/storage order: nine IEEE-754 float32 values, then one uint32 (40 bytes).
 * OV and OV recovery apply to both Vin and Vout. RMS and fast PEAK leakage
 * thresholds are independent; all current values are in amperes. */
typedef struct
{
    float ov_v;
    float uv_in_v;
    float oc_a;
    float leak_a;
    float ot_c;
    float ov_recover_v;
    float uv_in_recover_v;
    float ot_recover_c;
    float leak_fast_peak_a;
    uint32_t recover_hold_ms;
} ProtectionParams_t;

#define PROTECTION_DEFAULT_PARAMS {PROTECTION_OV_V, PROTECTION_UV_IN_V, \
    PROTECTION_OC_A, PROTECTION_LEAK_A, PROTECTION_OT_C, \
    PROTECTION_OV_RECOVER_V, PROTECTION_UV_IN_RECOVER_V, \
    PROTECTION_OT_RECOVER_C, PROTECTION_LEAK_FAST_PEAK_A, \
    PROTECTION_RECOVER_HOLD_MS}

#endif
