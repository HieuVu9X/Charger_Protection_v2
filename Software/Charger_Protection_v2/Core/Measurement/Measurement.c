/* ADC/DMA measurement for the supplied Charger Protection v2 schematic. */
#include "Measurement.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

_Static_assert(sizeof(float) == sizeof(uint32_t) && FLT_RADIX == 2 &&
               FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "Measurement compact math requires IEEE-754 binary32");

#define NUM_CHANNELS           5U
#define AC_CHANNELS            4U
#define HALF_WINDOW_SAMPLES    10U
#define HALF_BUFFER_SIZE       (HALF_WINDOW_SAMPLES * NUM_CHANNELS)
#define DMA_BUFFER_SIZE        (HALF_BUFFER_SIZE * 2U)
#define WINDOW_SAMPLES         MEASUREMENT_WINDOW_SAMPLES
#define SAMPLE_RATE_F          ((float)MEASUREMENT_SAMPLE_RATE_HZ)
/* Boot placeholder for the existing frequency/crossing paths only.
 * The RMS/power path processes raw ADC values and measured offsets in volts. */
#define ADC_FAST_BOOT_OFFSET   2048
#define ADC_LSB_V              (MEASUREMENT_ADC_REFERENCE_V / MEASUREMENT_ADC_COUNTS)
#define PARALLEL_R(a, b)       (((a) * (b)) / ((a) + (b)))
#define VIN_RATIO              ((MEASUREMENT_VIN_HIGH_R_OHM + MEASUREMENT_VIN_LOW_R_OHM) / MEASUREMENT_VIN_LOW_R_OHM)
#define VOUT_RATIO             ((MEASUREMENT_VOUT_HIGH_R_OHM + MEASUREMENT_VOUT_LOW_R_OHM) / MEASUREMENT_VOUT_LOW_R_OHM)
#define VIN_TAU                (PARALLEL_R(MEASUREMENT_VIN_HIGH_R_OHM, MEASUREMENT_VIN_LOW_R_OHM) * MEASUREMENT_VIN_C_F)
#define VOUT_TAU               (PARALLEL_R(MEASUREMENT_VOUT_HIGH_R_OHM, MEASUREMENT_VOUT_LOW_R_OHM) * MEASUREMENT_VOUT_C_F)
#define SHUNT_R                (1.0f / (1.0f / MEASUREMENT_SHUNT_R11_OHM + 1.0f / MEASUREMENT_SHUNT_R14_OHM + 1.0f / MEASUREMENT_SHUNT_R15_OHM))
/* Floating CT: its input-network currents are equal/opposite. Linear negative
 * feedback gives Vplus ~= Vminus, so Vct = Iinput * (R26 + R31).
 * Thus the ACTIVE differential input load is 200 ohm, despite negligible
 * current entering the op-amp pins. This is not a physical short between pins.
 * R24/R26 = R33/R31 = 1 sets the separate ADC differential voltage gain. */
#define LEAK_EFFECTIVE_R       PARALLEL_R(MEASUREMENT_LEAK_BURDEN_OHM, MEASUREMENT_LEAK_R26_OHM + MEASUREMENT_LEAK_R31_OHM)
/* Existing hardware conversion factors remain the baseline. The persistent
 * per-board tolerance trims are multiplied in Measurement_MainFunction(). */
#define VIN_V_PER_COUNT        (ADC_LSB_V * VIN_RATIO * MEASUREMENT_VIN_CALIB)
#define VOUT_V_PER_COUNT       (ADC_LSB_V * VOUT_RATIO * MEASUREMENT_VOUT_CALIB)
#define IIN_A_PER_COUNT        (ADC_LSB_V / (SHUNT_R * MEASUREMENT_CURRENT_GAIN) * MEASUREMENT_CURRENT_CALIB)
#define LEAK_A_PER_COUNT       (ADC_LSB_V * MEASUREMENT_LEAK_CT_RATIO / (LEAK_EFFECTIVE_R * MEASUREMENT_LEAK_GAIN) * MEASUREMENT_LEAK_CALIB)
#define LEAK_TRIP_COUNTS       ((int32_t)(MEASUREMENT_LEAK_FAST_PEAK_A / LEAK_A_PER_COUNT) + 1)
#define VIN_V_PER_ADC_V        (VIN_RATIO * MEASUREMENT_VIN_CALIB)
#define VOUT_V_PER_ADC_V       (VOUT_RATIO * MEASUREMENT_VOUT_CALIB)
#define IIN_A_PER_ADC_V        (MEASUREMENT_CURRENT_CALIB / (SHUNT_R * MEASUREMENT_CURRENT_GAIN))
#define LEAK_A_PER_ADC_V       (MEASUREMENT_LEAK_CT_RATIO * MEASUREMENT_LEAK_CALIB / (LEAK_EFFECTIVE_R * MEASUREMENT_LEAK_GAIN))

/* Tustin transform of (1+s*tau_divider)/(1+s*tau_bandwidth).
 * The matching current filter is 1/(1+s*tau_bandwidth).
 * b1 is derived from a+b0+b1=1 to keep exact DC gain after quantization. */
