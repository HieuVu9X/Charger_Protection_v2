/**
  ******************************************************************************
  * @file           : measurement.c
  * @brief          : Source file tính RMS Áp, RMS Dòng, Tần số và 2 DC Offset
  ******************************************************************************
  */
#include "measurement.h"
#include "math.h"

extern ADC_HandleTypeDef hadc;

/* ---------------------------------------------------------------------------
 * CÁC BIẾN NỘI BỘ (PRIVATE VARIABLES)
 * ---------------------------------------------------------------------------*/
uint16_t adc_dma_buffer[DMA_BUFFER_SIZE];

// Biến tích lũy cho Điện áp (Voltage)
static volatile uint64_t v_accumulated_sq_sum = 0;
static volatile uint64_t v_accumulated_raw_sum = 0;
static volatile int32_t v_dynamic_offset = 2048;

// Biến tích lũy cho Dòng điện (Current)
static volatile uint64_t i_accumulated_sq_sum = 0;
static volatile uint64_t i_accumulated_raw_sum = 0;
static volatile int32_t i_dynamic_offset = 2048;

static volatile int64_t p_accumulated_sum = 0;

// Biến đếm và đo tần số (đo theo kênh Điện áp)
static volatile uint16_t total_sample_counter = 0;
static volatile uint32_t freq_sample_counter = 0;
static volatile int32_t v_last_ac = 0;
static volatile uint8_t state_schmitt = 0; // 0: vùng âm, 1: vùng dương

// Biến nội suy lưu trữ phần phân số lệch pha (0.0f -> 1.0f)
static volatile float interp_fraction_current = 0.0f;
static volatile float interp_fraction_last = 0.0f;

// Biến kết nối dữ liệu ra luồng Main
static volatile uint64_t v_rms_processing_sum = 0;
static volatile uint64_t v_offset_processing_sum = 0;
static volatile uint64_t i_rms_processing_sum = 0;
static volatile uint64_t i_offset_processing_sum = 0;
static volatile int64_t  p_processing_sum = 0;
static volatile float    freq_processing_raw = 50.0f;
static volatile uint8_t  data_ready_flag = 0;

/* ---------------------------------------------------------------------------
 * CÁC BIẾN ĐẦU RA CÔNG KHAI (PUBLIC VARIABLES)
 * ---------------------------------------------------------------------------*/
float final_V_rms = 0.0f;
float final_I_rms = 0.0f;
float final_Frequency = 0.0f;
float final_V_DC_Offset = 2.048f;
float final_I_DC_Offset = 2.048f;
float final_P_active = 0.0f;
float final_S_apparent = 0.0f;
float final_PowerFactor = 0.0f;

/* ---------------------------------------------------------------------------
 * HÀM XỬ LÝ ĐỆM DMA XEN KẼ (Interleaved Buffer Processing)
 * ---------------------------------------------------------------------------*/
static inline void Process_Buffer_Segment(uint16_t *start_ptr, uint16_t length)
{
    uint64_t v_local_sq_sum = 0;
    uint64_t v_local_raw_sum = 0;
    uint64_t i_local_sq_sum = 0;
    uint64_t i_local_raw_sum = 0;
    int64_t  p_local_sum = 0;

    int32_t v_current_offset = v_dynamic_offset;
    int32_t i_current_offset = i_dynamic_offset;

    for (uint16_t i = 0; i < length; i += NUM_CHANNELS)
    {
        uint16_t v_adc_raw = start_ptr[i];
        uint16_t i_adc_raw = start_ptr[i + 1];

        int32_t v_ac = (int32_t)v_adc_raw - v_current_offset;
        int32_t i_ac = (int32_t)i_adc_raw - i_current_offset;

        v_local_raw_sum += v_adc_raw;
        v_local_sq_sum  += (int64_t)(v_ac * v_ac);
        i_local_raw_sum += i_adc_raw;
        i_local_sq_sum  += (int64_t)(i_ac * i_ac);

        p_local_sum += (int64_t)(v_ac * i_ac);

        // --- THUẬT TOÁN ĐO TẦN SỐ TÍNH HỢP HYSTERESIS & NỘI SUY TƯẾN TÍNH ---
        freq_sample_counter++;

        if (state_schmitt == 0)
        {
            // Nếu đang ở bán kỳ âm, đợi tín hiệu vượt qua ngưỡng Hysteresis cao
            if (v_ac > FREQ_HYSTERESIS_HIGH)
            {
                state_schmitt = 1; // Chuyển trạng thái sang bán kỳ dương

                // Thực hiện nội suy tuyến tính tìm điểm cắt 0 chính xác
                // fraction = (0 - v_last_ac) / (v_ac - v_last_ac)
                float denominator = (float)(v_ac - v_last_ac);
                if (denominator != 0.0f) {
                    interp_fraction_current = (float)(-v_last_ac) / denominator;
                } else {
                    interp_fraction_current = 0.0f;
                }

                // Tính tổng số chu kỳ mẫu có độ chính xác thực (float):
                // Số mẫu thực tế = Bộ đếm nguyên mẫu + Phần lẻ chu kỳ này - Phần lẻ chu kỳ trước
                float exact_samples = (float)freq_sample_counter + interp_fraction_current - interp_fraction_last;

                // Bộ lọc giới hạn tần số thô (Tránh nhiễu loạn xung)
                // Lưới 50Hz tương đương 200 mẫu. Giới hạn dải đo hợp lệ từ 40Hz (250 mẫu) đến 70Hz (142 mẫu)
                if (exact_samples >= 142.0f && exact_samples <= 250.0f)
                {
                    freq_processing_raw = SAMPLING_FREQ / exact_samples;
                }

                // Lưu lại vết lịch sử cho chu kỳ kế tiếp
                interp_fraction_last = interp_fraction_current;
                freq_sample_counter = 0; // Reset bộ đếm mẫu
            }
        }
        else
        {
            // Nếu đang ở bán kỳ dương, đợi tín hiệu tụt sâu dưới ngưỡng Hysteresis thấp
            if (v_ac < FREQ_HYSTERESIS_LOW)
            {
                state_schmitt = 0; // Chuyển trạng thái sang bán kỳ âm
            }
        }
        v_last_ac = v_ac;
    }

    v_accumulated_sq_sum  += v_local_sq_sum;
    v_accumulated_raw_sum += v_local_raw_sum;
    i_accumulated_sq_sum  += i_local_sq_sum;
    i_accumulated_raw_sum += i_local_raw_sum;
    p_accumulated_sum     += p_local_sum;

    total_sample_counter += (length / NUM_CHANNELS);

    if (total_sample_counter >= WINDOW_SAMPLES)
    {
        if (data_ready_flag == 0)
        {
            v_rms_processing_sum    = v_accumulated_sq_sum;
            v_offset_processing_sum = v_accumulated_raw_sum;
            i_rms_processing_sum    = i_accumulated_sq_sum;
            i_offset_processing_sum = i_accumulated_raw_sum;
            p_processing_sum        = p_accumulated_sum;
            data_ready_flag = 1;
        }
        v_accumulated_sq_sum = 0; v_accumulated_raw_sum = 0;
        i_accumulated_sq_sum = 0; i_accumulated_raw_sum = 0;
        p_accumulated_sum = 0;
        total_sample_counter = 0;
    }
}

