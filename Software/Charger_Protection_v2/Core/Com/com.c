#include <com.h>
#include "main.h"
#include "Rte.h"
#include <string.h>
#include <float.h>

/* Wire v1: A5 5A VERSION CMD SEQ LENGTH PAYLOAD CRC16_LE.
 * CRC-16/CCITT-FALSE covers VERSION..PAYLOAD, excludes SOF and CRC. */
#define SOF0 0xa5U
#define SOF1 0x5aU
#define VERSION 1U
#define RX_SIZE 128U
#define REQUEST_PAYLOAD_MAX 40U
#define REPLY_PAYLOAD_MAX 72U
#define FRAME_TIMEOUT_MS 200U

enum { GET_INFO=1, GET_MEAS, GET_CALIB, GET_STORED, SAVE_CALIB,
       RESTORE_DEFAULTS, CMD_RESET, GET_PROTECTION, GET_STORED_PROTECTION,
       SAVE_PROTECTION, RESTORE_PROTECTION_DEFAULTS, GET_LEAK_MAX, GET_LEAK_COUNT_MAX };
enum { BAD_COMMAND=0x80, BAD_LENGTH, BAD_CRC, BAD_VERSION, NO_STORED };

_Static_assert(sizeof(float)==4 && FLT_MANT_DIG==24 && sizeof(CalibParams_t)==20,
               "Com requires IEEE-754 binary32 parameters");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Com float payloads require little-endian targets"
#endif

static uint8_t rx[RX_SIZE], request[REQUEST_PAYLOAD_MAX+8U];
static uint8_t reply[REPLY_PAYLOAD_MAX+8U];
static volatile uint8_t rx_head, rx_tail, rx_fault, tx_pos, tx_length;
static volatile uint16_t rx_errors;
static uint8_t received, reset_pending;
static uint32_t last_byte_ms;

static uint16_t Crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc=0xffffU;
    while (length--)
    {
        crc ^= (uint16_t)*data++ << 8;
        for (uint8_t bit=0; bit<8U; bit++)
            crc=(uint16_t)((crc<<1) ^ ((crc&0x8000U) ? 0x1021U : 0U));
    }
    return crc;
}

static uint8_t TxIdle(void)
{
    return tx_pos==tx_length && (tx_length==0U || LL_USART_IsActiveFlag_TC(USART1));
}

static uint8_t CalibFlags(const CalibInfo_t *info)
{
    return (uint8_t)(info->active_from_flash | (info->stored_valid<<1) |
                     (info->reset_required<<2));
}