#define COEFF_SCALE            65536
#define SIGNAL_SCALE           256
/* Q19 covers 0..4095 ADC counts in int32_t, retaining fractional bias.
 * Tracking alpha=1/8192 at 10 kHz: tau ~= 0.8192 s. Division, rather than
 * signed right shift, gives symmetric truncation for both error polarities. */
#define LEAK_OFFSET_SCALE      524288
#define LEAK_OFFSET_TRACK_DIV  8192
#define LEAK_OFFSET_STABLE_COUNTS 1
#define FILTER_W               (SAMPLE_RATE_F / (3.14159265358979323846f * MEASUREMENT_BANDWIDTH_HZ))
#define FILTER_A               ((int32_t)(((FILTER_W - 1.0f) / (FILTER_W + 1.0f)) * COEFF_SCALE + 0.5f))
#define VIN_B0                 ((int32_t)(((1.0f + 2.0f * SAMPLE_RATE_F * VIN_TAU) / (1.0f + FILTER_W)) * COEFF_SCALE + 0.5f))
#define VOUT_B0                ((int32_t)(((1.0f + 2.0f * SAMPLE_RATE_F * VOUT_TAU) / (1.0f + FILTER_W)) * COEFF_SCALE + 0.5f))
#define CURRENT_B0             ((COEFF_SCALE - FILTER_A) / 2)
#define ADC_LPF_A(hz)          ((int32_t)(((SAMPLE_RATE_F - 3.14159265358979323846f * (hz)) / \
                                        (SAMPLE_RATE_F + 3.14159265358979323846f * (hz))) * COEFF_SCALE + 0.5f))
#define LEAK_FILTER_A         ADC_LPF_A(MEASUREMENT_LEAK_BANDWIDTH_HZ)
#define TEMP_FILTER_A         ADC_LPF_A(MEASUREMENT_TEMP_BANDWIDTH_HZ)
_Static_assert(LEAK_FILTER_A >= 0 && LEAK_FILTER_A < COEFF_SCALE &&
               TEMP_FILTER_A >= 0 && TEMP_FILTER_A < COEFF_SCALE,
               "ADC LPFs require positive cutoff no greater than sample_rate/pi");
/* CH2 is acquired two conversions later than CH0. Interpolate current back
 * to CH0 time before applying the matching low-pass (nominal HSI14 timing). */
#define CURRENT_SKEW_Q16       ((int32_t)(2.0f * MEASUREMENT_ADC_CYCLES_PER_CHANNEL / MEASUREMENT_ADC_CLOCK_HZ * SAMPLE_RATE_F * COEFF_SCALE + 0.5f))

/* Keep public clipping bits aligned with the hardware scan order. */
_Static_assert(ADC_CLIPPED_VAC_IN == (1U << ADC_CH_VAC_IN) &&
               ADC_CLIPPED_VAC_OUT == (1U << ADC_CH_VAC_OUT) &&
               ADC_CLIPPED_IAC == (1U << ADC_CH_IAC) &&
               ADC_CLIPPED_ILEAK == (1U << ADC_CH_ILEAK) &&
               ADC_CLIPPED_TEMP == (1U << ADC_CH_TEMP),
               "Rte clipping flags must match the ADC scan order");

#if (WINDOW_SAMPLES == 0U) || ((WINDOW_SAMPLES % HALF_WINDOW_SAMPLES) != 0U)
#error "Measurement window must be a positive multiple of DMA half-window samples"
#endif
_Static_assert((uint64_t)WINDOW_SAMPLES * 4095U * SIGNAL_SCALE <= UINT32_MAX,
               "Window raw sum must fit Q8 offset conversion");
_Static_assert((WINDOW_SAMPLES % (2U * HALF_WINDOW_SAMPLES)) == 0U,
               "Leakage startup checks two equal DMA-aligned half-windows");
_Static_assert(4095LL * LEAK_OFFSET_SCALE <= INT32_MAX,
               "Fast leakage raw values and differences must fit int32_t");
_Static_assert(MEASUREMENT_LEAK_FAST_CONFIRM_SAMPLES >= 1U &&
               MEASUREMENT_LEAK_FAST_CONFIRM_SAMPLES <= UINT8_MAX,
               "Fast leakage confirmation counter must fit uint8_t");

typedef struct
{
    int32_t previous_input;
    int32_t previous_output;
    uint8_t initialized;
} FilterState_t;

typedef struct
{
    int64_t sum[AC_CHANNELS];        /* Filtered raw ADC counts in Q8, no midpoint subtraction */
    uint64_t sum_sq[AC_CHANNELS];    /* Squares of filtered raw ADC, Q16 */
    uint32_t raw_sum[NUM_CHANNELS];
    uint32_t temperature_sum_q8;    /* Filtered NTC ADC counts, Q8 */
    int64_t power_sum;              /* Q16 */
    uint32_t clipped_mask;
    float frequency_hz;
} Window_t;

uint16_t adc_dma_buffer[DMA_BUFFER_SIZE];
volatile FastLeakTrip_t FastLeakTrip;
static Window_t accumulator;
static volatile Window_t pending_window;
static uint16_t sample_count;
static volatile uint8_t data_ready_flag;
/* Boot placeholders only. DMA replaces every channel with its measured raw
 * mean after each 200 ms window, even if main has not consumed pending data.
 * Relay crossings stay disarmed until the first mean exists. Fast leakage
 * has its own fractional offset and startup qualification below.
 * RMS/power use separate same-window measured offsets in volts. */
