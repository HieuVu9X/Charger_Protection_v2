/*
 * Measurement.c
 *
 *  Created on: Jun 9, 2026
 *      Author: Admin
 */

/*--------------------------------------------------------------------------------
 *                                 Include Files
 -------------------------------------------------------------------------------*/
#include "Measurement.h"
/*--------------------------------------------------------------------------------
 *                              Constant Definitions
 -------------------------------------------------------------------------------*/
#define ADC_CHANNEL_NUM				5u
#define SAMPLE_PER_HALF        		20u

#define ADC_DMA_BUFFER_SIZE 		(ADC_CHANNEL_NUM * SAMPLE_PER_HALF * 2u)

/*
 * 10kHz sampling
 * 50Hz AC
 * 200 samples = 1 AC cycle
 */
#define RMS_WINDOW_SAMPLES     		200u
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
uint16_t g_AdcBuffer[ADC_DMA_BUFFER_SIZE];

static RmsAccumulator_t VacInAcc;
static RmsAccumulator_t VacOutAcc;
static RmsAccumulator_t IacAcc;
static RmsAccumulator_t ILeakAcc;

static uint32_t TempSum;
static uint32_t TempCount;

/*--------------------------------------------------------------------------------
 *                           Static Function Prototypes
 -------------------------------------------------------------------------------*/
static void RMS_Reset(RmsAccumulator_t *acc);
static void RMS_Update(RmsAccumulator_t *acc, float sample);
static float RMS_GetValue(RmsAccumulator_t *acc);
static void ADC_ProcessBlock(const uint16_t *buffer);
static void ADC_UpdateMeasurement(void);
/*--------------------------------------------------------------------------------
 *                           Public Function Definitions
 -------------------------------------------------------------------------------*/
void MeasurementInit(void)
{
    memset(g_AdcBuffer, 0, sizeof(g_AdcBuffer));

    RMS_Reset(&VacInAcc);
    RMS_Reset(&VacOutAcc);
    RMS_Reset(&IacAcc);
    RMS_Reset(&ILeakAcc);

    TempSum = 0u;
    TempCount = 0u;

    HAL_ADC_Start_DMA(&hadc, (uint32_t*)g_AdcBuffer, ADC_DMA_BUFFER_SIZE);
}

void Measurement_MainFunction(void)
{
    /*------------------------------------------------------
     * Current version:
     * RMS value at ADC input.
     *
     * Replace scale factors below according to HW.
     *-----------------------------------------------------*/
	MeasureResult.AcInVoltage 		= ADC_CountToVolt(rmsVacIn);
	MeasureResult.AcOutVoltage 		= ADC_CountToVolt(rmsVacOut);
	MeasureResult.AcCurrent 		= ADC_CountToVolt(rmsIac);
	MeasureResult.LeakageCurruent 	= ADC_CountToVolt(rmsILeak);
    /*
     * Example:
     * 0~100°C -> 0~3.3V
     */
	MeasureResult.Temp 				= (tempAvg * 100.0f) / 4095.0f;;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    ADC_ProcessBlock(&g_AdcBuffer[0]);
    ADC_UpdateMeasurement();
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    ADC_ProcessBlock(&g_AdcBuffer[SAMPLE_PER_HALF * ADC_CHANNEL_NUM]);
    ADC_UpdateMeasurement();
}

/*--------------------------------------------------------------------------------
 *                           Static Function Definitions
 -------------------------------------------------------------------------------*/
static void RMS_Reset(RmsAccumulator_t *acc)
{
    acc->Sum = 0.0f;
    acc->SumSquare = 0.0f;
    acc->SampleCount = 0u;
}

static void RMS_Update(RmsAccumulator_t *acc, float sample)
{
    acc->Sum += sample;

    acc->SumSquare += sample * sample;

    acc->SampleCount++;
}

static float RMS_GetValue(RmsAccumulator_t *acc)
{
    float mean;
    float meanSquare;
    float rms;

    if(acc->SampleCount == 0u)
    {
        return 0.0f;
    }

    mean = acc->Sum / (float)acc->SampleCount;

    meanSquare = acc->SumSquare / (float)acc->SampleCount;

    rms = meanSquare - (mean * mean);

    if(rms < 0.0f)
    {
        rms = 0.0f;
    }

    return sqrtf(rms);
}

static void ADC_ProcessBlock(const uint16_t *buffer)
{
    uint32_t i;

    for(i = 0; i < SAMPLE_PER_HALF; i++)
    {
        uint16_t vacIn;
        uint16_t vacOut;
        uint16_t iac;
        uint16_t ileak;
        uint16_t temp;

        vacIn 	= buffer[i * ADC_CHANNEL_NUM + ADC_CH_VAC_IN];
        vacOut 	= buffer[i * ADC_CHANNEL_NUM + ADC_CH_VAC_OUT];
        iac 	= buffer[i * ADC_CHANNEL_NUM + ADC_CH_IAC];
        ileak 	= buffer[i * ADC_CHANNEL_NUM + ADC_CH_ILEAK];
        temp 	= buffer[i * ADC_CHANNEL_NUM + ADC_CH_TEMP];

        RMS_Update(&VacInAcc, (float)vacIn);
        RMS_Update(&VacOutAcc, (float)vacOut);
        RMS_Update(&IacAcc, (float)iac);
        RMS_Update(&ILeakAcc, (float)ileak);
        TempSum += temp;
        TempCount++;
    }
}

static void ADC_UpdateMeasurement(void)
{
    if(VacInAcc.SampleCount <
       RMS_WINDOW_SAMPLES)
    {
        return;
    }

    /*
     * NOTE:
     * Current values are RMS in ADC counts.
     *
     * Add HW scale factors here:
     *
     * VacRms = RMS_GetValue(...) * VAC_GAIN
     * IacRms = RMS_GetValue(...) * IAC_GAIN
     */

    g_Measurement.AcInputVoltageRms  = RMS_GetValue(&VacInAcc);
    g_Measurement.AcOutputVoltageRms = RMS_GetValue(&VacOutAcc);
    g_Measurement.AcCurrentRms 		 = RMS_GetValue(&IacAcc);
    g_Measurement.LeakageCurrentRms  = RMS_GetValue(&ILeakAcc);

    if(TempCount > 0u)
    {
        /*
         * Example:
         * 0~100°C -> 0~4095 ADC
         *
         * Replace with actual sensor equation.
         */
        g_Measurement.ModuleTempDegC = ((float)TempSum / (float)TempCount) * 100.0f / 4095.0f;
    }

    RMS_Reset(&VacInAcc);
    RMS_Reset(&VacOutAcc);
    RMS_Reset(&IacAcc);
    RMS_Reset(&ILeakAcc);

    TempSum = 0u;
    TempCount = 0u;
}






