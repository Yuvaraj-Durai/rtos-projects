/*
 * 05 - Event groups
 *
 * Part A  "Wait for ALL": the application task may only start once the
 *         network, sensor and storage subsystems have each set their
 *         READY bit. They come up at different times (20, 45, 30 ms).
 *
 * Part B  "Wait for ANY" + clear-on-exit: an alarm task sleeps until any of
 *         three fault bits is raised and reports which one(s).
 *
 * Part C  Rendezvous barrier with xEventGroupSync(): 4 workers run 5
 *         phases of different lengths; nobody may start phase k+1 until
 *         everybody has finished phase k. The check: at the moment any
 *         worker starts phase k+1, all 4 have completed phase k.
 */
#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"
#include "demo_util.h"

#define BIT_NET_READY     (1u << 0)
#define BIT_SENSOR_READY  (1u << 1)
#define BIT_STORAGE_READY (1u << 2)
#define ALL_READY         (BIT_NET_READY | BIT_SENSOR_READY | BIT_STORAGE_READY)

#define BIT_FAULT_OVERTEMP (1u << 4)
#define BIT_FAULT_UNDERVOLT (1u << 5)
#define BIT_FAULT_COMMS    (1u << 6)
#define ALL_FAULTS (BIT_FAULT_OVERTEMP | BIT_FAULT_UNDERVOLT | BIT_FAULT_COMMS)

#define NUM_WORKERS 4
#define NUM_PHASES  5
#define ALL_WORKERS ((1u << NUM_WORKERS) - 1)

static EventGroupHandle_t sys_events, barrier;
static TaskHandle_t controller;

/* ---------------- Part A ------------------------------------------------- */
typedef struct { const char *name; EventBits_t bit; TickType_t boot_ms; } subsystem_t;
static const subsystem_t subsystems[] = {
    { "network", BIT_NET_READY, 20 },
    { "sensor", BIT_SENSOR_READY, 45 },
    { "storage", BIT_STORAGE_READY, 30 },
};

static void subsystem_task(void *arg)
{
    const subsystem_t *s = arg;
    vTaskDelay(pdMS_TO_TICKS(s->boot_ms));
    demo_log("  %s ready", s->name);
    xEventGroupSetBits(sys_events, s->bit);
    vTaskSuspend(NULL);
}

/* ---------------- Part B ------------------------------------------------- */
static volatile EventBits_t alarms_seen[3];
static volatile int alarm_count;

