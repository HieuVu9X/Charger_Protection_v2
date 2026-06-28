/*
 * OldMeasurement.c
 *
 *  Created on: Jun 10, 2026
 *      Author: Admin
 */

/*--------------------------------------------------------------------------------
 *                                 Include Files
 -------------------------------------------------------------------------------*/
#include "Measurement.h"
/*--------------------------------------------------------------------------------
 *                              Constant Definitions
 -------------------------------------------------------------------------------*/
/* ---------------------------------------------------------------------------
 * ALGORITHM CONFIGURATION CONSTANTS
 * ---------------------------------------------------------------------------*/
#define NUM_CHANNELS        5       // ADC Scan Rank Order: V_In, V_Out, I_In, I_Leak, Temp
#define HALF_WINDOW_SAMPLES 10      // Samples per channel in half buffer (60 * 5 * 2 = 600 total samples)
#define DMA_BUFFER_SIZE     (HALF_WINDOW_SAMPLES * NUM_CHANNELS * 2)
#define HALF_BUFFER_SIZE    (HALF_WINDOW_SAMPLES * NUM_CHANNELS)

#define WINDOW_SAMPLES      2000    // RMS calculation window size (200ms at Fs = 10kHz)
#define SAMPLING_FREQ       10000.0f

/* --- HARDWARE CONVERSION RATIOS --- */
#define ADC_TO_VOLT_IN_RATIO   0.156f   // Conversion factor for Input Voltage (V)
#define ADC_TO_AMP_IN_RATIO    0.025f   // Conversion factor for Input Current (A)
#define ADC_TO_ILEAK_RATIO     0.0005f  // Conversion factor for Leakage Current (A)
#define ADC_TO_VOLT_OUT_RATIO  0.156f   // Conversion factor for Output Voltage (V)
#define ADC_TO_TEMP_RATIO      0.0805f  // Conversion factor for Temperature (°C)

/* --- ADVANCED PROTECTION & FILTER CONFIGURATIONS --- */
#define I_LEAK_TRIP_THRESHOLD   60      // Fast trip threshold for leakage current (raw ADC ticks)
#define FREQ_HYSTERESIS_HIGH    50      // High threshold for Schmitt Trigger zero-crossing detection
#define FREQ_HYSTERESIS_LOW    -50      // Low threshold for Schmitt Trigger zero-crossing detection
#define FREQ_LPF_ALPHA          0.1f    // IIR Digital Low-Pass Filter coefficient for frequency
/*--------------------------------------------------------------------------------
 *                              Variable Definitions
 -------------------------------------------------------------------------------*/
/*
 * DMA Buffer
 *
 * Layout:
 *
 * CH0 CH1 CH2 CH3 CH4
 * CH0 CH1 CH2 CH3 CH4
 * CH0 CH1 CH2 CH3 CH4
 */
uint16_t adc_dma_buffer[DMA_BUFFER_SIZE];

/* --- INTERRUPT ACCUMULATOR VARIABLES --- */
static volatile uint64_t vin_acc_sq  = 0;  static volatile uint64_t vin_acc_raw  = 0;  static volatile int32_t vin_offset  = 2048;
static volatile uint64_t vout_acc_sq = 0;  static volatile uint64_t vout_acc_raw = 0;  static volatile int32_t vout_offset = 2048;
static volatile uint64_t iin_acc_sq  = 0;  static volatile uint64_t iin_acc_raw  = 0;  static volatile int32_t iin_offset  = 2048;
static volatile uint64_t ilk_acc_sq  = 0;  static volatile uint64_t ilk_acc_raw  = 0;  static volatile int32_t ilk_offset  = 2048;
static volatile uint64_t temp_acc_raw = 0;
static volatile int64_t  pin_acc = 0;

static volatile uint16_t total_sample_counter = 0;

// Frequency calculation variables
static volatile uint32_t freq_in_counter = 0;         static volatile int32_t vin_last_ac = 0;
static volatile float interp_fraction_in_curr = 0.0f; static volatile float interp_fraction_in_last = 0.0f;

/* --- SNAPSHOT BUFFER FOR MAIN THREAD PROCESSING --- */
static volatile uint64_t p_vin_sq  = 0;  static volatile uint64_t p_vin_raw  = 0;
static volatile uint64_t p_vout_sq = 0;  static volatile uint64_t p_vout_raw = 0;
static volatile uint64_t p_iin_sq  = 0;  static volatile uint64_t p_iin_raw  = 0;  static volatile int64_t p_pin = 0;
static volatile uint64_t p_ilk_sq  = 0;  static volatile uint64_t p_ilk_raw  = 0;  static volatile uint64_t p_temp_raw = 0;
static volatile float    freq_in_raw = 50.0f;   static volatile float    freq_out_raw = 50.0f;
static volatile uint8_t  data_ready_flag = 0;
/*--------------------------------------------------------------------------------
 *                           Static Function Prototypes
 -------------------------------------------------------------------------------*/