static int32_t adc_offset[AC_CHANNELS] = {ADC_FAST_BOOT_OFFSET, ADC_FAST_BOOT_OFFSET,
                                        ADC_FAST_BOOT_OFFSET, ADC_FAST_BOOT_OFFSET};
static uint8_t adc_bias_ready;
static int32_t leak_trip_counts = LEAK_TRIP_COUNTS;
static int32_t leakage_offset_q19;
static volatile int32_t leakage_peak_q19;
static int32_t leakage_peak_published_q19;
/* Volatile makes each transition observable in debugger RAM reads. The
 * high-water mark retains brief candidates that live polling can miss. */
static volatile uint8_t leakage_over_count;
static volatile uint8_t leakage_over_count_max;
static uint32_t leakage_first_half_sum;
/* Main sets this latch on an RMS leakage fault; ISR only reads it. */
static volatile uint8_t leakage_offset_hold;
static FilterState_t vin_filter, vout_filter, iin_filter, leak_filter, temperature_filter;
static FilterState_t vin_adc_filter, vout_adc_filter, iin_adc_filter;
static int32_t previous_iin;
static uint8_t iin_initialized;

/* Zero-crossing timestamps use unsigned differences, including counter wrap. */
static uint32_t sample_index, last_cross_index, candidate_index, last_valid_index;
static float last_cross_fraction, candidate_fraction, frequency_raw;
static int32_t previous_vin_ac;
static uint8_t frequency_armed, candidate_ready, previous_cross_ready;

/* Separate from raw-ADC frequency detection: compensated Vin avoids the
 * divider's ~46 degree lag at 50 Hz. Confirm both polarities with hysteresis;
 * accept only plausible half-cycle spacing, rejecting DC/noise/startup. */
static int8_t zero_cross_polarity;
static uint8_t zero_cross_valid_edges;
static uint32_t zero_cross_last_index;

/* On this Cortex-M0 toolchain, a direct uint64_t/int64_t -> float cast pulls
 * in double-precision add/multiply/conversion helpers (~3.8 KiB). Split into
 * 32-bit limbs and use the existing single-precision helpers instead.
 * Two-step rounding differs from a direct cast by at most one float ULP. */
static float Unsigned64ToFloat(uint64_t value)
{
    return (float)(uint32_t)(value >> 32) * 4294967296.0f
         + (float)(uint32_t)value;
}

static float Signed64ToFloat(int64_t value)
{
    uint64_t magnitude = (uint64_t)value;
    if (value < 0) magnitude = 0U - magnitude;
    float result = Unsigned64ToFloat(magnitude);
    return value < 0 ? -result : result;
}

typedef union
{
    float value;
    uint32_t bits;
} FloatBits_t;

/* IEEE-754 subtraction is addition with the second operand's sign inverted.
 * The Cortex-M0 library has a separate 1224-byte subtraction routine, although
 * addition is already linked. Calling the ARM EABI add entry explicitly
 * prevents GCC from recreating that helper.
 * No fast-math or reassociation: retain infinities, signed zeros and rounding. */
static inline float MeasurementSubtract(float left, float right)
{
    FloatBits_t negative;
    negative.value = right;
    negative.bits ^= UINT32_C(0x80000000);
#if defined(__arm__) && !defined(__ARM_FP)
    extern float __aeabi_fadd(float, float);
    return __aeabi_fadd(left, negative.value);
#else
    return left + negative.value;
#endif
}

#if defined(__arm__) && !defined(__ARM_FP)
/* Supply the compiler's soft-float subtraction entry for every module. Keep
 * this symbol through LTO: compiler-generated references appear after LTO. */
__attribute__((used, externally_visible))
float __aeabi_fsub(float left, float right)
{
    return MeasurementSubtract(left, right);
}
#endif

/* Local finite-domain math avoids libm's general-purpose/error/errno paths.
 * The RMS caller supplies zero or a positive NORMAL finite mean-square. The
 * exponent seed followed by four Newton steps is accurate to float precision
 * over that domain. Acquisition/window bounds keep mean-square above FLT_MIN. */
static float MeasurementSqrt(float value)
{
    if (value <= 0.0f) return 0.0f;
    FloatBits_t estimate;
    estimate.value = value;
    estimate.bits = (estimate.bits >> 1) + 0x1fc00000U;
    float root = estimate.value;
    for (uint32_t i = 0U; i < 4U; i++)
        root = 0.5f * (root + value / root);
    return root;
}

/* NTC resistance ratios are positive normal finite values. Normalize to
 * m in [1/sqrt(2), sqrt(2)], then ln(m)=2*(z+z^3/3+z^5/5+z^7/7),
 * z=(m-1)/(m+1). Here |z|<=0.171573; series truncation is <3.1e-8.
 * Actual float rounding and full NTC conversion are covered by host tests. */
