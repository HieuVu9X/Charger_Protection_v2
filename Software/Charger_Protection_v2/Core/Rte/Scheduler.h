/* Simple cooperative scheduler driven by the existing 1 ms SysTick. */
#ifndef RTE_SCHEDULER_H_
#define RTE_SCHEDULER_H_
#include <stdint.h>

#define SCHEDULER_TASK_1MS_PERIOD_MS    1U
#define SCHEDULER_TASK_10MS_PERIOD_MS   10U
#define SCHEDULER_TASK_100MS_PERIOD_MS  100U

/* Once after all component Init functions and the 1 ms clock configuration.
 * Clears pending releases and enables SysTick interrupts. */
void Scheduler_Init(void);
/* SysTick handler only. Marks due tasks and runs the short protection timer
 * hook so the relay timeout/LED do not depend on main-loop execution. */
void Scheduler_Tick1msISR(void);
/* Main loop only. Dispatches ready tasks in 1/10/100 ms order, outside ISR and
 * outside critical sections. Tasks must return quickly and must not block.
 * Multiple missed releases coalesce into one execution per task; no backlog
 * replay. Actual start times depend on the other tasks and interrupt load. */
void Scheduler_MainFunction(void);
uint32_t Scheduler_GetTimeMs(void);

/* Default task bodies are weak: edit them in scheduler.c, or override them in
 * an application source file. Task MainFunctions are called directly; keep
 * Measurement before Protection in task 1ms. Component callbacks use RTE.
 * Task 10ms runs Com through RTE; task 100ms is empty by default. */
void Scheduler_Task1ms(void);
void Scheduler_Task10ms(void);
void Scheduler_Task100ms(void);

#endif /* RTE_SCHEDULER_H_ */
