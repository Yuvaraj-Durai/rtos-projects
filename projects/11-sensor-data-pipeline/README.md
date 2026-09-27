# 11 — Sensor data pipeline (Acquire → Filter → Log)

A three-stage sensor pipeline in which each stage is its own task and the stages are
connected by queues. The signal-processing code is pure C with no RTOS dependency. It
has its own host unit tests, and after the scheduler stops the demo checks the RTOS
pipeline bit-exact against an offline run of the same code.

## RTOS concept

- **Pipeline decoupling:** queues between stages let each task run at its own rate.
  When a slow stage is busy, its input queue fills up, but the stages before it keep
  running.
- **Rate-monotonic priorities:** the stage with the tightest timing (acquisition) gets
  the highest priority.
- **Jitter-free periodic sampling** with `vTaskDelayUntil`, which wakes at an absolute
  time. `vTaskDelay` wakes relative to the call and drifts.
- **Keeping algorithms RTOS-free**, so they can be unit tested on the host and reused.

## How the demo works

```
Acquire (prio 4, every 2 ms, vTaskDelayUntil)
   | raw_q  (length 16: sample, sequence, timestamp)
Filter  (prio 3): median-of-3 despike -> Q15 IIR low-pass (alpha 0.25) -> decimate /4
   | out_q  (length 8: filtered value, sequence)
Log     (prio 2): min/max statistics, hysteresis alarm (on >= 2800, off <= 2600), prints some records
```

- **Signal** (`signal_gen.c`): 2000 + 1000·sin (from a 64-entry integer table, 256
  samples per period) + noise of ±20, plus a +1500 spike on every 37th sample. It is
  deterministic and needs no libm, so results are bit-exact on any host.
- **Filters** (`filter.c`): `median3` removes single-sample spikes. `iir` is a
  first-order low-pass in Q15 with 8 extra fractional bits of state, which avoids limit
  cycles. `decim` averages blocks of 4. `hyst` is a threshold alarm with hysteresis.
  `chain_step()` connects the first three.
- **Tasks:** `Acquire` produces 1024 samples and checks its own sampling period each
  time (`max_period_err`). `Filter` checks input sequence numbers. `Log` checks output
  sequence numbers, stores all 256 outputs, and prints an alarm edge or every 32nd
  record. `Ctrl` (prio 5) creates the stages and waits for `Log` to finish.
- **Offline reference:** after `vTaskEndScheduler()`, `main()` runs the same generator
  and filter chain in a plain loop and compares all 256 outputs with what the tasks
  produced.

The checks: 256 outputs, no sequence errors, zero mismatches against the offline
reference, spikes present in the raw data (raw max > 3500), spikes gone after
filtering (max < 3100), exactly 4 alarms (one per sine crest), and a sampling period
error of 0 ticks.

## Build and run

```sh
cmake -S projects/11-sensor-data-pipeline -B build/11
cmake --build build/11
./build/11/sensor_pipeline
./build/11/p11_test_filter            # host unit tests for filter.c / signal_gen.c
(cd build/11 && ctest --output-on-failure)
```

Or from a whole-repo build: `cmake --build build --target sensor_pipeline p11_test_filter`
and `(cd build && ctest -R p11 --output-on-failure)`.

## Demo output

```
11-sensor-data-pipeline (FreeRTOS POSIX simulator)
[     9] Log: record   0 value 2005
[    97] Log: record  11 value 2831  alarm ON
[   225] Log: record  27 value 2553  alarm off
[   265] Log: record  32 value 2091
[   521] Log: record  64 value 1907
[   609] Log: record  75 value 2837  alarm ON
[   737] Log: record  91 value 2554  alarm off
[   777] Log: record  96 value 2095
[  1033] Log: record 128 value 1902
[  1121] Log: record 139 value 2838  alarm ON
[  1249] Log: record 155 value 2554  alarm off
[  1289] Log: record 160 value 2097
[  1545] Log: record 192 value 1905
[  1633] Log: record 203 value 2834  alarm ON
[  1761] Log: record 219 value 2562  alarm off
[  1801] Log: record 224 value 2106
samples=1024 outputs=256 spikes injected=27 raw max=4476 filtered range=[1003, 2998] alarms=4
max sampling period error=0 ticks, sequence errors=0, offline mismatches=0
checks: 7, failures: 0
RESULT: PASS
```

Unit tests:

```
[ RUN  ] median_removes_single_spike
[ RUN  ] median_follows_steps_with_one_sample_delay
[ RUN  ] iir_primes_with_first_sample
[ RUN  ] iir_step_response_converges
[ RUN  ] iir_alpha_is_clamped
[ RUN  ] decimator_averages_blocks
[ RUN  ] hysteresis_edges
[ RUN  ] generator_is_deterministic
[ RUN  ] chain_rejects_spikes_end_to_end
9 tests, 402 checks, 0 failures
```

## What to look for

- `raw max=4476` but `filtered range=[1003, 2998]`: the median filter removed all 27
  spikes, and the remaining range is the sine's ±1000 around 2000.
- Alarm ON and off edges come in pairs, once per sine period (every 64 records). The
  hysteresis band keeps noise near the threshold from causing chatter.
- `max sampling period error=0 ticks`: `vTaskDelayUntil` kept acquisition on a strict
  2 ms grid for 1024 samples.
- `offline mismatches=0`: running the code as tasks changed nothing about the numbers.
  No data was lost, reordered or corrupted between stages.

## Key takeaways

- Give each pipeline stage its own task and connect the stages with queues. Size each
  queue for the worst-case burst from the stage before it.
- Put the tightest deadline at the highest priority. Acquisition must never wait for
  logging.
- Keep DSP and business logic free of RTOS calls and stdio. You can then unit test it
  on the host, and use the same code as a golden reference for the RTOS build.
- Use fixed-point arithmetic (Q15 here) with guard bits, which is typical on MCUs
  without an FPU and gives bit-exact, reproducible results.