static float MeasurementLog(float value)
{
    FloatBits_t normalized;
    normalized.value = value;
    int32_t exponent = (int32_t)(normalized.bits >> 23) - 127;
    normalized.bits = (normalized.bits & 0x007fffffU) | 0x3f800000U;
    float mantissa = normalized.value;
    if (mantissa > 1.41421356237f)
    {
        mantissa *= 0.5f;
        exponent++;
    }
    float z = (mantissa - 1.0f) / (mantissa + 1.0f);
    float z2 = z * z;
    float series = 1.0f + z2 * (1.0f / 3.0f + z2 * (1.0f / 5.0f + z2 * (1.0f / 7.0f)));
    return (float)exponent * 0.69314718056f + 2.0f * z * series;
}

static int32_t FilterSample(FilterState_t *state, int32_t input, int32_t b0, int32_t a)
{
    if (state->initialized == 0U)
    {
        state->previous_input = input;
        state->previous_output = input;
        state->initialized = 1U;
    }
    int32_t b1 = COEFF_SCALE - a - b0;
    int64_t output = (int64_t)a * state->previous_output
                   + (int64_t)b0 * input + (int64_t)b1 * state->previous_input;
    state->previous_input = input;
    state->previous_output = (int32_t)(output / COEFF_SCALE);
    return state->previous_output;
}

static int32_t FilterAdcSample(FilterState_t *state, int32_t raw, int32_t a)
{
    /* Q16 state avoids the large rounding deadband of a slow Q8 NTC filter.
     * Return rounded Q8 to match the existing RMS accumulators. Positive LPF
     * weights keep the state inside the ADC range, including full-scale steps.
     * FilterSample seeds from the first actual sample, not from zero/2048. */
    int32_t q16 = FilterSample(state, raw * COEFF_SCALE, (COEFF_SCALE - a) / 2, a);
    const int32_t divisor = COEFF_SCALE / SIGNAL_SCALE;
    return (q16 + divisor / 2) / divisor;
}

static void ProcessFrequency(int32_t vin_ac)
{
    sample_index++;
    if (vin_ac <= -MEASUREMENT_FREQ_HYSTERESIS_COUNTS * SIGNAL_SCALE)
    {
        frequency_armed = 1U;
        candidate_ready = 0U;
    }
    if (frequency_armed != 0U)
    {
        if (vin_ac > 0 && previous_vin_ac <= 0)
        {
            candidate_fraction = (float)(-previous_vin_ac) / (float)(vin_ac - previous_vin_ac);
            candidate_index = sample_index;
            candidate_ready = 1U;
        }
        /* Confirm at +hysteresis, but keep the interpolated ZERO timestamp. */
        if (vin_ac >= MEASUREMENT_FREQ_HYSTERESIS_COUNTS * SIGNAL_SCALE && candidate_ready != 0U)
        {
            if (previous_cross_ready != 0U)
            {
                float period = (float)(uint32_t)(candidate_index - last_cross_index) +
                               candidate_fraction - last_cross_fraction;
                if (period >= SAMPLE_RATE_F / MEASUREMENT_FREQ_MAX_HZ &&
                    period <= SAMPLE_RATE_F / MEASUREMENT_FREQ_MIN_HZ)
                {
                    frequency_raw = SAMPLE_RATE_F / period;
                    last_valid_index = sample_index;
                }
            }
            last_cross_index = candidate_index;
            last_cross_fraction = candidate_fraction;
            previous_cross_ready = 1U;
            frequency_armed = 0U;
            candidate_ready = 0U;
        }
    }
    if ((uint32_t)(sample_index - last_valid_index) > MEASUREMENT_FREQ_TIMEOUT_SAMPLES)
    {
        frequency_raw = 0.0f;
    }
    if ((uint32_t)(sample_index - last_cross_index) > MEASUREMENT_FREQ_TIMEOUT_SAMPLES)
    {
        previous_cross_ready = 0U;
    }
    previous_vin_ac = vin_ac;
}

/* Compute mean((sample_adc_volts - offset_v)^2), without a raw-sample buffer
 * or floating-point work in the DMA ISR. offset_v is measured from THIS window
 * and is also published as MeasureResult.*_DC_Offset.
 *
 * First translate the integer moments near the measured DC level. Converting
 * the uncentered moments to float first would subtract ~1.65^2 from ~1.65^2
 * and lose tiny leakage signals. center_q8 is a per-window numeric aid, never
 * a fixed ADC midpoint. The remaining offset subtraction is explicitly volts. */
static float WindowAcMeanSquare(const Window_t *window, uint32_t ch,
                                float offset_v, float *ac_mean_v)
{
    int32_t center_q8 = (int32_t)((window->raw_sum[ch] * SIGNAL_SCALE) / WINDOW_SAMPLES);
    int64_t centered_sum = window->sum[ch] - (int64_t)center_q8 * WINDOW_SAMPLES;
    int64_t centered_square_sum = (int64_t)window->sum_sq[ch]
        - 2 * center_q8 * window->sum[ch] + (int64_t)center_q8 * center_q8 * WINDOW_SAMPLES;
    const float volts_per_q8 = ADC_LSB_V / SIGNAL_SCALE;
    float centered_mean_v = Signed64ToFloat(centered_sum) *
                            (volts_per_q8 / WINDOW_SAMPLES);
    float residual_offset_v = offset_v - (float)center_q8 * volts_per_q8;
    float mean_square_v2 = Unsigned64ToFloat((uint64_t)centered_square_sum) *
        (volts_per_q8 * volts_per_q8 / WINDOW_SAMPLES)
        - 2.0f * residual_offset_v * centered_mean_v
        + residual_offset_v * residual_offset_v;
    *ac_mean_v = centered_mean_v - residual_offset_v;
    return mean_square_v2 > 0.0f ? mean_square_v2 : 0.0f;
}

