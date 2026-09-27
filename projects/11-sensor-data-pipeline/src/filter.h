/*
 * Pure, RTOS-independent signal-processing blocks used by the pipeline.
 * All state lives in caller-owned structs so they are trivially unit-testable
 * and re-entrant.
 */
#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>

/* Median of the last 3 samples: removes single-sample spikes. */
typedef struct { int32_t h[3]; int n; } median3_t;
void    median3_init(median3_t *m);
int32_t median3_step(median3_t *m, int32_t x);

/* First-order IIR low-pass, y += alpha * (x - y), alpha in Q15 (0..32767).
 * State is kept with 8 extra fractional bits to avoid limit cycles. */
typedef struct { int32_t alpha_q15; int64_t y_q8; int primed; } iir_t;
void    iir_init(iir_t *f, int32_t alpha_q15);
int32_t iir_step(iir_t *f, int32_t x);

/* Decimate by N (N <= 16) with averaging. Returns 1 when *out is valid. */
typedef struct { int n, count; int64_t acc; } decim_t;
void decim_init(decim_t *d, int n);
int  decim_step(decim_t *d, int32_t x, int32_t *out);

/* Threshold alarm with hysteresis: goes high at >= hi, low again at <= lo. */
typedef struct { int32_t hi, lo; int state; } hyst_t;
void hyst_init(hyst_t *h, int32_t hi, int32_t lo);
int  hyst_step(hyst_t *h, int32_t x); /* returns +1 rising edge, -1 falling, 0 none */

/* The complete per-sample chain used by the Filter task. */
typedef struct { median3_t med; iir_t iir; decim_t dec; } chain_t;
void chain_init(chain_t *c);
int  chain_step(chain_t *c, int32_t raw, int32_t *out);

#endif
