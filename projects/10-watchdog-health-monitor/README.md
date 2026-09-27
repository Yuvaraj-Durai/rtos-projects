# 10 — Watchdog and task health monitor

This demo shows a supervision pattern for a multi-task system. Each task checks in to
a monitor, and the monitor kicks the hardware watchdog only when **every** task is
healthy. It restarts a task that hangs. A task that keeps failing escalates to a full
reset by letting the watchdog expire.

## RTOS concept

One hardware watchdog cannot tell you *which* task has died. Kicking it from a timer
ISR proves only that interrupts still work. The standard fix:

1. Each supervised task records a "check-in" timestamp once per loop iteration.
2. A high-priority **monitor task** checks that every task's last check-in is younger
   than that task's deadline.
3. Only when all tasks pass does the monitor kick the hardware watchdog.
4. A task that misses its deadline is deleted and re-created. After `MAX_RESTARTS`
   (2), the monitor escalates: it stops kicking and lets the watchdog reset the MCU.

The hardware watchdog is simulated in the **tick hook**: a down-counter reloaded to 100
ticks on each kick. When it reaches 0 it "resets the MCU" by notifying `Ctrl` from the
ISR (`vTaskNotifyGiveFromISR`).

## How the demo works

| Task | Priority | Period | Deadline (max check-in age) | Injected fault |
|------|----------|--------|-----------------------------|----------------|
| `Ctrl` | 6 | — | — | waits up to 5 s for the watchdog to bite, then reports |
| `Monitor` | 5 | 20 ms (`vTaskDelayUntil`) | — | — |
| `Control` | 3 | 5 ms | 25 ms | none |
| `Sensor` | 2 | 10 ms | 40 ms | at its 20th iteration, blocks forever on a semaphore that is never given (once only) |
| `Comms` | 1 | 20 ms | 60 ms | after 30 iterations, each cycle takes 90 ms and it never recovers (the iteration counter survives restarts) |

Expected sequence:

1. The Sensor hang (t≈200) is detected one or two monitor periods later. Sensor is
   restarted and recovers.
2. Comms slows down around t≈600 and is restarted twice. It keeps failing, so the
   monitor escalates.
3. With no more kicks, the watchdog expires exactly 100 ticks after the last kick.

The checks: the watchdog expired, Sensor was restarted exactly once and detected
within deadline + monitor period and checked in again after the restart, Comms reached
the maximum restarts, Control never faulted, and no kick happened after escalation.

## Build and run

```sh
cmake -S projects/10-watchdog-health-monitor -B build/10
cmake --build build/10
./build/10/watchdog_monitor
(cd build/10 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target watchdog_monitor` and
`(cd build && ctest -R p10 --output-on-failure)`.

## Demo output

```
10-watchdog-health-monitor (FreeRTOS POSIX simulator)
[     0] system up: 3 supervised tasks, monitor every 20 ms, WDT timeout 100 ms
[   200]   [inject] Sensor blocks forever on a semaphore
[   260] monitor: Sensor missed its deadline (age 60 > 40) -> restart #1
[   680] monitor: Comms missed its deadline (age 80 > 60) -> restart #1
[   760] monitor: Comms missed its deadline (age 80 > 60) -> restart #2
[   840] monitor: Comms failed after 2 restarts -> escalate: stop kicking the watchdog
[   920] *** hardware watchdog expired at t=920 (last kick t=820) -> MCU reset ***
[   920]   Sensor   checkins=87   faults=1 restarts=1
[   920]   Comms    checkins=34   faults=3 restarts=2
[   920]   Control  checkins=184  faults=0 restarts=0
[   920] Sensor hang detected 60 ms after it happened (deadline 40 + monitor period 20)
checks: 8, failures: 0
RESULT: PASS
```

## What to look for

- **Detection time:** the Sensor hang is caught 60 ms later. That is the worst case of
  the 40 ms deadline plus one 20 ms monitor period, and it is the figure you would
  design the watchdog timeout around.
- **Restart vs escalate:** Sensor recovers after one restart (its check-ins continue up
  to 87). Comms fails again after each restart, so the monitor stops kicking at t=840.
- `expired at t=920 (last kick t=820)`: the reset comes exactly one watchdog timeout
  after the last healthy kick. No task could "accidentally" keep the system alive.
- `Control ... faults=0`: a healthy high-rate task is not disturbed by the recovery
  actions on the others.

## Key takeaways

- Kick the hardware watchdog from *one* place, and only after checking the health of
  every task. Never kick it from an ISR or from any single task's loop.
- Give each task its own deadline, based on its period plus its worst-case execution
  time. A fixed global timeout is too loose for fast tasks and too tight for slow ones.
- Restarting a task only helps with transient faults. Bound the retries and escalate
  to a reset, which is a known-good state, for persistent ones.
- Deleting a task that is blocked on a kernel object is safe in FreeRTOS. Deleting a
  task that holds a mutex or other resources is not. Real systems usually restart the
  whole subsystem.