static void Execute(void)
{
    uint8_t cmd=request[3], length=request[5], payload=1U, status=CALIB_OK;
    uint8_t expected=cmd==SAVE_CALIB ? 20U : (cmd==SAVE_PROTECTION ? 40U : 0U);
    CalibInfo_t info;
    CalibParams_t params;
    ProtectionParams_t protection;
    if (request[2]!=VERSION) status=BAD_VERSION;
    else if (cmd<GET_INFO || cmd>GET_LEAK_COUNT_MAX) status=BAD_COMMAND;
    else if (length!=expected) status=BAD_LENGTH;
    else switch (cmd)
    {
    case GET_INFO:
        Rte_Call_Calib_GetInfo(&info);
        reply[7]=VERSION; reply[8]=2U; reply[9]=7U;
        reply[10]=CalibFlags(&info);
        memcpy(&reply[11], &info.sequence, 4U);
        reply[15]=0xffU; reply[16]=31U; /* command capability bits 0..12 */
        { uint16_t errors=rx_errors; memcpy(&reply[17], &errors, 2U); }
        payload=13U;
        break;
    case GET_MEAS:
        {
            MeasureResult_t m;
            Rte_Read_MeasureResult(&m);
            const float values[15]={m.V_in_rms,m.I_in_rms,m.P_in_active,
                m.S_in_apparent,m.PowerFactor_in,m.Frequency_in,m.V_out_rms,
                m.I_leak_rms,m.Temperature,m.V_in_DC_Offset,m.V_out_DC_Offset,
                m.I_in_DC_Offset,m.I_leak_DC_Offset,m.leakage_adc_rms,
                m.temperature_adc_average};
            memcpy(&reply[7], values, sizeof(values));
            uint32_t clipped=(uint32_t)m.adc_clipped_mask;
            uint32_t faults=Rte_Call_Protection_GetFaultMask();
            memcpy(&reply[67], &clipped, 4U);
            memcpy(&reply[71], &faults, 4U);
            reply[75]=m.startup_complete;
            reply[76]=m.leakage_trip_flag;
            reply[77]=m.temperature_fault;
            payload=72U;
        }
        break;
    case GET_LEAK_MAX:
        {
            MeasureResult_t m;
            Rte_Read_MeasureResult(&m);
            float maxima[2]={m.I_leak_peak_max,m.I_leak_rms_max};
            memcpy(&reply[7],maxima,sizeof(maxima));
            reply[15]=m.fast_leakage_ready;
            payload=10U;
        }
        break;
    case GET_LEAK_COUNT_MAX:
        {
            MeasureResult_t m;
            Rte_Read_MeasureResult(&m);
            reply[7]=m.leakage_over_count_max;
            payload=2U;
        }
        break;
    case GET_CALIB:
        memcpy(&reply[7], Rte_Call_Calib_Get(), sizeof(params));
        payload=21U;
        break;
    case GET_STORED:
        if (Rte_Call_Calib_ReadStored(&params)!=CALIB_OK) status=NO_STORED;
        else { memcpy(&reply[7], &params, sizeof(params)); payload=21U; }
        break;
    case SAVE_CALIB:
    case RESTORE_DEFAULTS:
    case SAVE_PROTECTION:
    case RESTORE_PROTECTION_DEFAULTS:
        if (cmd==SAVE_CALIB)
        {
            memcpy(&params, &request[6], sizeof(params));
            status=(uint8_t)Rte_Call_Calib_Save(&params);
        }
        else if (cmd==SAVE_PROTECTION)
        {
            memcpy(&protection, &request[6], sizeof(protection));
            status=(uint8_t)Rte_Call_Calib_SaveProtection(&protection);
        }
        else if (cmd==RESTORE_PROTECTION_DEFAULTS)
            status=(uint8_t)Rte_Call_Calib_RestoreProtectionDefaults();
        else status=(uint8_t)Rte_Call_Calib_RestoreDefaults();
        Rte_Call_Calib_GetInfo(&info);
        reply[7]=CalibFlags(&info);
        payload=2U;
        break;
    case GET_PROTECTION:
        memcpy(&reply[7], Rte_Call_Calib_GetProtection(), sizeof(protection));
        payload=41U;
        break;
    case GET_STORED_PROTECTION:
        if (Rte_Call_Calib_ReadStoredProtection(&protection)!=CALIB_OK) status=NO_STORED;
        else { memcpy(&reply[7], &protection, sizeof(protection)); payload=41U; }
        break;
    case CMD_RESET:
        reset_pending=1U;
        break;
    default: break;
    }
    reply[0]=SOF0; reply[1]=SOF1; reply[2]=VERSION;
    reply[3]=(uint8_t)(cmd|0x80U); reply[4]=request[4]; reply[5]=payload;
    reply[6]=status;
    uint16_t crc=Crc16(&reply[2], (uint8_t)(payload+4U));
    reply[payload+6U]=(uint8_t)crc;
    reply[payload+7U]=(uint8_t)(crc>>8);
    /* Publish the complete frame before enabling TX interrupt. */
    tx_pos=0U;
    tx_length=(uint8_t)(payload+8U);
    LL_USART_ClearFlag_TC(USART1);
    LL_USART_EnableIT_TXE(USART1);
}

