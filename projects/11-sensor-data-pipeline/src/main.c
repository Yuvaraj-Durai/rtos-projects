/*
 * 11 - Sensor data pipeline: Acquire -> Filter -> Log
 *
 *   Acquire (prio 4, every 2 ms, vTaskDelayUntil)
 *      | raw_q  (sample, sequence, timestamp)
 *   Filter  (prio 3): median-of-3 despike -> Q15 IIR low-pass -> decimate /4
 *      | out_q  (filtered value, sequence)
 *   Log     (prio 2): statistics, hysteresis alarm, every Nth record printed
 *
 * Each stage is a task connected by queues, so a slow logger never delays
 * acquisition (it only fills out_q). The filters live in filter.c and are
 * unit tested on the host; here the demo also re-runs the same chain
 * offline after the scheduler stops and requires a bit-exact match with
 * what the tasks produced.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "demo_util.h"
#include "filter.h"
#include "signal_gen.h"

#define NUM_SAMPLES   1024
#define DECIMATION    4
#define NUM_OUT       (NUM_SAMPLES / DECIMATION)
#define SAMPLE_PERIOD pdMS_TO_TICKS(2)

typedef struct { int32_t value; uint16_t seq; TickType_t t; } raw_t;
typedef struct { int32_t value; uint16_t seq; } out_t;

static QueueHandle_t raw_q, out_q;
static TaskHandle_t controller;
static int32_t produced_out[NUM_OUT];
static int out_n, spikes_injected, alarms, seq_errors;
static int32_t out_min = INT32_MAX, out_max = INT32_MIN;
static TickType_t max_period_err;

static void acquire_task(void *arg)
{
    (void)arg;
    siggen_t g;
    siggen_init(&g, 2024);
    TickType_t wake = xTaskGetTickCount(), prev = wake;
    for (uint16_t i = 0; i < NUM_SAMPLES; i++) {
        vTaskDelayUntil(&wake, SAMPLE_PERIOD);
        TickType_t now = xTaskGetTickCount();
        if (i > 0) {
            TickType_t err = (now - prev) > SAMPLE_PERIOD ? (now - prev) - SAMPLE_PERIOD : SAMPLE_PERIOD - (now - prev);
            if (err > max_period_err) max_period_err = err;
        }
        prev = now;
        int spike;
        raw_t r = { siggen_next(&g, &spike), i, now };
        spikes_injected += spike;
        if (xQueueSend(raw_q, &r, 0) != pdPASS) DEMO_CHECK(0, "raw queue overflow at %u", i);
    }
    vTaskSuspend(NULL);
}

static void filter_task(void *arg)
{
    (void)arg;
    chain_t c;
    chain_init(&c);
    raw_t r;
    uint16_t expect = 0, out_seq = 0;
    for (;;) {
        xQueueReceive(raw_q, &r, portMAX_DELAY);
        if (r.seq != expect++) seq_errors++;
        out_t o;
        if (chain_step(&c, r.value, &o.value)) {
            o.seq = out_seq++;
            xQueueSend(out_q, &o, portMAX_DELAY);
        }
    }
}

static void log_task(void *arg)
{
    (void)arg;
    hyst_t alarm;
    hyst_init(&alarm, 2800, 2600);
    out_t o;
    while (out_n < NUM_OUT) {
        xQueueReceive(out_q, &o, portMAX_DELAY);
        if (o.seq != out_n) seq_errors++;
        produced_out[out_n++] = o.value;
        if (o.value < out_min) out_min = o.value;
        if (o.value > out_max) out_max = o.value;
        int edge = hyst_step(&alarm, o.value);
        if (edge > 0) alarms++;
        if (edge) demo_log("Log: record %3u value %4ld  alarm %s", o.seq, (long)o.value, edge > 0 ? "ON" : "off");
        else if (o.seq % 32 == 0) demo_log("Log: record %3u value %4ld", o.seq, (long)o.value);
    }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

static void controller_task(void *arg)
{
    (void)arg;
    controller = xTaskGetCurrentTaskHandle();
    raw_q = xQueueCreate(16, sizeof(raw_t));
    out_q = xQueueCreate(8, sizeof(out_t));
    TaskHandle_t a, f, l;
    xTaskCreate(log_task, "Log", configMINIMAL_STACK_SIZE * 2, NULL, 2, &l);
    xTaskCreate(filter_task, "Filter", configMINIMAL_STACK_SIZE * 2, NULL, 3, &f);
    xTaskCreate(acquire_task, "Acquire", configMINIMAL_STACK_SIZE * 2, NULL, 4, &a);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    vTaskDelete(a); vTaskDelete(f); vTaskDelete(l);
    vTaskEndScheduler();
}

int main(void)
{
    printf("11-sensor-data-pipeline (FreeRTOS POSIX simulator)\n");
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, 5, NULL);
    vTaskStartScheduler();

    /* Offline reference: same generator, same chain, no RTOS. */
    siggen_t g;
    chain_t c;
    siggen_init(&g, 2024);
    chain_init(&c);
    int k = 0, mismatches = 0;
    int32_t raw_max = INT32_MIN;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        int32_t v = siggen_next(&g, NULL), o;
        if (v > raw_max) raw_max = v;
        if (chain_step(&c, v, &o)) { if (k >= out_n || produced_out[k] != o) mismatches++; k++; }
    }
    printf("samples=%d outputs=%d spikes injected=%d raw max=%ld filtered range=[%ld, %ld] alarms=%d\n",
           NUM_SAMPLES, out_n, spikes_injected, (long)raw_max, (long)out_min, (long)out_max, alarms);
    printf("max sampling period error=%lu ticks, sequence errors=%d, offline mismatches=%d\n",
           (unsigned long)max_period_err, seq_errors, mismatches);
    DEMO_CHECK(out_n == NUM_OUT, "all decimated records logged");
    DEMO_CHECK(seq_errors == 0, "no lost or reordered samples");
    DEMO_CHECK(mismatches == 0 && k == NUM_OUT, "task pipeline == offline reference");
    DEMO_CHECK(spikes_injected > 20 && raw_max > 3500, "spikes were present in raw data");
    DEMO_CHECK(out_max < 3100, "spikes removed by median filter (max %ld)", (long)out_max);
    DEMO_CHECK(alarms == 4, "one alarm per sine crest (%d)", alarms);
    DEMO_CHECK(max_period_err == 0, "periodic acquisition without drift");
    return demo_report();
}
