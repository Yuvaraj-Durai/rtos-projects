# 06 — Software timers

This demo covers auto-reload and one-shot timers, a button debouncer built by
restarting a timer, changing a timer's period at run time, one callback shared by
several timers, and deferring a function call to the timer service task.

## RTOS concept

FreeRTOS software timers do not use a hardware timer each. They are managed by the
**timer service (daemon) task**, and every callback runs in that task's context. Timer
API calls (`xTimerStart`, `xTimerReset`, ...) send commands to the daemon through the
timer command queue. The rules that follow: callbacks must be short and must never
block, and the daemon's priority decides how promptly they run. Here
`configTIMER_TASK_PRIORITY` is `configMAX_PRIORITIES - 1` (9), and the command queue
holds 20 entries.

## How the demo works

`Ctrl` runs at priority 3, below the daemon (9), and steps through six parts:

| Part | Timer(s) | What happens | Check |
|------|----------|--------------|-------|
| A | `heartbeat`, auto-reload, 10 ms | Started, then `Ctrl` sleeps 205 ms with `vTaskDelayUntil` | exactly 20 callbacks |
| B | `oneshot`, 50 ms | Started while the heartbeat keeps running | fires once, exactly 50 ticks after start, then `xTimerIsTimerActive()` is false |
| C | `debounce`, one-shot, 30 ms | A button "bounces" 6 times, 4 ms apart, and every edge calls `xTimerReset()` | fires once, exactly 30 ms after the *last* edge |
| D | `heartbeat` | `xTimerChangePeriod(hb, 25 ms)` | 10 callbacks in 251 ms |
| E | 3 × `shared`, periods 5/10/15 ms | One callback uses `pvTimerGetTimerID()` to tell the timers apart, and each timer stops itself after id+1 hits | hits `[1 2 3]` |
| F | none | `xTimerPendFunctionCall(fn, 40, 2)` | `fn` runs in the daemon task and computes 42 |

At the end every timer is deleted with `xTimerDelete()`.

## Build and run

```sh
cmake -S projects/06-software-timers -B build/06
cmake --build build/06
./build/06/software_timers
(cd build/06 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target software_timers` and
`(cd build && ctest -R p06 --output-on-failure)`.

## Demo output

```
06-software-timers (FreeRTOS POSIX simulator)
[   205] A: auto-reload 10 ms timer fired 20 times in 205 ms
[   355] B: one-shot fired 1 time(s), 50 ms after start
[   479] C: 6 bounces -> debounced callback fired 1 time(s), 30 ms after last edge
[   730] D: after xTimerChangePeriod(25 ms): 10 callbacks in 251 ms
[   830] E: shared callback hits by timer ID: [1 2 3]
[   835] F: pended function ran in daemon task=1, result=42
checks: 9, failures: 0
RESULT: PASS
```

## What to look for

- The timing is exact to the tick: 20 callbacks in 205 ms, a one-shot firing 50 ticks
  after start, the debounce firing 30 ticks after the last edge. Auto-reload timers
  re-arm from their expiry time, not from when the callback ran, so they do not drift.
- In part C, six `xTimerReset()` calls produce **one** event. That is a debouncer with
  no polling loop and no dedicated task.
- `daemon task=1` in part F: `xTimerGetTimerDaemonTaskHandle()` matches the task that
  ran the pended function.

## Key takeaways

- Timer callbacks run in the daemon task. Never call a blocking API with a non-zero
  timeout from a callback, because that would stall every other timer.
- Timer API calls are commands on a queue, so they can fail when the queue is full.
  Check their return value when you pass a zero block time from busy code.
- Restarting a one-shot timer (`xTimerReset`) is the standard "quiet period" pattern for
  debouncing, inactivity timeouts and back-off.
- `xTimerPendFunctionCall()` (and its `FromISR` variant, see project 09) is a cheap way
  to run a function in task context without creating a task.