/* ---------------------------------------------------------------------------
 * DRIVER CALLBACKS
 * ---------------------------------------------------------------------------*/
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc_ptr)
{
    if (hadc_ptr->Instance == hadc.Instance) {
        Process_Buffer_Segment(&adc_dma_buffer[0], HALF_BUFFER_SIZE);
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc_ptr)
{
    if (hadc_ptr->Instance == hadc.Instance) {
        Process_Buffer_Segment(&adc_dma_buffer[HALF_BUFFER_SIZE], HALF_BUFFER_SIZE);
    }
}

/* ---------------------------------------------------------------------------
 * CÁC HÀM ĐIỀU KHIỂN PUBLIC
 * ---------------------------------------------------------------------------*/
void Measurement_Init(void)
{
    HAL_ADC_Start_DMA(&hadc, (uint32_t*)adc_dma_buffer, DMA_BUFFER_SIZE);
}

void Measurement_MainFunction(void)
{
    if (data_ready_flag == 1)
    {
        // 1. Tính toán và cập nhật DC Offset động
        uint32_t v_computed_offset = (uint32_t)(v_offset_processing_sum / WINDOW_SAMPLES);
        v_dynamic_offset = (int32_t)v_computed_offset;
        final_V_DC_Offset = ((float)v_computed_offset * 3.3f) / 4096.0f;

        uint32_t i_computed_offset = (uint32_t)(i_offset_processing_sum / WINDOW_SAMPLES);
        i_dynamic_offset = (int32_t)i_computed_offset;
        final_I_DC_Offset = ((float)i_computed_offset * 3.3f) / 4096.0f;

        // 2. Tính toán Điện áp và Dòng điện RMS
        float v_mean_squared = (float)v_rms_processing_sum / (float)WINDOW_SAMPLES;
        final_V_rms = sqrtf(v_mean_squared) * ADC_TO_VOLT_RATIO;

        float i_mean_squared = (float)i_rms_processing_sum / (float)WINDOW_SAMPLES;
        final_I_rms = sqrtf(i_mean_squared) * ADC_TO_AMP_RATIO;

        // 3. Tính toán Công suất thực (P)
        float p_mean = (float)p_processing_sum / (float)WINDOW_SAMPLES;
        final_P_active = p_mean * ADC_TO_VOLT_RATIO * ADC_TO_AMP_RATIO;
        if (final_P_active < 0.0f) final_P_active = 0.0f;

        // 4. Tính toán Công suất biểu kiến (S)
        final_S_apparent = final_V_rms * final_I_rms;

        // 5. Tính Hệ số công suất (Power Factor - PF)
        if (final_S_apparent > 0.1f)
        {
            final_PowerFactor = final_P_active / final_S_apparent;
            if (final_PowerFactor > 1.0f) final_PowerFactor = 1.0f;
        }
        else
        {
            final_PowerFactor = 1.0f;
        }

        // 6. BỘ LỌC THÔNG THẤP KỸ THUẬT SỐ (IIR FILTER) CHO TẦN SỐ
        // Công thức: Y(n) = Alpha * X(n) + (1 - Alpha) * Y(n-1)
        final_Frequency = (FREQ_LPF_ALPHA * freq_processing_raw) + ((1.0f - FREQ_LPF_ALPHA) * final_Frequency);

        // Chống trôi tần số khi ngắt kết nối tải / mất điện lưới hẳn
        if (final_V_rms < 10.0f) {
            final_Frequency = 0.0f;
        }

        data_ready_flag = 0;
    }
}
