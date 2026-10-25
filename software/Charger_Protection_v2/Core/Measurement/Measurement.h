/* Measurement interface and board-specific calib. */
#ifndef MEASUREMENT_MEASUREMENT_H_
#define MEASUREMENT_MEASUREMENT_H_

#include "main.h"
#include "Rte.h"

/* Acquisition is configured by the existing ADC/TIM1 component: PA0..PA4,
 * forward scan, 12 bits, 41.5-cycle acquisition, HSI14, TIM1 TRGO at 10 kHz.
 * These constants must agree with that configuration; they do not change it. */
#define MEASUREMENT_SAMPLE_RATE_HZ              10000U
#define MEASUREMENT_WINDOW_SAMPLES              2000U
/* Nominal VDDA for conversion to volts/amperes. ADC hardware calibration does
 * not measure VDDA or the external op-amp bias; DC bias is learned separately. */
#define MEASUREMENT_ADC_REFERENCE_V             3.3f
#define MEASUREMENT_ADC_COUNTS                  4096.0f
#define MEASUREMENT_ADC_CLOCK_HZ                14000000.0f
#define MEASUREMENT_ADC_CYCLES_PER_CHANNEL      (41.5f + 12.5f)

/* Schematic: R4+R8 / R13+C6, and R3+R7 / R12+C7.
 * LMV324 U1B/U1C are unity-gain buffers. C6/C7 are 100 nF, NOT 100 pF.
 * At 50 Hz these dividers also attenuate by ~0.6955 and lag by ~45.93 deg. */
#define MEASUREMENT_VIN_HIGH_R_OHM              (4700000.0f + 4700000.0f)
#define MEASUREMENT_VIN_LOW_R_OHM               33000.0f
#define MEASUREMENT_VIN_C_F                     100.0e-9f
#define MEASUREMENT_VOUT_HIGH_R_OHM             (4700000.0f + 4700000.0f)
#define MEASUREMENT_VOUT_LOW_R_OHM              33000.0f
#define MEASUREMENT_VOUT_C_F                    100.0e-9f

/* ADC prefilter followed by noise-limited inverse RC compensation. Voltage
 * and line-current paths share two matching 1 kHz poles, preserving relative
 * phase. RMS and power are therefore band-limited, including at harmonics.
 * Nominal 50 Hz amplitude loss: ~0.25%; power loss: ~0.50%.
 * Per-sample voltage/current filters use fixed-point arithmetic; zero-crossing
 * interpolation still uses floating point when a crossing occurs. */
#define MEASUREMENT_BANDWIDTH_HZ                1000.0f
/* Separate ADC low-pass cutoffs. Ileak RMS and fast protection both use the
 * filtered signal: brief peaks can be attenuated. Temperature is much slower.
 * These are compile-time settings; thresholds/persistent Calib are unchanged. */
#define MEASUREMENT_LEAK_BANDWIDTH_HZ           1000.0f
#define MEASUREMENT_TEMP_BANDWIDTH_HZ           10.0f

/* Schematic: R11 || R14 || R15, each 0.05 ohm; R17/R19/R16/R21 = 100 ohm.
 * Positive load power: I_LINE_POS is on the load side of the neutral shunt. */
#define MEASUREMENT_SHUNT_R11_OHM               0.05f
#define MEASUREMENT_SHUNT_R14_OHM               0.05f
#define MEASUREMENT_SHUNT_R15_OHM               0.05f
#define MEASUREMENT_CURRENT_GAIN               1.0f
#define MEASUREMENT_CURRENT_POLARITY           1.0f

/* TEMP1: +3V3 -- R35 (10k) -- ADC -- RT1 (NTC) -- AGND.
 * Fitted RT1: TDK NTCG163JF103HT1, R25=10k +/-3%, B25/85=3435 K +/-1%.
 * TDK also specifies B25/50=3380 K; use B25/85 consistently in this model.
 * Source: https://product.tdk.com/system/files/dam/doc/product/sensor/ntc/chip-ntc-thermistor/data_sheet/datasheet_ntcg163jf103ht1.pdf
 * The Beta equation is an approximation, not the full manufacturer R/T curve. */
#define MEASUREMENT_NTC_PULLUP_OHM              10000.0f
#define MEASUREMENT_NTC_R25_OHM                 10000.0f
#define MEASUREMENT_NTC_BETA_K                  3435.0f
#define MEASUREMENT_NTC_T0_K                    298.15f
#define MEASUREMENT_NTC_ADC_RAIL_MARGIN         4U

