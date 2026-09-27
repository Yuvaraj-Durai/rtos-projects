# 09 — Deferred interrupt processing

This demo splits interrupt handling into a minimal **top half** (the ISR) and a
**bottom half** (a task). Three simulated peripherals use three FreeRTOS ISR-to-task
mechanisms. A CPU hog then shows why the priority of the deferred handler matters.

## RTOS concept

An ISR should only capture data, clear the source, and wake a task. The real work
(parsing, filtering, logging) belongs in a task, where it can block, use any API, and
be scheduled by priority. FreeRTOS gives ISRs `...FromISR()` APIs, which never block
and report through `pxHigherPriorityTaskWoken` whether a context switch is needed.

**Simulating interrupts:** the POSIX port has no peripherals, so the FreeRTOS **tick
hook** (`vApplicationTickHook`) stands in for the interrupt controller. It runs inside
the tick interrupt, so the ISR-only API rules apply to it. The demo does not call
`portYIELD_FROM_ISR()`, because the tick handler performs the context switch itself
when a higher-priority task has been unblocked.

## How the demo works

| Source | Rate | Top half (in the tick hook) | Bottom half |
|--------|------|-----------------------------|-------------|
| UART RX | every 2 ticks, 39 bytes of AT-style text | `xQueueSendFromISR(uart_q, {char, timestamp})` | `UartTask` (prio 4) assembles lines |
| ADC end-of-conversion | every 5 ticks, 80 samples | writes a 16-entry ring buffer, then `vTaskNotifyGiveFromISR(AdcTask)` | `AdcTask` (prio 4) drains the ring, averages blocks of 8, and queues the averages to `avg_q` |
| GPIO button | at t=100 and t=250 | `xTimerPendFunctionCallFromISR(button_deferred)` | runs in the timer daemon task (prio 9) |

Other tasks:

- `Hog` (prio 2) sleeps 80 ticks, then spins the CPU for 80 ticks.
- `Logger` (prio 1) consumes the ADC averages from `avg_q`. It is below the hog, so it
  gets delayed.
- `Ctrl` (prio 5) enables the simulation, waits for the hog and for all data, then
  reports and checks.

The checks: 4 UART lines with the correct content, no UART or ADC overruns, an ADC sum
equal to the expected sum, UART and ADC ISR-to-task latency of at most 1 tick, both
button presses handled in the daemon task, and at least 30 ticks of logger latency
caused by the hog.

## Build and run

```sh
cmake -S projects/09-deferred-interrupt-processing -B build/09
cmake --build build/09
./build/09/deferred_interrupt
(cd build/09 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target deferred_interrupt` and
`(cd build && ctest -R p09 --output-on-failure)`.

## Demo output

```
09-deferred-interrupt-processing (FreeRTOS POSIX simulator)
[     0] simulated interrupts enabled
[    15] UartTask: line "AT+PING"
[    35] UartTask: line "TEMP=23.5"
[    59] UartTask: line "HUMIDITY=41"
[    77] UartTask: line "AT+RESET"
[   405] ISRs raised: 121  (uart overruns 0, adc overruns 0)
[   405] UART: 4 lines, max ISR->task latency 0 ticks
[   405] ADC : 80 samples, mean 2049, max ISR->task latency 1 ticks
[   405] GPIO: 2 presses handled in timer daemon=1
[   405] low-priority logger: 10 averages, max latency 44 ticks (delayed by the CPU hog)
checks: 7, failures: 0
RESULT: PASS
```

## What to look for

- `ISRs raised: 121` = 39 UART bytes + 80 ADC samples + 2 button presses.
- UART and ADC latency of 0–1 ticks, **even while the hog spins**. The handlers run at
  priority 4, above the hog, so every interrupt preempts it straight away.
- The logger's `max latency 44 ticks`: the same data, handled by a task *below* the hog,
  waits until the hog yields. Deferring work is only as responsive as the priority of
  the task you defer it to.
- `handled in timer daemon=1`: the pended button function ran in the timer service
  task, and no dedicated task was needed.

## Key takeaways

- Keep ISRs short. Capture, wake, return.
- Pick the mechanism by what you need to pass:
  - a notification, to wake exactly one task cheaply (optionally with a shared ring
    buffer for the data);
  - a queue, to copy small items;
  - `xTimerPendFunctionCallFromISR`, for occasional events that need no task of their
    own.
- Give deferred handlers a priority that matches the deadline of the interrupt they
  serve. Latency comes from priority, not from the ISR.
- On real hardware, call `portYIELD_FROM_ISR(xHigherPriorityTaskWoken)` at the end of
  the ISR, so a woken high-priority task runs right away instead of at the next tick.