static float WindowAcPower(const Window_t *window, const float *offset_v,
                           const float *ac_mean_v)
{
    int32_t vin_center = (int32_t)((window->raw_sum[ADC_CH_VAC_IN] * SIGNAL_SCALE) / WINDOW_SAMPLES);
    int32_t iin_center = (int32_t)((window->raw_sum[ADC_CH_IAC] * SIGNAL_SCALE) / WINDOW_SAMPLES);
    int64_t centered_product_sum = window->power_sum
        - vin_center * window->sum[ADC_CH_IAC]
        - iin_center * window->sum[ADC_CH_VAC_IN]
        + (int64_t)vin_center * iin_center * WINDOW_SAMPLES;
    const float volts_per_q8 = ADC_LSB_V / SIGNAL_SCALE;
    float vin_residual = offset_v[ADC_CH_VAC_IN] - (float)vin_center * volts_per_q8;
    float iin_residual = offset_v[ADC_CH_IAC] - (float)iin_center * volts_per_q8;
    /* mean((Vin_adc-offset_vin)*(Iin_adc-offset_iin)), in ADC volts squared. */
    return Signed64ToFloat(centered_product_sum) *
        (volts_per_q8 * volts_per_q8 / WINDOW_SAMPLES)
        - vin_residual * ac_mean_v[ADC_CH_IAC]
        - iin_residual * ac_mean_v[ADC_CH_VAC_IN]
        - vin_residual * iin_residual;
}

static void ProcessZeroCrossing(int32_t vin_ac_q8)
{
    if (adc_bias_ready == 0U) return;
    const int32_t hysteresis = MEASUREMENT_ZERO_CROSS_HYSTERESIS_COUNTS * SIGNAL_SCALE;
    int8_t polarity = 0;
    if (vin_ac_q8 >= hysteresis) polarity = 1;
    else if (vin_ac_q8 <= -hysteresis) polarity = -1;
    if (polarity == 0 || polarity == zero_cross_polarity) return;

    uint32_t elapsed = sample_index - zero_cross_last_index;
    if (zero_cross_polarity != 0 &&
        elapsed >= MEASUREMENT_SAMPLE_RATE_HZ / (2U * (uint32_t)MEASUREMENT_FREQ_MAX_HZ) &&
        elapsed <= MEASUREMENT_SAMPLE_RATE_HZ / (2U * (uint32_t)MEASUREMENT_FREQ_MIN_HZ))
    {
        if (zero_cross_valid_edges < 2U) zero_cross_valid_edges++;
        if (zero_cross_valid_edges >= 2U) Rte_Call_Measurement_ZeroCross();
    }
    else
        zero_cross_valid_edges = 0U;

    zero_cross_polarity = polarity;
    zero_cross_last_index = sample_index;
}

static float ConvertTemperature(float adc_average)
{
    if (adc_average <= (float)MEASUREMENT_NTC_ADC_RAIL_MARGIN ||
        adc_average >= MEASUREMENT_ADC_COUNTS - (float)MEASUREMENT_NTC_ADC_RAIL_MARGIN)
    {
        MeasureResult.temperature_fault = 1U;
        return NAN;
    }
    /* Ratiometric divider: VDD cancels because pull-up and ADC use +3V3. */
    float resistance = MEASUREMENT_NTC_PULLUP_OHM * adc_average / (MEASUREMENT_ADC_COUNTS - adc_average);
    float inverse_kelvin = 1.0f / MEASUREMENT_NTC_T0_K
                        + MeasurementLog(resistance / MEASUREMENT_NTC_R25_OHM) / MEASUREMENT_NTC_BETA_K;
    MeasureResult.temperature_fault = 0U;
    return 1.0f / inverse_kelvin - 273.15f;
}

static void ProcessFastLeakage(int32_t filtered_q8, uint16_t raw)
{
    if (MeasureResult.fast_leakage_ready == 0U)
    {
        leakage_over_count = 0U;
        return;
    }
    int32_t error = filtered_q8 * (LEAK_OFFSET_SCALE / SIGNAL_SCALE) - leakage_offset_q19;
    int32_t magnitude = abs(error);
    if (magnitude > leakage_peak_q19) leakage_peak_q19 = magnitude;
    /* Continue retaining larger peaks after a trip, while the trip snapshot
     * and the frozen baseline still describe the first triggering sample. */
    if (MeasureResult.leakage_trip_flag != 0U) return;
    /* Decide BEFORE learning: a fault sample must not move its own baseline.
     * Preserve the existing configured, integer-count peak threshold. */
    if (magnitude >= leak_trip_counts * LEAK_OFFSET_SCALE)
    {
        /* Keep the candidate baseline fixed. Qualify each ADC sample, not
         * each DMA interrupt; retain the counter across buffer/window edges. */
        if (leakage_over_count < MEASUREMENT_LEAK_FAST_CONFIRM_SAMPLES)
            leakage_over_count++;
        if (leakage_over_count > leakage_over_count_max)
            leakage_over_count_max = leakage_over_count;
        if (leakage_over_count < MEASUREMENT_LEAK_FAST_CONFIRM_SAMPLES) return;
        MeasureResult.leakage_trip_flag = 1U;
        /* Event-only conversions: no extra float work on healthy samples.
         * Read valid==1 before inspecting this immutable first-trip evidence. */
        FastLeakTrip.filtered_adc_count = (float)filtered_q8 / SIGNAL_SCALE;
        FastLeakTrip.offset_adc_count = (float)leakage_offset_q19 / LEAK_OFFSET_SCALE;
        FastLeakTrip.delta_adc_count = (float)error / LEAK_OFFSET_SCALE;
        FastLeakTrip.threshold_adc_count = leak_trip_counts;
        FastLeakTrip.sample_index = sample_index;
        FastLeakTrip.raw_adc_count = raw;
        FastLeakTrip.valid = 1U;
    }
    else
    {
        leakage_over_count = 0U;
        if (leakage_offset_hold == 0U &&
            (accumulator.clipped_mask & ADC_CLIPPED_ILEAK) == 0U)
            leakage_offset_q19 += error / LEAK_OFFSET_TRACK_DIV;
    }
}

