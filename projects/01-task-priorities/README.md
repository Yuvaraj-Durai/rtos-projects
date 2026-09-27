# 01 — Task creation and priorities

This demo shows how the FreeRTOS scheduler picks the next task to run. It covers
fixed-priority preemptive scheduling, round-robin time slicing between tasks of equal
priority, and the task lifecycle (`vTaskSuspend`, `vTaskResume`, `vTaskDelete`,
`vTaskPrioritySet`).

## RTOS concept

FreeRTOS always runs the **highest-priority task that is Ready**. With
`configUSE_PREEMPTION = 1`, a task that becomes Ready at a higher priority than the
running task takes the CPU at once, even inside the API call that made it Ready. With
`configUSE_TIME_SLICING = 1`, Ready tasks that share the top priority take turns, one
tick (1 ms here) each.

## How the demo works

A controller task (`Ctrl`, priority 5) runs five experiments one after another. Each
worker task appends a one-character tag to a shared **trace** string, inside a critical
section. The controller then compares the trace with the order it expects.

| Experiment | Tasks created (priority) | What the controller checks |
|------------|--------------------------|----------------------------|
| 1. Creation order vs priority | `Low` (1), `Mid` (2), `High` (3) | Nothing runs while `Ctrl` (5) keeps the CPU. After `vTaskDelay(10)` the trace is `HML`, so the tasks ran in priority order, not creation order. |
| 2. Immediate preemption | `Urgent` (6) | Trace `cUc`: the new task ran *inside* `xTaskCreate()`, before it returned to `Ctrl`. |
| 3. Time slicing | `SpinA`, `SpinB` (both 2), CPU-bound | Both make progress, and at least 4 alternations appear in 50 ms. A tag is added only when a spinner gets the CPU back, so the trace length counts time slices. |
| 4. `vTaskPrioritySet()` | `Bumped` (1, raised to 6) | Trace `cBc`: raising a Ready task above the caller preempts the caller inside the call. |
| 5. Lifecycle | `Bumped` again | `eTaskGetState()` reports `eSuspended`. `vTaskResume()` at priority 6 runs the task at once (`cBcB`). At priority 1 it only becomes `eReady`. Then `vTaskDelete()`. |

Kernel APIs used: `xTaskCreate`, `vTaskDelay`, `vTaskSuspend`, `vTaskResume`,
`vTaskDelete`, `vTaskPrioritySet`, `uxTaskPriorityGet`, `eTaskGetState`,
`uxTaskGetNumberOfTasks`, `taskENTER_CRITICAL`/`taskEXIT_CRITICAL`, `vTaskEndScheduler`.

## Build and run

From the repository root:

```sh
cmake -S projects/01-task-priorities -B build/01
cmake --build build/01
./build/01/task_priorities
(cd build/01 && ctest --output-on-failure)
```

If you already configured the whole repo with `cmake -S . -B build`, you can run just
this project:

```sh
cmake --build build --target task_priorities
./build/projects/01-task-priorities/task_priorities
(cd build && ctest -R p01 --output-on-failure)
```

## Demo output

```
01-task-priorities: FreeRTOS V11.1.0 on the POSIX simulator
[     0] exp1: create Low(1), Mid(2), High(3) from controller(5)
[    10] exp1: trace = HML
[    10] exp2: trace = cUc (U ran inside xTaskCreate)
[    65] exp3: A=233587 B=240734 iterations, 50 slices observed
[    65] exp4: trace = cBc
[    65] exp5: suspend/resume/delete ok, tasks now: 3
checks: 11, failures: 0
RESULT: PASS
```

The number in brackets is the tick count, and one tick is 1 ms. The spinner iteration
counts in `exp3` depend on the host and change from run to run. Every other line is
deterministic.

## What to look for

- `HML`, not `LMH`: tasks run in priority order, not creation order.
- `cUc` and `cBc`: preemption happens *inside* the API call that makes a
  higher-priority task Ready.
- `50 slices observed` in 50 ms: equal-priority CPU-bound tasks swap on every tick.
  The two iteration counts are close, which shows the CPU was shared fairly.
- `tasks now: 3`: after all the deletes, only `Ctrl`, the idle task and the timer
  service task are left.

## Key takeaways

- Priority decides who runs. Creation order and the order of API calls do not.
- Any call that can unblock or create a task can also switch context. Code after
  `xTaskCreate()` or `vTaskPrioritySet()` may run only after other tasks have run.
- A CPU-bound task starves every task below it. Only tasks at the *same* priority get
  time slices.
- On the POSIX port each task is a pthread, but only one runs at a time, so these
  results match a single-core MCU.
