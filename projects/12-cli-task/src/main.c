/*
 * 12 - Command line interface running in its own task
 *
 * Bytes arrive from a simulated UART: main() reads the whole of stdin before
 * starting the scheduler, then a UART RX "ISR" (the tick hook) delivers one
 * byte per tick into a stream buffer with xStreamBufferSendFromISR(). The CLI
 * task blocks on that stream buffer, feeds bytes to the line editor in cli.c
 * and executes commands. Commands touch simulated hardware (an LED), a small
 * parameter store guarded by a mutex (so other tasks could read it), and
 * FreeRTOS introspection APIs.
 *
 *   ./cli_task < tests/session.txt      (or type commands, then Ctrl-D)
 */
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "stream_buffer.h"
#include "task.h"
#include "cli.h"
#include "demo_util.h"

#define INPUT_MAX 4096

static char input[INPUT_MAX];
static size_t input_len;
static volatile size_t input_pos;
static StreamBufferHandle_t rx_stream;
static volatile int exit_requested;

/* ---- simulated UART RX interrupt ---------------------------------------- */
void vApplicationTickHook(void)
{
    if (!rx_stream || input_pos >= input_len) return;
    BaseType_t woken = pdFALSE;
    if (xStreamBufferSendFromISR(rx_stream, &input[input_pos], 1, &woken) == 1) input_pos++;
}

/* ---- application state reachable from commands ------------------------- */
static int led_on;
static SemaphoreHandle_t params_lock;
static struct { const char *name; int value; } params[] = { { "gain", 1 }, { "offset", 0 }, { "rate", 100 } };

static void uart_write(void *ctx, const char *s)
{
    (void)ctx;
    demo_printf("%s", s);
}

static int cmd_echo(cli_t *c, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) cli_printf(c, "%s%s", argv[i], i + 1 < argc ? " " : "\n");
    return CLI_OK;
}

static int cmd_led(cli_t *c, int argc, char **argv)
{
    if (argc == 2) {
        if (!strcmp(argv[1], "on")) led_on = 1;
        else if (!strcmp(argv[1], "off")) led_on = 0;
        else if (!strcmp(argv[1], "toggle")) led_on = !led_on;
        else { cli_printf(c, "led: expected on|off|toggle\n"); return CLI_ERR; }
    }
    cli_printf(c, "LED is %s\n", led_on ? "ON" : "OFF");
    return CLI_OK;
}

static int find_param(const char *n)
{
    for (size_t i = 0; i < sizeof params / sizeof params[0]; i++)
        if (!strcmp(params[i].name, n)) return (int)i;
    return -1;
}

static int cmd_set(cli_t *c, int argc, char **argv)
{
    (void)argc;
    int i = find_param(argv[1]);
    char *end;
    long v = strtol(argv[2], &end, 0);
    if (i < 0 || *end) { cli_printf(c, "set: bad parameter or value\n"); return CLI_ERR; }
    xSemaphoreTake(params_lock, portMAX_DELAY);
    params[i].value = (int)v;
    xSemaphoreGive(params_lock);
    cli_printf(c, "%s := %ld\n", argv[1], v);
    return CLI_OK;
}

static int cmd_get(cli_t *c, int argc, char **argv)
{
    for (size_t i = 0; i < sizeof params / sizeof params[0]; i++) {
        if (argc == 2 && strcmp(argv[1], params[i].name)) continue;
        xSemaphoreTake(params_lock, portMAX_DELAY);
        int v = params[i].value;
        xSemaphoreGive(params_lock);
        cli_printf(c, "%s = %d\n", params[i].name, v);
    }
    return CLI_OK;
}

static int cmd_sum(cli_t *c, int argc, char **argv)
{
    long s = 0;
    for (int i = 1; i < argc; i++) s += strtol(argv[i], NULL, 0);
    cli_printf(c, "sum = %ld\n", s);
    return CLI_OK;
}

static int cmd_uptime(cli_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    cli_printf(c, "uptime %lu ms\n", (unsigned long)(xTaskGetTickCount() * portTICK_PERIOD_MS));
    return CLI_OK;
}

static int cmd_tasks(cli_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    TaskStatus_t st[8];
    UBaseType_t n = uxTaskGetSystemState(st, 8, NULL);
    static const char *state[] = { "run", "ready", "block", "susp", "del", "inv" };
    cli_printf(c, "%-10s %4s %s\n", "name", "prio", "state");
    for (UBaseType_t i = 0; i < n; i++)
        cli_printf(c, "%-10s %4lu %s\n", st[i].pcTaskName, (unsigned long)st[i].uxCurrentPriority,
                   state[st[i].eCurrentState]);
    return CLI_OK;
}

static int cmd_heap(cli_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    cli_printf(c, "heap free %lu bytes, minimum ever %lu bytes\n", (unsigned long)xPortGetFreeHeapSize(),
               (unsigned long)xPortGetMinimumEverFreeHeapSize());
    return CLI_OK;
}

static int cmd_exit(cli_t *c, int argc, char **argv)
{
    (void)argc; (void)argv;
    cli_printf(c, "bye\n");
    exit_requested = 1;
    return CLI_OK;
}

static const cli_cmd_t commands[] = {
    { "help", "- list commands", 0, 0, cli_cmd_help },
    { "echo", "<words...>", 1, 7, cmd_echo },
    { "led", "[on|off|toggle]", 0, 1, cmd_led },
    { "set", "<param> <value>", 2, 2, cmd_set },
    { "get", "[param]", 0, 1, cmd_get },
    { "sum", "<n1> [n2...]", 1, 7, cmd_sum },
    { "uptime", "- ms since boot", 0, 0, cmd_uptime },
    { "tasks", "- list FreeRTOS tasks", 0, 0, cmd_tasks },
    { "heap", "- FreeRTOS heap usage", 0, 0, cmd_heap },
    { "exit", "- stop the simulator", 0, 0, cmd_exit },
};

static cli_t cli;

static void cli_task(void *arg)
{
    (void)arg;
    cli_init(&cli, commands, sizeof commands / sizeof commands[0], uart_write, NULL);
    cli.echo = 1;
    demo_printf("FreeRTOS CLI ready. Type 'help'.\n");
    while (!exit_requested) {
        char buf[16];
        size_t n = xStreamBufferReceive(rx_stream, buf, sizeof buf, pdMS_TO_TICKS(200));
        if (n == 0 && input_pos >= input_len) break; /* EOF on the "UART" */
        for (size_t i = 0; i < n && !exit_requested; i++) cli_feed(&cli, buf[i]);
    }
    vTaskEndScheduler();
}

int main(void)
{
    input_len = fread(input, 1, sizeof input, stdin);
    params_lock = xSemaphoreCreateMutex();
    rx_stream = xStreamBufferCreate(64, 1);
    xTaskCreate(cli_task, "CLI", configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    vTaskStartScheduler();

    printf("commands executed: %u, errors: %u, exit command: %s\n", cli.executed, cli.errors,
           exit_requested ? "yes" : "no (EOF)");
    DEMO_CHECK(cli.executed > 0, "at least one command ran");
    DEMO_CHECK(exit_requested, "session ended with the exit command");
    return demo_report();
}