static void Process_Buffer_Segment(uint16_t *start_ptr, uint16_t length)
{
    for (uint16_t i = 0U; i < length; i += NUM_CHANNELS)
    {
        int32_t signal[AC_CHANNELS];
        for (uint32_t ch = 0U; ch < NUM_CHANNELS; ch++)
        {
            uint16_t raw = start_ptr[i + ch];
            accumulator.raw_sum[ch] += raw;
            if (raw <= 2U || raw >= 4093U)
                accumulator.clipped_mask |= 1UL << ch;
        }
        /* Suppress ADC noise before the inverse-RC compensation can amplify
         * it. Matching the extra pole in Iin preserves voltage/current phase. */
        int32_t vin_q8 = FilterAdcSample(&vin_adc_filter, start_ptr[i + ADC_CH_VAC_IN], FILTER_A);
        int32_t vout_q8 = FilterAdcSample(&vout_adc_filter, start_ptr[i + ADC_CH_VAC_OUT], FILTER_A);
        int32_t iin_q8 = FilterAdcSample(&iin_adc_filter, start_ptr[i + ADC_CH_IAC], FILTER_A);
        int32_t leak = start_ptr[i + ADC_CH_ILEAK];

        signal[ADC_CH_VAC_IN] = FilterSample(&vin_filter, vin_q8, VIN_B0, FILTER_A);
        signal[ADC_CH_VAC_OUT] = FilterSample(&vout_filter, vout_q8, VOUT_B0, FILTER_A);
        if (iin_initialized == 0U)
        {
            previous_iin = iin_q8;
            iin_initialized = 1U;
        }
        int32_t aligned_iin = iin_q8
            - (int32_t)((int64_t)(iin_q8 - previous_iin) * CURRENT_SKEW_Q16 / COEFF_SCALE);
        previous_iin = iin_q8;
        signal[ADC_CH_IAC] = FilterSample(&iin_filter, aligned_iin, CURRENT_B0, FILTER_A);
        signal[ADC_CH_ILEAK] = FilterAdcSample(&leak_filter, leak, LEAK_FILTER_A);
        accumulator.temperature_sum_q8 += (uint32_t)FilterAdcSample(
            &temperature_filter, start_ptr[i + ADC_CH_TEMP], TEMP_FILTER_A);

        for (uint32_t ch = 0U; ch < AC_CHANNELS; ch++)
        {
            accumulator.sum[ch] += signal[ch];
            accumulator.sum_sq[ch] += (uint64_t)((int64_t)signal[ch] * signal[ch]);
        }
        accumulator.power_sum += (int64_t)signal[ADC_CH_VAC_IN] * signal[ADC_CH_IAC];

        ProcessFastLeakage(signal[ADC_CH_ILEAK], (uint16_t)leak);
        ProcessFrequency(vin_q8 - adc_offset[ADC_CH_VAC_IN] * SIGNAL_SCALE);
        ProcessZeroCrossing(signal[ADC_CH_VAC_IN] - adc_offset[ADC_CH_VAC_IN] * SIGNAL_SCALE);
    }
    sample_count += length / NUM_CHANNELS;
    if (sample_count == WINDOW_SAMPLES / 2U)
        leakage_first_half_sum = accumulator.raw_sum[ADC_CH_ILEAK];
    if (sample_count >= WINDOW_SAMPLES)
    {
        if (MeasureResult.fast_leakage_ready == 0U &&
            (accumulator.clipped_mask & ADC_CLIPPED_ILEAK) == 0U &&
            abs((int32_t)accumulator.raw_sum[ADC_CH_ILEAK] -
                2 * (int32_t)leakage_first_half_sum) <=
                (int32_t)(WINDOW_SAMPLES / 2U) * LEAK_OFFSET_STABLE_COUNTS)
        {
            /* Seed from a measured stable mean, never from 2048. Q8 seed
             * arithmetic fits uint32_t; subsequent tracking retains Q19. */
            leakage_offset_q19 = (int32_t)((accumulator.raw_sum[ADC_CH_ILEAK] *
                SIGNAL_SCALE) / WINDOW_SAMPLES) * (LEAK_OFFSET_SCALE / SIGNAL_SCALE);
            MeasureResult.fast_leakage_ready = 1U;
        }
        accumulator.frequency_hz = frequency_raw;
        if (data_ready_flag == 0U)
        {
            pending_window = accumulator;
            data_ready_flag = 1U;
        }
        /* Legacy time-domain means for frequency/crossing, independent of
         * fast leakage and same-window RMS offsets. */
        for (uint32_t ch = 0U; ch < AC_CHANNELS; ch++)
            adc_offset[ch] = (int32_t)((accumulator.raw_sum[ch] + WINDOW_SAMPLES / 2U) / WINDOW_SAMPLES);
        adc_bias_ready = 1U;
        memset(&accumulator, 0, sizeof(accumulator));
        sample_count = 0U;
    }
}