/* Fitted CT: ZMCT118F, rated 5 A primary / 5 mA secondary = 1000:1.
 * The 0..30 A linear range is specified with a 50-ohm burden.
 * Source: https://www.micro-transformer.com/pcb-mounting-current-transformer-zmct118f.html
 * Those accuracy conditions do not characterize mA leakage with this board's
 * effective burden; MEASUREMENT_LEAK_CALIB requires board measurements.
 * IMPORTANT: R28=4.7k is loaded by the LMV358 difference-amplifier input:
 * R_effective = R28 || (R26+R31) = 4700 || 200 = 191.8367 ohm, at low frequency.
 * This is an active differential load for a floating CT and linear feedback,
 * not the resistance read by an ohmmeter with the board unpowered. R24=R33=100
 * with R26=R31=100 gives voltage gain 1; input-pin currents remain negligible.
 * The ratio below is independent of the fast-flag current threshold. */
#define MEASUREMENT_LEAK_CT_PRIMARY_A           5.0f
#define MEASUREMENT_LEAK_CT_SECONDARY_A         0.005f
#define MEASUREMENT_LEAK_CT_RATIO               (MEASUREMENT_LEAK_CT_PRIMARY_A / MEASUREMENT_LEAK_CT_SECONDARY_A)
#define MEASUREMENT_LEAK_BURDEN_OHM             4700.0f
#define MEASUREMENT_LEAK_R26_OHM                100.0f
#define MEASUREMENT_LEAK_R31_OHM                100.0f
#define MEASUREMENT_LEAK_GAIN                  1.0f
#define MEASUREMENT_LEAK_CALIB            1.0f

/* Fast flag requires sustained peak amplitude, not an RMS trip. It is latched.
 * A clean 200 ms window with stable half-window means seeds the fractional
 * baseline. Then a slow IIR tracks DC drift; fast/RMS leakage faults freeze
 * learning. Protection inhibits relay enable until fast_leakage_ready. */
#define MEASUREMENT_LEAK_FAST_PEAK_A            PROTECTION_LEAK_FAST_PEAK_A
/* Consecutive filtered samples at/above the peak threshold, 100 us apart.
 * Any sample below threshold cancels the candidate. Counter spans DMA blocks. */
#define MEASUREMENT_LEAK_FAST_CONFIRM_SAMPLES   20U
#define MEASUREMENT_FREQ_HYSTERESIS_COUNTS      8
#define MEASUREMENT_FREQ_MIN_HZ                40.0f
#define MEASUREMENT_FREQ_MAX_HZ                70.0f
#define MEASUREMENT_FREQ_TIMEOUT_SAMPLES        (MEASUREMENT_SAMPLE_RATE_HZ / 10U)
#define MEASUREMENT_FREQ_ALPHA                  0.1f

/* Relay synchronization uses RC-compensated, low-pass Vin, both crossing edges.
 * Hysteresis is expressed in compensated ADC counts. An accepted half-cycle
 * must be within the same 40..70 Hz limits as the frequency detector.
 * Notifications start after the initial DC-bias window and two valid edges. */
#define MEASUREMENT_ZERO_CROSS_HYSTERESIS_COUNTS 8

/* Optional gain trims after measuring the fitted board against a reference. */
#define MEASUREMENT_VIN_CALIB             1.0f
#define MEASUREMENT_VOUT_CALIB            1.0f
#define MEASUREMENT_CURRENT_CALIB         1.0f

typedef enum
{
    ADC_CH_VAC_IN = 0,
    ADC_CH_VAC_OUT,
    ADC_CH_IAC,
    ADC_CH_ILEAK,
    ADC_CH_TEMP
} AdcChannel_t;

/* Results and diagnostics are published together as MeasureResult in Rte.h.
 * Other components need only Rte.h to access ADC_CLIPPED_* and those fields. */

/* First fast-trip evidence for debugger Watch. ADC counts, not V/A. Captured
 * once at the triggering sample, valid written last; retained until reset.
 * Independent of UART/MeasureResult layout and the normal 200 ms snapshots. */
typedef struct
{
    float filtered_adc_count;
    float offset_adc_count;
    float delta_adc_count;
    int32_t threshold_adc_count;
    uint32_t sample_index;
    uint16_t raw_adc_count;
    uint8_t valid;
} FastLeakTrip_t;
extern volatile FastLeakTrip_t FastLeakTrip;

void MeasurementInit(void);
void Measurement_MainFunction(void);
/* Direct call from DMA1_Channel1_IRQHandler(): ADC1 DMA1 channel 1 HT/TC/TE. */
void Measurement_DMA_IRQHandler(void);
/* Qualified crossings call Rte_Call_Measurement_ZeroCross() in the DMA ISR.
 * RTE owns the destination callback. DMA batches 10 scans, adding up to about
 * 1 ms of notification latency; this is not a mechanical-contact prediction. */

#endif /* MEASUREMENT_MEASUREMENT_H_ */
