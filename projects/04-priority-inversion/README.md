# 04 — Priority inversion and priority inheritance

This demo recreates the classic three-task priority inversion (the 1997 Mars
Pathfinder bug). It runs the scenario twice, first with a binary semaphore as the lock
and then with a FreeRTOS mutex, and measures how long the high-priority task is
blocked.

## RTOS concept

**Priority inversion** happens when a high-priority task waits for a lock held by a
low-priority task, and a medium-priority task that does not need the lock preempts the
low one. The high-priority task then waits for the medium task too, and this delay has
no bound. A FreeRTOS **mutex** has an owner, so the kernel can apply **priority
inheritance**: while HIGH waits, the holder temporarily runs at HIGH's priority. A
binary semaphore has no owner, so the kernel cannot do this.

## How the demo works

| Task | Priority | Behaviour (times relative to the start of the scenario) |
|------|----------|--------------------------------------------------------|
| `Low` | 1 | t=0: takes the lock, does 30 ticks of CPU work, gives the lock |
| `High` | 3 | t=5 (`vTaskDelayUntil`): takes the lock, which blocks, then gives it straight back |
| `Med` | 2 | t=10: does 100 ticks of CPU work, never touches the lock |
| `Probe` | 4 | t=7: samples `uxTaskPriorityGet(Low)` while High is waiting |
| `Ctrl` | 5 | Runs both scenarios and checks the results |

"CPU work" counts only ticks in which the task actually held the CPU. A jump of more
than one tick means the task was preempted, and those ticks do not count. A preempted
task therefore makes no progress, as on a real single-core MCU.

- **Run 1** — `xSemaphoreCreateBinary()` (given once so it starts available). The
  checks: Low stays at priority 1, High is blocked for at least Med's 100 ticks, and
  Med finishes before High.
- **Run 2** — `xSemaphoreCreateMutex()`. The checks: Low is raised to priority 3 while
  High waits, High is blocked for at most Low's 30-tick critical section, and High
  finishes before Med.

Kernel objects: binary semaphore, mutex, `vTaskDelayUntil` for release times, and task
notifications for completion signalling.

## Build and run

```sh
cmake -S projects/04-priority-inversion -B build/04
cmake --build build/04
./build/04/priority_inversion
(cd build/04 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target priority_inversion` and
`(cd build && ctest -R p04 --output-on-failure)`.

## Demo output

```
04-priority-inversion (FreeRTOS POSIX simulator)
[   131] binary semaphore: HIGH blocked 126 ticks | LOW prio while HIGH waits=1 | MED done@110, LOW unlock@131, HIGH done@131
[   261] mutex (inherit):  HIGH blocked  25 ticks | LOW prio while HIGH waits=3 | LOW unlock@30, HIGH done@30, MED done@130
[   261] priority inheritance cut HIGH's blocking time from 126 to 25 ticks
checks: 6, failures: 0
RESULT: PASS
```

## What to look for

- **Binary semaphore:** `MED done@110` comes *before* `LOW unlock@131`. Med ran its
  whole 100-tick job while Low, which held the lock, was preempted. High, the most
  important task, finished last.
- **Mutex:** `LOW prio while HIGH waits=3` shows inheritance at work. Low finishes its
  critical section at t=30 with no interruption, High runs straight after, and Med
  runs only then (`MED done@130`).
- High's blocking time drops from 126 ticks, which is bounded only by Med's workload,
  to 25 ticks, which is bounded by Low's critical section.

## Key takeaways

- Use a **mutex**, not a binary semaphore, to protect a shared resource. Keep binary
  semaphores for signalling.
- Priority inheritance *bounds* the inversion but does not remove it. High still waits
  for the rest of Low's critical section, so keep critical sections short.
- Inheritance only helps when the lock has an owner. That is also why a FreeRTOS mutex
  must not be given from an ISR or by a task that did not take it.
- The Mars Pathfinder bug was fixed by turning on priority inheritance for a VxWorks
  mutex. This demo shows the same before-and-after.
