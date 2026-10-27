#include "Scheduler.h"
#include "main.h"
#include "Rte.h"
#include "Measurement.h"
#include "Protection.h"

#define TASK_1MS_MASK    (1U << 0)
#define TASK_10MS_MASK   (1U << 1)
#define TASK_100MS_MASK  (1U << 2)

static volatile uint8_t pending_tasks;
static volatile uint32_t time_ms;
/* Written only by SysTick after initialization. */
static uint8_t divider_10ms;
static uint8_t divider_100ms;

void Scheduler_Init(void)
{
    uint32_t saved_primask = __get_PRIMASK();
    __disable_irq();
    pending_tasks = 0U;
    time_ms = 0U;
    divider_10ms = 0U;
    divider_100ms = 0U;
    LL_SYSTICK_EnableIT();
    __set_PRIMASK(saved_primask);
}

void Scheduler_Tick1msISR(void)
{
    time_ms++;
    /* Keep the 30 ms zero-cross fallback and 500 ms LED cycle in real ticks. */
    Rte_Call_Protection_Tick1msISR();

    pending_tasks |= TASK_1MS_MASK;
    if (++divider_10ms >= SCHEDULER_TASK_10MS_PERIOD_MS)
    {
        divider_10ms = 0U;
        pending_tasks |= TASK_10MS_MASK;
    }
    if (++divider_100ms >= SCHEDULER_TASK_100MS_PERIOD_MS)
    {
        divider_100ms = 0U;
        pending_tasks |= TASK_100MS_MASK;
    }
}

uint32_t Scheduler_GetTimeMs(void)
{
    return time_ms;
}

void Scheduler_MainFunction(void)
{
    /* Atomic take-and-clear: a tick during task execution remains pending for
     * the next dispatch. Interrupts stay enabled while components execute. */
    uint32_t saved_primask = __get_PRIMASK();
    __disable_irq();
    uint8_t ready_tasks = pending_tasks;
    pending_tasks = 0U;
    __set_PRIMASK(saved_primask);

    if ((ready_tasks & TASK_1MS_MASK) != 0U) Scheduler_Task1ms();
    if ((ready_tasks & TASK_10MS_MASK) != 0U) Scheduler_Task10ms();
    if ((ready_tasks & TASK_100MS_MASK) != 0U) Scheduler_Task100ms();
}

__WEAK void Scheduler_Task1ms(void)
{
    Measurement_MainFunction();
    Protection_MainFunction();
}

__WEAK void Scheduler_Task10ms(void)
{
    Rte_Call_Com_MainFunction();
}

__WEAK void Scheduler_Task100ms(void)
{
    /* Add component MainFunction calls with a 100 ms period here. */
}