void MeasurementInit(void)
{
    MeasureResult.fast_leakage_ready = 0U;
    FastLeakTrip = (FastLeakTrip_t){0};
    MeasureResult.I_leak_peak_max = 0.0f;
    MeasureResult.I_leak_rms_max = 0.0f;
    leakage_peak_q19 = leakage_peak_published_q19 = 0;
    leakage_over_count = 0U;
    leakage_over_count_max = 0U;
    MeasureResult.leakage_over_count_max = 0U;
    leakage_offset_q19 = 0;
    leakage_first_half_sum = 0U;
    leakage_offset_hold = 0U;
    memset(&leak_filter, 0, sizeof(leak_filter));
    memset(&temperature_filter, 0, sizeof(temperature_filter));
    memset(&vin_adc_filter, 0, sizeof(vin_adc_filter));
    memset(&vout_adc_filter, 0, sizeof(vout_adc_filter));
    memset(&iin_adc_filter, 0, sizeof(iin_adc_filter));
    leak_trip_counts = (int32_t)(Rte_Call_Calib_GetProtection()->leak_fast_peak_a /
        (LEAK_A_PER_COUNT * Rte_Call_Calib_Get()->leakage_gain)) + 1;
    /* Call once after MX_ADC_Init/MX_DMA_Init/MX_TIM1_Init, while ADC disabled. */
    memset(adc_dma_buffer, 0, sizeof(adc_dma_buffer));
    LL_TIM_DisableCounter(TIM1);
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_1);
    LL_ADC_REG_SetDMATransfer(ADC1, LL_ADC_REG_DMA_TRANSFER_NONE);
    LL_ADC_StartCalibration(ADC1);
    while (LL_ADC_IsCalibrationOnGoing(ADC1) != 0U) {}
    LL_mDelay(1U);
    LL_ADC_REG_SetDMATransfer(ADC1, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
    LL_DMA_ConfigAddresses(DMA1, LL_DMA_CHANNEL_1,
                          LL_ADC_DMA_GetRegAddr(ADC1, LL_ADC_DMA_REG_REGULAR_DATA),
                          (uint32_t)adc_dma_buffer, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_1, DMA_BUFFER_SIZE);
    LL_DMA_ClearFlag_HT1(DMA1);
    LL_DMA_ClearFlag_TC1(DMA1);
    LL_DMA_ClearFlag_TE1(DMA1);
    LL_DMA_EnableIT_HT(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_1);
    LL_ADC_ClearFlag_ADRDY(ADC1);
    LL_ADC_Enable(ADC1);
    while (LL_ADC_IsActiveFlag_ADRDY(ADC1) == 0U) {}
    LL_TIM_SetCounter(TIM1, 0U);
    LL_TIM_ClearFlag_UPDATE(TIM1);
    LL_ADC_REG_StartConversion(ADC1);
    LL_TIM_EnableCounter(TIM1);
}