static inline void Process_Buffer_Segment(uint16_t *start_ptr, uint16_t length);
/*--------------------------------------------------------------------------------
 *                           Public Function Definitions
 -------------------------------------------------------------------------------*/
void MeasurementInit(void)
{
    memset(adc_dma_buffer, 0, sizeof(adc_dma_buffer));

    HAL_ADC_Start_DMA(&hadc, (uint32_t*)adc_dma_buffer, DMA_BUFFER_SIZE);
}

void Measurement_MainFunction(void)
{
    if (data_ready_flag == 1)
    {
        float window_f = (float)WINDOW_SAMPLES;

        // 1. Update dynamic DC Offsets to compensate hardware drift
        vin_offset  = (int32_t)(p_vin_raw / WINDOW_SAMPLES);  MeasureResult.V_in_DC_Offset  = ((float)vin_offset * 3.3f) / 4096.0f;
        vout_offset = (int32_t)(p_vout_raw / WINDOW_SAMPLES); MeasureResult.V_out_DC_Offset = ((float)vout_offset * 3.3f) / 4096.0f;
        iin_offset  = (int32_t)(p_iin_raw / WINDOW_SAMPLES);  MeasureResult.I_in_DC_Offset  = ((float)iin_offset * 3.3f) / 4096.0f;
        ilk_offset  = (int32_t)(p_ilk_raw / WINDOW_SAMPLES);  MeasureResult.I_leak_DC_Offset = ((float)ilk_offset * 3.3f) / 4096.0f;

        // 2. Compute final RMS values directly into the output structure
        MeasureResult.V_in_rms   = sqrtf((float)p_vin_sq / window_f) * ADC_TO_VOLT_IN_RATIO;
        MeasureResult.V_out_rms  = sqrtf((float)p_vout_sq / window_f) * ADC_TO_VOLT_OUT_RATIO;
        MeasureResult.I_in_rms   = sqrtf((float)p_iin_sq / window_f) * ADC_TO_AMP_IN_RATIO;
        MeasureResult.I_leak_rms = sqrtf((float)p_ilk_sq / window_f) * ADC_TO_ILEAK_RATIO;

        // 3. Compute active power (P), apparent power (S), and Power Factor (PF) for Input
        MeasureResult.P_in_active = ((float)p_pin / window_f) * ADC_TO_VOLT_IN_RATIO * ADC_TO_AMP_IN_RATIO;
        if (MeasureResult.P_in_active < 0.0f) MeasureResult.P_in_active = 0.0f;
        MeasureResult.S_in_apparent = MeasureResult.V_in_rms * MeasureResult.I_in_rms;

        if (MeasureResult.S_in_apparent > 0.1f) {
        	MeasureResult.PowerFactor_in = MeasureResult.P_in_active / MeasureResult.S_in_apparent;
            if (MeasureResult.PowerFactor_in > 1.0f) MeasureResult.PowerFactor_in = 1.0f;
        } else {
        	MeasureResult.PowerFactor_in = 1.0f;
        }

        // 4. Compute average Environment/System Temperature
        float temp_raw_avg = (float)p_temp_raw / window_f;
        MeasureResult.Temperature = temp_raw_avg * ADC_TO_TEMP_RATIO;

        // 5. Apply digital IIR Low-Pass Filtering to Frequency_in
        MeasureResult.Frequency_in  = (FREQ_LPF_ALPHA * freq_in_raw)  + ((1.0f - FREQ_LPF_ALPHA) * MeasureResult.Frequency_in);

        // Force frequency to 0Hz when main AC voltage drops/disconnected
        if (MeasureResult.V_in_rms < 10.0f)  MeasureResult.Frequency_in = 0.0f;

        data_ready_flag = 0; // Release data lock for next window cycle
    }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc_ptr)
{
    if (hadc_ptr->Instance == hadc.Instance)
    {
    	Process_Buffer_Segment(&adc_dma_buffer[0], HALF_BUFFER_SIZE);
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc_ptr)
{
	if (hadc_ptr->Instance == hadc.Instance)
	{
		Process_Buffer_Segment(&adc_dma_buffer[HALF_BUFFER_SIZE], HALF_BUFFER_SIZE);
	}
}

/*--------------------------------------------------------------------------------
 *                           Static Function Definitions
 -------------------------------------------------------------------------------*/
/* --- INTERRUPT SERVICE ROUTINES (ISR) --- */
static inline void Process_Buffer_Segment(uint16_t *start_ptr, uint16_t length)
{
    // Local register-level variables to optimize loop processing speed
    uint64_t l_vin_sq = 0;  uint64_t l_vin_raw = 0;
    uint64_t l_vout_sq = 0; uint64_t l_vout_raw = 0;
    uint64_t l_iin_sq = 0;  uint64_t l_iin_raw = 0;  int64_t l_pin = 0;
    uint64_t l_ilk_sq = 0;  uint64_t l_ilk_raw = 0;  uint64_t l_temp_raw = 0;

    int32_t c_vin_off  = vin_offset;   int32_t c_vout_off = vout_offset;
    int32_t c_iin_off  = iin_offset;   int32_t c_ilk_off  = ilk_offset;

    // Loop step i += 5 matching the specific Rank configuration:
    // index i   => Rank 1 (V_in)
    // index i+1 => Rank 2 (V_out)
    // index i+2 => Rank 3 (I_in)
    // index i+3 => Rank 4 (I_leak)
    // index i+4 => Rank 5 (Temp)
    for (uint16_t i = 0; i < length; i += NUM_CHANNELS)
    {
        uint16_t r_vin  = start_ptr[i + ADC_CH_VAC_IN];
        uint16_t r_vout = start_ptr[i + ADC_CH_VAC_OUT];
        uint16_t r_iin  = start_ptr[i + ADC_CH_IAC];
        uint16_t r_ilk  = start_ptr[i + ADC_CH_ILEAK];
        uint16_t r_temp = start_ptr[i + ADC_CH_TEMP];

        // 1. Dynamic DC Offset cancellation for AC signals
        int32_t ac_vin  = (int32_t)r_vin - c_vin_off;
        int32_t ac_vout = (int32_t)r_vout - c_vout_off;
        int32_t ac_iin  = (int32_t)r_iin - c_iin_off;
        int32_t ac_ilk  = (int32_t)r_ilk - c_ilk_off;

        // --- EMERGENCY LEAKAGE PROTECTION FAULT ---
        if (abs(ac_ilk) > I_LEAK_TRIP_THRESHOLD)
        {
        	MeasureResult.leakage_trip_flag = 1;
        }

        // 2. Accumulate raw and squared values for each channel
        l_vin_raw  += r_vin;    l_vin_sq  += (int64_t)(ac_vin * ac_vin);
        l_vout_raw += r_vout;   l_vout_sq += (int64_t)(ac_vout * ac_vout);
        l_iin_raw  += r_iin;    l_iin_sq  += (int64_t)(ac_iin * ac_iin);
        l_ilk_raw  += r_ilk;    l_ilk_sq  += (int64_t)(ac_ilk * ac_ilk);
        l_temp_raw += r_temp;
        l_pin      += (int64_t)(ac_vin * ac_iin);

        // 3. AC Input Frequency Measurement (Interpolation + Hysteresis)
        freq_in_counter++;
        if (ac_vin > 0 && vin_last_ac <= 0) {
            float denom = (float)(ac_vin - vin_last_ac);
            if (denom != 0.0f) interp_fraction_in_curr = (float)(-vin_last_ac) / denom;
            float ex_samples = (float)freq_in_counter + interp_fraction_in_curr - interp_fraction_in_last;
            if (ex_samples >= 142.0f && ex_samples <= 250.0f) freq_in_raw = SAMPLING_FREQ / ex_samples;
            interp_fraction_in_last = interp_fraction_in_curr;
            freq_in_counter = 0;
        }
        vin_last_ac = ac_vin;
    }

    // Transfer data from local segment memory to global accumulators
    vin_acc_raw  += l_vin_raw;  vin_acc_sq  += l_vin_sq;
    vout_acc_raw += l_vout_raw; vout_acc_sq += l_vout_sq;
    iin_acc_raw  += l_iin_raw;  iin_acc_sq  += l_iin_sq;
    ilk_acc_raw  += l_ilk_raw;  ilk_acc_sq  += l_ilk_sq;
    temp_acc_raw += l_temp_raw; pin_acc     += l_pin;

    total_sample_counter += (length / NUM_CHANNELS);

    // Check if the 2000 samples calculation window is reached
    if (total_sample_counter >= WINDOW_SAMPLES)
    {
        if (data_ready_flag == 0)
        {
            p_vin_sq  = vin_acc_sq;   p_vin_raw  = vin_acc_raw;
            p_vout_sq = vout_acc_sq;  p_vout_raw = vout_acc_raw;
            p_iin_sq  = iin_acc_sq;   p_iin_raw  = iin_acc_raw;
            p_ilk_sq  = ilk_acc_sq;   p_ilk_raw  = ilk_acc_raw;
            p_temp_raw = temp_acc_raw; p_pin = pin_acc;
            data_ready_flag = 1;
        }
        // Reset ISR accumulators for the next window cycle
        vin_acc_raw = 0;  vin_acc_sq = 0;  vout_acc_raw = 0;  vout_acc_sq = 0;
        iin_acc_raw = 0;  iin_acc_sq = 0;  ilk_acc_raw  = 0;  ilk_acc_sq  = 0;
        temp_acc_raw = 0; pin_acc = 0;     total_sample_counter = 0;
    }
}
