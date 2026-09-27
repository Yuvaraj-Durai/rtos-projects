/*
 * 09 - Deferred interrupt processing (simulated ISRs -> tasks)
 *
 * The FreeRTOS tick hook runs inside the tick interrupt, so on the POSIX
 * simulator it is a convenient stand-in for "a peripheral interrupt". Three
 * simulated peripherals raise interrupts from it:
 *
 *   UART RX  every 2 ticks : one byte   -> xQueueSendFromISR()      -> UartTask  (prio 4)
 *   ADC EOC  every 5 ticks : one sample -> ring buffer +
 *                                          vTaskNotifyGiveFromISR() -> AdcTask   (prio 4)
 *   GPIO     twice         : button     -> xTimerPendFunctionCallFromISR()
 *                                          (runs in the timer daemon task)
 *
 * Each ISR only captures data and wakes a task: the "top half". The heavy
 * lifting (line assembly, averaging, logging) is the "bottom half" in task
 * context, where it can block and be prioritised.
 *
 * A CPU hog task (prio 2) runs for 80 ticks in the middle of the test. The
 * high-priority handlers keep a latency of 0 ticks, while a low-priority
 * (prio 1) consumer of ADC averages is delayed by the hog - which is why the
 * priority of deferred handlers matters.
 *
 * Note: we do not call portYIELD_FROM_ISR() in the tick hook; the kernel's
 * tick handler performs the context switch itself when an ISR-safe API has
 * unblocked a higher-priority task.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "timers.h"
#include "demo_util.h"

static const char uart_script[] =
    "AT+PING\n"
    "TEMP=23.5\n"
    "HUMIDITY=41\n"
    "AT+RESET\n";
#define UART_LEN (sizeof uart_script - 1)
#define ADC_SAMPLES 80
#define ADC_RING 16

/* ---- "hardware" state touched by ISRs ----------------------------------- */
static volatile int sim_enabled;
static volatile TickType_t sim_t0;
static volatile size_t uart_pos;
static volatile uint32_t isr_count, uart_overruns, adc_overruns;
static volatile uint16_t adc_ring[ADC_RING];
static volatile TickType_t adc_stamp[ADC_RING];
static volatile unsigned adc_head, adc_tail, adc_generated;
static volatile int gpio_fired;

static QueueHandle_t uart_q;           /* bytes + timestamp */
static QueueHandle_t avg_q;            /* ADC averages -> low-prio logger */
static TaskHandle_t adc_task_h, controller;

typedef struct { char c; TickType_t t; } uart_evt_t;

/* ---- results ------------------------------------------------------------ */
static char lines[8][32];
static int line_count;
static TickType_t max_uart_latency, max_adc_latency, max_logger_latency;
static unsigned adc_processed;
static uint32_t adc_sum;
static volatile int button_presses;
static volatile int button_in_daemon;
static int avg_logged;

static uint16_t adc_value(unsigned n) { return (uint16_t)(2000 + (n * 37) % 100); }

static void button_deferred(void *p1, uint32_t p2)
{
    (void)p1;
    button_in_daemon = (xTaskGetCurrentTaskHandle() == xTimerGetTimerDaemonTaskHandle());
    button_presses += (int)p2;
}

/* Runs in the tick interrupt: our simulated interrupt controller. */
void vApplicationTickHook(void)
{
    if (!sim_enabled) return;
    TickType_t now = xTaskGetTickCountFromISR();
    TickType_t rel = now - sim_t0;
    BaseType_t woken = pdFALSE; /* informational: the tick ISR switches anyway */

    if ((rel % 2) == 0 && uart_pos < UART_LEN) {                 /* UART RX ISR */
        isr_count++;
        uart_evt_t e = { uart_script[uart_pos], now };
        if (xQueueSendFromISR(uart_q, &e, &woken) != pdPASS) uart_overruns++;
        uart_pos++;
    }
    if ((rel % 5) == 0 && adc_generated < ADC_SAMPLES) {         /* ADC ISR */
        isr_count++;
        unsigned next = (adc_head + 1) % ADC_RING;
        if (next == adc_tail) {
            adc_overruns++;
        } else {
            adc_ring[adc_head] = adc_value(adc_generated);
            adc_stamp[adc_head] = now;
            adc_head = next;
        }
        adc_generated++;
        vTaskNotifyGiveFromISR(adc_task_h, &woken);
    }
    if (rel == 100 || rel == 250) {                              /* GPIO ISR */
        isr_count++;
        gpio_fired++;
        xTimerPendFunctionCallFromISR(button_deferred, NULL, 1, &woken);
    }
}

static void uart_task(void *arg)
{
    (void)arg;
    char buf[32];
    size_t n = 0;
    uart_evt_t e;
    for (;;) {
        xQueueReceive(uart_q, &e, portMAX_DELAY);
        TickType_t lat = xTaskGetTickCount() - e.t;
        if (lat > max_uart_latency) max_uart_latency = lat;
        if (e.c == '\n') {
            buf[n] = '\0';
            if (line_count < 8) strcpy(lines[line_count], buf);
            line_count++;
            demo_log("UartTask: line \"%s\"", buf);
            n = 0;
        } else if (n < sizeof buf - 1) {
            buf[n++] = e.c;
        }
    }
}

