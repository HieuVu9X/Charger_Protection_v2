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
#define ADC_CHANNEL_NUM				5u
#define ADC_SAMPLE_PER_CHANNEL		200u

#define ADC_DMA_BUFFER_SIZE 		(ADC_CHANNEL_NUM * ADC_SAMPLE_PER_CHANNEL)
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

static float rmsVacIn;
static float rmsVacOut;

static float rmsIac;
static float rmsILeak;

static float tempAvg;
/*--------------------------------------------------------------------------------
 *                           Static Function Prototypes
 -------------------------------------------------------------------------------*/
static void ADC_Process(void);
static float ADC_CountToVolt(float adcCount);
static float ADC_CalculateRms(uint8_t channel);
static float ADC_GetAverage(uint8_t channel);
/*--------------------------------------------------------------------------------
 *                           Public Function Definitions
 -------------------------------------------------------------------------------*/
void MeasurementInit(void)
{
    memset(g_AdcBuffer, 0, sizeof(g_AdcBuffer));

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

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    ADC_Process();
}



/*--------------------------------------------------------------------------------
 *                           Static Function Definitions
 -------------------------------------------------------------------------------*/
/*
 * Call every DMA Complete callback.
 */
static void ADC_Process(void)
{
	rmsVacIn  	= ADC_CalculateRms(ADC_CH_VAC_IN);
	rmsVacOut  	= ADC_CalculateRms(ADC_CH_VAC_OUT);

    rmsIac   	= ADC_CalculateRms(ADC_CH_IAC);
    rmsILeak 	= ADC_CalculateRms(ADC_CH_ILEAK);

    tempAvg  	= ADC_GetAverage(ADC_CH_TEMP);
}

static float ADC_CountToVolt(float adcCount)
{
    return (adcCount * 3.3f) / 4095.0f;
}

/*
 * RMS calculation with DC offset removal.
 */
static float ADC_CalculateRms(uint8_t channel)
{
    uint32_t sample;

    float mean = 0.0f;
    float rms  = 0.0f;

    for(sample = 0; sample < ADC_SAMPLE_PER_CHANNEL; sample++)
    {
        mean += (float)g_AdcBuffer[sample * ADC_CHANNEL_NUM + channel];
    }

    mean /= (float)ADC_SAMPLE_PER_CHANNEL;

    for(sample = 0; sample < ADC_SAMPLE_PER_CHANNEL; sample++)
    {
        float x;

        x = (float)g_AdcBuffer[sample * ADC_CHANNEL_NUM + channel];

        x -= mean;

        rms += (x * x);
    }

    rms /= (float)ADC_SAMPLE_PER_CHANNEL;

    return sqrtf(rms);
}

static float ADC_GetAverage(uint8_t channel)
{
    uint32_t sample;
    float avg = 0.0f;

    for(sample = 0; sample < ADC_SAMPLE_PER_CHANNEL; sample++)
    {
        avg += (float)g_AdcBuffer[sample * ADC_CHANNEL_NUM + channel];
    }

    avg /= (float)ADC_SAMPLE_PER_CHANNEL;

    return avg;
}
