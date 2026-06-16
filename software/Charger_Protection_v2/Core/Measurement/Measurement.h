/**
  ******************************************************************************
  * @file           : measurement.h
  * @brief          : Header file cho module đo lường AC 1 pha (Áp + Dòng)
  ******************************************************************************
  */
#ifndef __MEASUREMENT_H__
#define __MEASUREMENT_H__

#include "main.h"

/* ---------------------------------------------------------------------------
 * CẤU HÌNH CÁC HẰNG SỐ THUẬT TOÁN
 * ---------------------------------------------------------------------------*/
#define NUM_CHANNELS        2
#define HALF_WINDOW_SAMPLES 100
#define DMA_BUFFER_SIZE     (HALF_WINDOW_SAMPLES * NUM_CHANNELS * 2)
#define HALF_BUFFER_SIZE    (HALF_WINDOW_SAMPLES * NUM_CHANNELS)

#define WINDOW_SAMPLES      2000
#define SAMPLING_FREQ       10000.0f

#define ADC_TO_VOLT_RATIO   0.156f
#define ADC_TO_AMP_RATIO    0.025f

/* --- CẤU HÌNH THÔNG SỐ CHỐNG NHIỄU NÂNG CAO --- */
// Ngưỡng Hysteresis cho bộ dò điểm 0 (tính bằng đơn vị ADC thô sau khi trừ offset)
// Ví dụ: 50 đơn vị ADC ứng với khoảng 50 * (3.3 / 4096) / hệ số mạch ~= vài Volt thực tế
#define FREQ_HYSTERESIS_HIGH   50
#define FREQ_HYSTERESIS_LOW   -50

// Hệ số lọc thông thấp IIR cho tần số (Alpha từ 0.0 đến 1.0)
// Càng nhỏ thì càng mịn nhưng đáp ứng chậm. 0.1f là trị số hài hòa cho hiển thị.
#define FREQ_LPF_ALPHA         0.1f

/* ---------------------------------------------------------------------------
 * KHAI BÁO CÁC HÀM PUBLIC
 * ---------------------------------------------------------------------------*/
void Measurement_Init(void);
void Measurement_MainFunction(void);

/* --- KHAI BÁO CÁC BIẾN ĐẦU RA --- */
extern float final_V_rms;
extern float final_I_rms;
extern float final_P_active;
extern float final_S_apparent;
extern float final_PowerFactor;
extern float final_Frequency;
extern float final_V_DC_Offset;
extern float final_I_DC_Offset;

#endif /* __MEASUREMENT_H__ */
