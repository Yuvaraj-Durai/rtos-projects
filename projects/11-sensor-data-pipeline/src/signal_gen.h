/* Deterministic synthetic sensor: slow sine + noise + periodic spikes. */
#ifndef SIGNAL_GEN_H
#define SIGNAL_GEN_H
#include <stdint.h>

typedef struct { uint32_t n; uint32_t rng; } siggen_t;
void    siggen_init(siggen_t *g, uint32_t seed);
int32_t siggen_next(siggen_t *g, int *is_spike);

#endif
