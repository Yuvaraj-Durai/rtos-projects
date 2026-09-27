/*
 * 10 - Watchdog / task health monitor
 *
 * A single hardware watchdog cannot tell you *which* task died, and kicking
 * it from a timer ISR proves nothing about the tasks. The standard pattern:
 *
 *   - every supervised task "checks in" once per loop iteration
 *   - a high-priority monitor task verifies each task's last check-in is
 *     younger than its deadline
 *   - only if ALL tasks are healthy does the monitor kick the hardware
 *     watchdog; a failed task is restarted, and a task that keeps failing
 *     escalates to a full reset by letting the hardware watchdog expire.
 *
 * The hardware watchdog is simulated in the tick hook (a down-counter that
 * "resets the MCU" at zero). Faults injected:
 *   - Sensor hangs forever on a semaphore at its 20th iteration (once):
 *     detected, task deleted and re-created, recovers.
 *   - Comms becomes permanently too slow after 30 iterations: restarted
 *     twice, still failing -> escalation -> watchdog expires -> "reset".
 */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "demo_util.h"

#define WDT_TIMEOUT_TICKS 100
#define MONITOR_PERIOD    pdMS_TO_TICKS(20)
#define MAX_RESTARTS      2

typedef struct {
    const char   *name;
    TaskFunction_t fn;
    UBaseType_t   prio;
    TickType_t    period;
    TickType_t    deadline;       /* max age of last check-in */
    TaskHandle_t  handle;
    volatile TickType_t last_checkin;
    volatile uint32_t   checkins;
    int           faults;
    int           restarts;
    TickType_t    first_fault_at;
} supervised_t;

enum { T_SENSOR, T_COMMS, T_CONTROL, NUM_SUPERVISED };

static void sensor_task(void *arg);
static void comms_task(void *arg);
static void control_task(void *arg);

static supervised_t sup[NUM_SUPERVISED] = {
    [T_SENSOR]  = { "Sensor", sensor_task, 2, pdMS_TO_TICKS(10), pdMS_TO_TICKS(40) },
    [T_COMMS]   = { "Comms", comms_task, 1, pdMS_TO_TICKS(20), pdMS_TO_TICKS(60) },
    [T_CONTROL] = { "Control", control_task, 3, pdMS_TO_TICKS(5), pdMS_TO_TICKS(25) },
};

/* ---- simulated hardware watchdog --------------------------------------- */
static volatile int32_t wdt_counter = -1; /* -1 = not started */
static volatile TickType_t wdt_reset_at, last_kick_at;
static volatile int wdt_expired;
static TaskHandle_t controller;

static void wdt_kick(void)
{
    taskENTER_CRITICAL();
    wdt_counter = WDT_TIMEOUT_TICKS;
    last_kick_at = xTaskGetTickCount();
    taskEXIT_CRITICAL();
}

void vApplicationTickHook(void)
{
    if (wdt_counter > 0 && --wdt_counter == 0 && !wdt_expired) {
        wdt_expired = 1;
        wdt_reset_at = xTaskGetTickCountFromISR();
        BaseType_t w = pdFALSE;
        vTaskNotifyGiveFromISR(controller, &w);
    }
}

/* ---- supervised tasks --------------------------------------------------- */
static void checkin(int id)
{
    sup[id].last_checkin = xTaskGetTickCount();
    sup[id].checkins++;
}

static SemaphoreHandle_t never_given;
static volatile int sensor_fault_injected;
static volatile TickType_t sensor_hang_at;

static void sensor_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (uint32_t it = 0;; it++) {
        checkin(T_SENSOR);
        if (it == 20 && !sensor_fault_injected) {
            sensor_fault_injected = 1;
            sensor_hang_at = xTaskGetTickCount();
            demo_log("  [inject] Sensor blocks forever on a semaphore");
            xSemaphoreTake(never_given, portMAX_DELAY);
        }
        vTaskDelayUntil(&wake, sup[T_SENSOR].period);
    }
}

static volatile uint32_t comms_total_iterations;

static void comms_task(void *arg)
{
    (void)arg;
    for (;;) {
        checkin(T_COMMS);
        comms_total_iterations++;
        /* After 30 iterations the "link" degrades: each cycle takes 90 ms,
         * longer than the 60 ms deadline, and it never recovers. */
        vTaskDelay(comms_total_iterations > 30 ? pdMS_TO_TICKS(90) : sup[T_COMMS].period);
    }
}

static void control_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        checkin(T_CONTROL);
        vTaskDelayUntil(&wake, sup[T_CONTROL].period);
    }
}

