#include "filter.h"

void median3_init(median3_t *m) { m->h[0] = m->h[1] = m->h[2] = 0; m->n = 0; }

int32_t median3_step(median3_t *m, int32_t x)
{
    m->h[0] = m->h[1];
    m->h[1] = m->h[2];
    m->h[2] = x;
    if (m->n < 3) m->n++;
    if (m->n < 3) return x; /* warm-up: pass through */
    int32_t a = m->h[0], b = m->h[1], c = m->h[2];
    if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
    if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
    return c;
}

void iir_init(iir_t *f, int32_t alpha_q15)
{
    if (alpha_q15 < 0) alpha_q15 = 0;
    if (alpha_q15 > 32767) alpha_q15 = 32767;
    f->alpha_q15 = alpha_q15;
    f->y_q8 = 0;
    f->primed = 0;
}

int32_t iir_step(iir_t *f, int32_t x)
{
    int64_t x_q8 = (int64_t)x * 256;
    if (!f->primed) { f->y_q8 = x_q8; f->primed = 1; }
    /* arithmetic shift of a signed product: rounds toward -inf, fine here */
    f->y_q8 += ((x_q8 - f->y_q8) * f->alpha_q15) / 32768;
    int64_t y = f->y_q8 >= 0 ? (f->y_q8 + 128) / 256 : -((-f->y_q8 + 128) / 256);
    return (int32_t)y;
}

void decim_init(decim_t *d, int n)
{
    d->n = (n < 1) ? 1 : (n > 16 ? 16 : n);
    d->count = 0;
    d->acc = 0;
}

int decim_step(decim_t *d, int32_t x, int32_t *out)
{
    d->acc += x;
    if (++d->count < d->n) return 0;
    *out = (int32_t)(d->acc / d->n);
    d->acc = 0;
    d->count = 0;
    return 1;
}

void hyst_init(hyst_t *h, int32_t hi, int32_t lo) { h->hi = hi; h->lo = lo; h->state = 0; }

int hyst_step(hyst_t *h, int32_t x)
{
    if (!h->state && x >= h->hi) { h->state = 1; return +1; }
    if (h->state && x <= h->lo) { h->state = 0; return -1; }
    return 0;
}

void chain_init(chain_t *c)
{
    median3_init(&c->med);
    iir_init(&c->iir, 8192); /* alpha = 0.25 */
    decim_init(&c->dec, 4);
}

int chain_step(chain_t *c, int32_t raw, int32_t *out)
{
    return decim_step(&c->dec, iir_step(&c->iir, median3_step(&c->med, raw)), out);
}
