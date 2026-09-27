#include "signal_gen.h"

/* 64-entry integer sine table (amplitude 1000) keeps the generator bit-exact
 * on every host - no libm rounding differences. */
static const int16_t sine64[64] = {
       0,   98,  195,  290,  383,  471,  556,  634,  707,  773,  831,  882,  924,  957,  981,  995,
    1000,  995,  981,  957,  924,  882,  831,  773,  707,  634,  556,  471,  383,  290,  195,   98,
       0,  -98, -195, -290, -383, -471, -556, -634, -707, -773, -831, -882, -924, -957, -981, -995,
   -1000, -995, -981, -957, -924, -882, -831, -773, -707, -634, -556, -471, -383, -290, -195,  -98,
};

void siggen_init(siggen_t *g, uint32_t seed) { g->n = 0; g->rng = seed; }

int32_t siggen_next(siggen_t *g, int *is_spike)
{
    g->rng = g->rng * 1664525u + 1013904223u;
    int32_t noise = (int32_t)((g->rng >> 16) % 41) - 20;   /* -20..+20 */
    int32_t v = 2000 + sine64[(g->n / 4) % 64] + noise;    /* period: 256 samples */
    int spike = (g->n % 37) == 36;
    if (spike) v += 1500;
    if (is_spike) *is_spike = spike;
    g->n++;
    return v;
}