static void start_supervised(int id)
{
    sup[id].last_checkin = xTaskGetTickCount();
    xTaskCreate(sup[id].fn, sup[id].name, configMINIMAL_STACK_SIZE, NULL, sup[id].prio, &sup[id].handle);
}

/* ---- the monitor -------------------------------------------------------- */
static volatile int escalated;
static volatile TickType_t escalated_at;

static void monitor_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, MONITOR_PERIOD);
        TickType_t now = xTaskGetTickCount();
        int healthy = 1;
        for (int i = 0; i < NUM_SUPERVISED; i++) {
            TickType_t age = now - sup[i].last_checkin;
            if (age <= sup[i].deadline) continue;
            healthy = 0;
            sup[i].faults++;
            if (sup[i].faults == 1) sup[i].first_fault_at = now;
            if (sup[i].restarts < MAX_RESTARTS) {
                demo_log("monitor: %s missed its deadline (age %lu > %lu) -> restart #%d",
                         sup[i].name, (unsigned long)age, (unsigned long)sup[i].deadline,
                         sup[i].restarts + 1);
                vTaskDelete(sup[i].handle);
                sup[i].restarts++;
                start_supervised(i);
            } else if (!escalated) {
                escalated = 1;
                escalated_at = now;
                demo_log("monitor: %s failed after %d restarts -> escalate: stop kicking the watchdog",
                         sup[i].name, MAX_RESTARTS);
            }
        }
        if (healthy && !escalated) wdt_kick();
    }
}

static void controller_task(void *arg)
{
    (void)arg;
    controller = xTaskGetCurrentTaskHandle();
    never_given = xSemaphoreCreateBinary();
    for (int i = 0; i < NUM_SUPERVISED; i++) start_supervised(i);
    TaskHandle_t mon;
    xTaskCreate(monitor_task, "Monitor", configMINIMAL_STACK_SIZE * 2, NULL, 5, &mon);
    wdt_kick();
    demo_log("system up: 3 supervised tasks, monitor every %lu ms, WDT timeout %d ms",
             (unsigned long)MONITOR_PERIOD, WDT_TIMEOUT_TICKS);

    /* Wait for the simulated hardware watchdog to bite (or give up). */
    BaseType_t bit = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
    vTaskSuspend(mon);
    demo_log("*** hardware watchdog expired at t=%lu (last kick t=%lu) -> MCU reset ***",
             (unsigned long)wdt_reset_at, (unsigned long)last_kick_at);

    for (int i = 0; i < NUM_SUPERVISED; i++)
        demo_log("  %-8s checkins=%-4lu faults=%d restarts=%d", sup[i].name,
                 (unsigned long)sup[i].checkins, sup[i].faults, sup[i].restarts);

    TickType_t detect = sup[T_SENSOR].first_fault_at - sensor_hang_at;
    demo_log("Sensor hang detected %lu ms after it happened (deadline %lu + monitor period %lu)",
             (unsigned long)detect, (unsigned long)sup[T_SENSOR].deadline, (unsigned long)MONITOR_PERIOD);

    DEMO_CHECK(bit != 0 && wdt_expired, "watchdog must eventually expire after escalation");
    DEMO_CHECK(sup[T_SENSOR].restarts == 1, "Sensor restarted exactly once (%d)", sup[T_SENSOR].restarts);
    DEMO_CHECK(detect <= sup[T_SENSOR].deadline + MONITOR_PERIOD, "Sensor hang detected in time");
    DEMO_CHECK(sup[T_SENSOR].last_checkin > sup[T_SENSOR].first_fault_at, "Sensor recovered after restart");
    DEMO_CHECK(sup[T_COMMS].restarts == MAX_RESTARTS, "Comms restarted the maximum times");
    DEMO_CHECK(sup[T_CONTROL].faults == 0, "Control never faulted");
    DEMO_CHECK(escalated && wdt_reset_at - last_kick_at == WDT_TIMEOUT_TICKS,
               "reset exactly one WDT timeout after the last kick");
    DEMO_CHECK(last_kick_at <= escalated_at, "no kicks after escalation");

    vTaskDelete(mon);
    for (int i = 0; i < NUM_SUPERVISED; i++) vTaskDelete(sup[i].handle);
    vTaskEndScheduler();
}

int main(void)
{
    printf("10-watchdog-health-monitor (FreeRTOS POSIX simulator)\n");
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, 6, NULL);
    vTaskStartScheduler();
    return demo_report();
}