float offset_v[AC_CHANNELS];
float ac_mean_v[AC_CHANNELS];
float rms_adc_v[AC_CHANNELS];
void Measurement_MainFunction(void)
{
    /* Publish even without a full RMS window; the ISR counter is one byte. */
    MeasureResult.leakage_over_count_max = leakage_over_count_max;
    /* One aligned ISR-owned integer snapshot; a newer peak arriving during
     * conversion is published next time. Float only on new records in main. */
    int32_t peak = leakage_peak_q19;
    if (peak > leakage_peak_published_q19)
    {
        MeasureResult.I_leak_peak_max = (float)peak *
            (LEAK_A_PER_COUNT / LEAK_OFFSET_SCALE) * Rte_Call_Calib_Get()->leakage_gain;
        leakage_peak_published_q19 = peak;
    }
    if (data_ready_flag == 0U) return;
    /* ISR does not overwrite this snapshot while the flag is set. Release
     * after copying, so expensive math never blocks the next snapshot. */
    Window_t window = pending_window;
    data_ready_flag = 0U;
//    float offset_v[AC_CHANNELS];
//    float ac_mean_v[AC_CHANNELS];
//    float rms_adc_v[AC_CHANNELS];
    for (uint32_t ch = 0U; ch < AC_CHANNELS; ch++)
    {
        offset_v[ch] = (float)window.raw_sum[ch] * (ADC_LSB_V / WINDOW_SAMPLES);
        rms_adc_v[ch] = MeasurementSqrt(WindowAcMeanSquare(&window, ch, offset_v[ch], &ac_mean_v[ch]));
    }
    /* The four real ADC-voltage offsets used above are the values reported
     * to the PC. No assumed midpoint or previous-window RMS offset is used. */
    const CalibParams_t *cal = Rte_Call_Calib_Get();
    /* With default trim = 1, these are exactly the original hardware scales;
     * component tolerances are accepted until the board is calibrated. */
    float vin_scale = VIN_V_PER_ADC_V * cal->vin_gain;
    float current_scale = IIN_A_PER_ADC_V * cal->current_gain;
    MeasureResult.V_in_rms = rms_adc_v[ADC_CH_VAC_IN] * vin_scale;
    MeasureResult.V_out_rms = rms_adc_v[ADC_CH_VAC_OUT] * VOUT_V_PER_ADC_V * cal->vout_gain;
    MeasureResult.I_in_rms = rms_adc_v[ADC_CH_IAC] * current_scale;
    MeasureResult.I_leak_rms = rms_adc_v[ADC_CH_ILEAK] * LEAK_A_PER_ADC_V * cal->leakage_gain;
    if (MeasureResult.I_leak_rms > MeasureResult.I_leak_rms_max)
        MeasureResult.I_leak_rms_max = MeasureResult.I_leak_rms;
    if (MeasureResult.I_leak_rms >= Rte_Call_Calib_GetProtection()->leak_a)
        leakage_offset_hold = 1U;
    MeasureResult.P_in_active = WindowAcPower(&window, offset_v, ac_mean_v) *
        vin_scale * current_scale * MEASUREMENT_CURRENT_POLARITY;
    if (MeasureResult.P_in_active < 0.0f) MeasureResult.P_in_active = 0.0f;
    MeasureResult.S_in_apparent = MeasureResult.V_in_rms * MeasureResult.I_in_rms;
    MeasureResult.PowerFactor_in = MeasureResult.S_in_apparent > 0.1f
        ? MeasureResult.P_in_active / MeasureResult.S_in_apparent : 1.0f;
    if (MeasureResult.PowerFactor_in > 1.0f) MeasureResult.PowerFactor_in = 1.0f;

    MeasureResult.V_in_DC_Offset = offset_v[ADC_CH_VAC_IN];
    MeasureResult.V_out_DC_Offset = offset_v[ADC_CH_VAC_OUT];
    MeasureResult.I_in_DC_Offset = offset_v[ADC_CH_IAC];
    MeasureResult.I_leak_DC_Offset = offset_v[ADC_CH_ILEAK];
    MeasureResult.temperature_adc_average = (float)window.temperature_sum_q8 /
                                            (float)(WINDOW_SAMPLES * SIGNAL_SCALE);
    /* Do not let the NTC LPF postpone a whole-window open/short diagnostic. */
    float raw_temperature_average = (float)window.raw_sum[ADC_CH_TEMP] / (float)WINDOW_SAMPLES;
    if (raw_temperature_average <= (float)MEASUREMENT_NTC_ADC_RAIL_MARGIN ||
        raw_temperature_average >= MEASUREMENT_ADC_COUNTS - (float)MEASUREMENT_NTC_ADC_RAIL_MARGIN)
        MeasureResult.temperature_adc_average = raw_temperature_average;
    MeasureResult.Temperature = ConvertTemperature(MeasureResult.temperature_adc_average) + cal->temperature_offset_c;
    MeasureResult.leakage_adc_rms = rms_adc_v[ADC_CH_ILEAK] / ADC_LSB_V;
    MeasureResult.adc_clipped_mask = (AdcClippedMask_t)window.clipped_mask;

    if (MeasureResult.V_in_rms < 10.0f || window.frequency_hz == 0.0f)
        MeasureResult.Frequency_in = 0.0f;
    else if (MeasureResult.Frequency_in == 0.0f)
        MeasureResult.Frequency_in = window.frequency_hz;
    else
        MeasureResult.Frequency_in += MEASUREMENT_FREQ_ALPHA * (window.frequency_hz - MeasureResult.Frequency_in);
    MeasureResult.startup_complete = 1U;
}

/* Called directly by DMA1_Channel1_IRQHandler() in stm32f0xx_it.c.
 * Source: ADC1 transfers on DMA1 channel 1 (HT/TC data, TE transfer error). */
void Measurement_DMA_IRQHandler(void)
{
    if (LL_DMA_IsActiveFlag_TE1(DMA1) != 0U)
    {
        LL_DMA_ClearFlag_TE1(DMA1);
        LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_1);
        LL_TIM_DisableCounter(TIM1);
        Rte_Call_Application_ErrorHandler();
        return;
    }
    if (LL_DMA_IsActiveFlag_HT1(DMA1) != 0U)
    {
        LL_DMA_ClearFlag_HT1(DMA1);
        Process_Buffer_Segment(&adc_dma_buffer[0], HALF_BUFFER_SIZE);
    }
    if (LL_DMA_IsActiveFlag_TC1(DMA1) != 0U)
    {
        LL_DMA_ClearFlag_TC1(DMA1);
        Process_Buffer_Segment(&adc_dma_buffer[HALF_BUFFER_SIZE], HALF_BUFFER_SIZE);
    }
}