static void adc_task(void *arg)
{
    (void)arg;
    uint32_t block_sum = 0;
    int block_n = 0;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   /* one or more samples ready */
        while (adc_tail != adc_head) {
            TickType_t lat = xTaskGetTickCount() - adc_stamp[adc_tail];
            if (lat > max_adc_latency) max_adc_latency = lat;
            uint16_t v = adc_ring[adc_tail];
            adc_tail = (adc_tail + 1) % ADC_RING;
            adc_sum += v;
            adc_processed++;
            block_sum += v;
            if (++block_n == 8) {
                uint32_t msg[2] = { block_sum / 8, xTaskGetTickCount() };
                xQueueSend(avg_q, msg, 0);
                block_sum = 0;
                block_n = 0;
            }
        }
    }
}

static void logger_task(void *arg)                       /* low priority */
{
    (void)arg;
    uint32_t msg[2];
    for (;;) {
        xQueueReceive(avg_q, msg, portMAX_DELAY);
        TickType_t lat = xTaskGetTickCount() - msg[1];
        if (lat > max_logger_latency) max_logger_latency = lat;
        avg_logged++;
    }
}

static void hog_task(void *arg)                          /* CPU hog, prio 2 */
{
    (void)arg;
    vTaskDelay(80);
    TickType_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < 80) { /* spin */ }
    xTaskNotifyGive(controller);
    vTaskSuspend(NULL);
}

static void controller_task(void *arg)
{
    (void)arg;
    controller = xTaskGetCurrentTaskHandle();
    uart_q = xQueueCreate(8, sizeof(uart_evt_t));
    avg_q = xQueueCreate(16, sizeof(uint32_t[2]));
    TaskHandle_t u, l, h;
    xTaskCreate(uart_task, "UartTask", configMINIMAL_STACK_SIZE * 2, NULL, 4, &u);
    xTaskCreate(adc_task, "AdcTask", configMINIMAL_STACK_SIZE * 2, NULL, 4, &adc_task_h);
    xTaskCreate(logger_task, "Logger", configMINIMAL_STACK_SIZE * 2, NULL, 1, &l);
    xTaskCreate(hog_task, "Hog", configMINIMAL_STACK_SIZE, NULL, 2, &h);

    sim_t0 = xTaskGetTickCount() + 1;
    sim_enabled = 1;
    demo_log("simulated interrupts enabled");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);          /* hog finished */
    while (adc_processed < ADC_SAMPLES || uart_pos < UART_LEN || button_presses < 2 ||
           avg_logged < ADC_SAMPLES / 8)
        vTaskDelay(10);
    vTaskDelay(5);
    sim_enabled = 0;

    demo_log("ISRs raised: %lu  (uart overruns %lu, adc overruns %lu)", (unsigned long)isr_count,
             (unsigned long)uart_overruns, (unsigned long)adc_overruns);
    demo_log("UART: %d lines, max ISR->task latency %lu ticks", line_count, (unsigned long)max_uart_latency);
    demo_log("ADC : %u samples, mean %lu, max ISR->task latency %lu ticks", adc_processed,
             (unsigned long)(adc_sum / (adc_processed ? adc_processed : 1)), (unsigned long)max_adc_latency);
    demo_log("GPIO: %d presses handled in timer daemon=%d", button_presses, button_in_daemon);
    demo_log("low-priority logger: %d averages, max latency %lu ticks (delayed by the CPU hog)",
             avg_logged, (unsigned long)max_logger_latency);

    uint32_t expect_sum = 0;
    for (unsigned i = 0; i < ADC_SAMPLES; i++) expect_sum += adc_value(i);
    DEMO_CHECK(line_count == 4, "4 UART lines");
    DEMO_CHECK(strcmp(lines[1], "TEMP=23.5") == 0 && strcmp(lines[3], "AT+RESET") == 0, "line content");
    DEMO_CHECK(uart_overruns == 0 && adc_overruns == 0, "no overruns");
    DEMO_CHECK(adc_sum == expect_sum, "ADC data integrity");
    DEMO_CHECK(max_uart_latency <= 1 && max_adc_latency <= 1,
               "high-priority handlers run within a tick of the interrupt");
    DEMO_CHECK(button_presses == 2 && button_in_daemon, "GPIO deferred to daemon task");
    DEMO_CHECK(max_logger_latency >= 30, "low-priority consumer is delayed by the hog (%lu)",
               (unsigned long)max_logger_latency);

    vTaskDelete(u); vTaskDelete(adc_task_h); vTaskDelete(l); vTaskDelete(h);
    vTaskEndScheduler();
}

int main(void)
{
    printf("09-deferred-interrupt-processing (FreeRTOS POSIX simulator)\n");
    xTaskCreate(controller_task, "Ctrl", configMINIMAL_STACK_SIZE * 2, NULL, 5, NULL);
    vTaskStartScheduler();
    return demo_report();
}