void Com_Init(void)
{
    rx_head=rx_tail=rx_fault=tx_pos=tx_length=0U;
    rx_errors=0U; received=reset_pending=0U; last_byte_ms=0U;
    LL_USART_DisableIT_TXE(USART1);
    USART1->ICR=USART_ICR_ORECF|USART_ICR_FECF|USART_ICR_NCF|USART_ICR_PECF;
    if (LL_USART_IsActiveFlag_RXNE(USART1)) (void)LL_USART_ReceiveData8(USART1);
    /* RDR has no FIFO: service a byte before the longer ADC batch ISR.
     * This IRQ only moves bytes; parsing/Flash stay in the main loop. */
    NVIC_SetPriority(USART1_IRQn,0U);
    NVIC_EnableIRQ(USART1_IRQn);
    LL_USART_EnableIT_ERROR(USART1);
    LL_USART_EnableIT_RXNE(USART1);
}

void Com_UART_IRQHandler(void)
{
    uint32_t flags=USART1->ISR;
    if (flags & (USART_ISR_ORE|USART_ISR_FE|USART_ISR_NE|USART_ISR_PE))
    {
        USART1->ICR=USART_ICR_ORECF|USART_ICR_FECF|USART_ICR_NCF|USART_ICR_PECF;
        rx_fault=1U; rx_errors++;
        if (flags&USART_ISR_RXNE) (void)LL_USART_ReceiveData8(USART1);
    }
    else if (flags&USART_ISR_RXNE)
    {
        uint8_t value=LL_USART_ReceiveData8(USART1);
        uint8_t next=(uint8_t)((rx_head+1U)&(RX_SIZE-1U));
        if (next==rx_tail) { rx_fault=1U; rx_errors++; }
        else { rx[rx_head]=value; rx_head=next; }
    }
    if (LL_USART_IsEnabledIT_TXE(USART1) && LL_USART_IsActiveFlag_TXE(USART1))
    {
        if (tx_pos<tx_length) LL_USART_TransmitData8(USART1,reply[tx_pos++]);
        if (tx_pos==tx_length) LL_USART_DisableIT_TXE(USART1);
    }
}

static void Application_Reset(void)
{
    /* Called only after the ACK has left the UART shift register (TC). */
    RTE_Call_Protection_EnterCalibMode();
    NVIC_SystemReset();
}

void Com_MainFunction(void)
{
    if (!TxIdle()) return;
    if (reset_pending) { Application_Reset(); return; }
    uint32_t now=Rte_Call_Scheduler_GetTimeMs();
    if (received && (uint32_t)(now-last_byte_ms)>=FRAME_TIMEOUT_MS) received=0U;
    /* Serialize fault recovery with the ISR; never parse across a lost byte. */
    uint32_t mask=__get_PRIMASK();
    __disable_irq();
    if (rx_fault) { rx_tail=rx_head; received=0U; rx_fault=0U; }
    __set_PRIMASK(mask);
    for (uint8_t budget=0U; budget<RX_SIZE && rx_tail!=rx_head; budget++)
    {
        if (rx_fault) { received=0U; return; }
        uint8_t value=rx[rx_tail];
        rx_tail=(uint8_t)((rx_tail+1U)&(RX_SIZE-1U));
        last_byte_ms=now;
        if (received==0U) { if (value==SOF0) request[received++]=value; continue; }
        if (received==1U && value!=SOF1)
        { received=value==SOF0 ? 1U : 0U; continue; }
        request[received++]=value;
        if (received==6U && (request[5]>REQUEST_PAYLOAD_MAX || (request[3]&0x80U)))
        { received=0U; continue; }
        if (received>=8U && received==(uint8_t)(request[5]+8U))
        {
            uint16_t wire=(uint16_t)request[received-2U] |
                          (uint16_t)((uint16_t)request[received-1U]<<8);
            uint8_t valid=Crc16(&request[2],(uint8_t)(received-4U))==wire;
            received=0U;
            if (valid && !rx_fault) { Execute(); return; }
            /* UART ISR also increments this diagnostic counter. */
            mask=__get_PRIMASK();
            __disable_irq();
            rx_errors++;
            __set_PRIMASK(mask);
        }
    }
}
