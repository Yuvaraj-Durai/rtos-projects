/*
 * Shared FreeRTOS configuration for the GCC/POSIX (Linux simulator) port.
 *
 * A project may override any setting by providing an "app_config.h" in one
 * of its include directories; it is included first and every option below
 * is guarded with #ifndef.
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__has_include)
#if __has_include("app_config.h")
#include "app_config.h"
#endif
#endif

/* ---- Scheduler ---------------------------------------------------------- */
#ifndef configUSE_PREEMPTION
#define configUSE_PREEMPTION                    1
#endif
#ifndef configUSE_TIME_SLICING
#define configUSE_TIME_SLICING                  1
#endif
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ                      1000
#endif
#ifndef configMAX_PRIORITIES
#define configMAX_PRIORITIES                    10
#endif
/* Words. The POSIX port runs each task on its own pthread whose stack is at
 * least PTHREAD_STACK_MIN, so this mostly sizes the TCB-side allocation. */
#define configMINIMAL_STACK_SIZE                256
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1

/* ---- Memory ------------------------------------------------------------- */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#ifndef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE                   ((size_t)(1024 * 1024))
#endif
#define configAPPLICATION_ALLOCATED_HEAP        0

/* ---- Hooks (weak defaults in freertos_hooks.c) -------------------------- */
#define configUSE_IDLE_HOOK                     0
#ifndef configUSE_TICK_HOOK
#define configUSE_TICK_HOOK                     1
#endif
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
/* Stack checking is not meaningful on the POSIX port: tasks execute on
 * pthread stacks, not on the FreeRTOS-allocated stack. */
#define configCHECK_FOR_STACK_OVERFLOW          0

/* ---- Features ----------------------------------------------------------- */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    1
#define configQUEUE_REGISTRY_SIZE               16
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   3
#define configUSE_EVENT_GROUPS                  1
#define configUSE_STREAM_BUFFERS                1
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    1
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_CO_ROUTINES                   0
#define configRECORD_STACK_HIGH_ADDRESS         1

/* ---- Software timers ---------------------------------------------------- */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                20
#define configTIMER_TASK_STACK_DEPTH            (configMINIMAL_STACK_SIZE * 2)

/* ---- API inclusion ------------------------------------------------------ */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskAbortDelay                 1
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xSemaphoreGetMutexHolder        1
#define INCLUDE_xTaskResumeFromISR              1

/* ---- Assertions --------------------------------------------------------- */
void vAssertCalled(const char *file, unsigned long line);
#define configASSERT(x) do { if ((x) == 0) vAssertCalled(__FILE__, __LINE__); } while (0)

#endif /* FREERTOS_CONFIG_H */