static void alarm_task(void *arg)
{
    (void)arg;
    while (alarm_count < 3) {
        /* clearOnExit=pdTRUE, waitForAll=pdFALSE */
        EventBits_t b = xEventGroupWaitBits(sys_events, ALL_FAULTS, pdTRUE, pdFALSE, portMAX_DELAY);
        alarms_seen[alarm_count++] = b & ALL_FAULTS;
        demo_log("  alarm task woke: faults=0x%02lx", (unsigned long)(b & ALL_FAULTS));
    }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

/* ---------------- Part C ------------------------------------------------- */
static volatile int phase_done[NUM_PHASES];
static volatile int barrier_violations;

static void worker_task(void *arg)
{
    int id = (int)(uintptr_t)arg;
    for (int ph = 0; ph < NUM_PHASES; ph++) {
        /* Starting phase ph: everyone must have finished ph-1. */
        if (ph > 0 && phase_done[ph - 1] != NUM_WORKERS) barrier_violations++;
        vTaskDelay(pdMS_TO_TICKS(1 + ((id * 7 + ph * 3) % 6))); /* uneven work */
        taskENTER_CRITICAL();
        phase_done[ph]++;
        taskEXIT_CRITICAL();
        xEventGroupSync(barrier, 1u << id, ALL_WORKERS, portMAX_DELAY);
    }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

static void controller_task(void *arg)
{
    (void)arg;
    controller = xTaskGetCurrentTaskHandle();
    sys_events = xEventGroupCreate();
    barrier = xEventGroupCreate();

    /* Part A */
    demo_log("part A: waiting for ALL of net|sensor|storage");
    TaskHandle_t sub[3];
    TickType_t t0 = xTaskGetTickCount();
    for (int i = 0; i < 3; i++)
        xTaskCreate(subsystem_task, subsystems[i].name, configMINIMAL_STACK_SIZE,
                    (void *)&subsystems[i], 2, &sub[i]);
    EventBits_t early = xEventGroupWaitBits(sys_events, ALL_READY, pdFALSE, pdTRUE, pdMS_TO_TICKS(10));
    DEMO_CHECK((early & ALL_READY) != ALL_READY, "timeout expected before everything booted");
    EventBits_t b = xEventGroupWaitBits(sys_events, ALL_READY, pdFALSE, pdTRUE, pdMS_TO_TICKS(200));
    TickType_t waited = xTaskGetTickCount() - t0;
    demo_log("part A: all ready after %lu ms (slowest subsystem boots in 45 ms)", (unsigned long)waited);
    DEMO_CHECK((b & ALL_READY) == ALL_READY, "all ready bits set");
    DEMO_CHECK(waited >= pdMS_TO_TICKS(45) && waited <= pdMS_TO_TICKS(60), "woke with the last bit (%lu)",
               (unsigned long)waited);
    for (int i = 0; i < 3; i++) vTaskDelete(sub[i]);

    /* Part B */
    demo_log("part B: alarm task waits for ANY fault bit");
    TaskHandle_t al;
    xTaskCreate(alarm_task, "Alarm", configMINIMAL_STACK_SIZE, NULL, 3, &al);
    vTaskDelay(5);
    xEventGroupSetBits(sys_events, BIT_FAULT_OVERTEMP);
    vTaskDelay(5);
    /* Two bits set atomically in one call wake the waiter once. */
    xEventGroupSetBits(sys_events, BIT_FAULT_UNDERVOLT | BIT_FAULT_COMMS);
    vTaskDelay(5);
    xEventGroupSetBits(sys_events, BIT_FAULT_COMMS);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
    DEMO_CHECK(alarm_count == 3, "three alarm wake-ups");
    DEMO_CHECK(alarms_seen[0] == BIT_FAULT_OVERTEMP, "first alarm: overtemp");
    DEMO_CHECK(alarms_seen[1] == (BIT_FAULT_UNDERVOLT | BIT_FAULT_COMMS), "second alarm: both bits");
    DEMO_CHECK(alarms_seen[2] == BIT_FAULT_COMMS, "third alarm: comms");
    DEMO_CHECK((xEventGroupGetBits(sys_events) & ALL_FAULTS) == 0, "fault bits cleared on exit");
    DEMO_CHECK((xEventGroupGetBits(sys_events) & ALL_READY) == ALL_READY, "ready bits untouched");
    vTaskDelete(al);

    /* Part C */
    demo_log("part C: %d workers x %d phases with xEventGroupSync barrier", NUM_WORKERS, NUM_PHASES);
    TaskHandle_t w[NUM_WORKERS];
    for (int i = 0; i < NUM_WORKERS; i++)
        xTaskCreate(worker_task, "Worker", configMINIMAL_STACK_SIZE, (void *)(uintptr_t)i, 1 + i % 2, &w[i]);
    for (int i = 0; i < NUM_WORKERS; i++) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    for (int i = 0; i < NUM_WORKERS; i++) vTaskDelete(w[i]);
    demo_log("part C: phase completion counts = %d %d %d %d %d, violations = %d",
             phase_done[0], phase_done[1], phase_done[2], phase_done[3], phase_done[4], barrier_violations);
    for (int ph = 0; ph < NUM_PHASES; ph++)
        DEMO_CHECK(phase_done[ph] == NUM_WORKERS, "phase %d completed by all", ph);
    DEMO_CHECK(barrier_violations == 0, "no worker ran ahead of the barrier");

    vEventGroupDelete(sys_events);
    vEventGroupDelete(barrier);
    vTaskEndScheduler();
}

int main(void)
{
    printf("05-event-groups (FreeRTOS POSIX simulator)\n");
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, 4, NULL);
    vTaskStartScheduler();
    return demo_report();
}
