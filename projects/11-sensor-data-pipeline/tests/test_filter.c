#include "filter.h"
#include "minitest.h"
#include "signal_gen.h"

TEST(median_removes_single_spike)
{
    median3_t m;
    median3_init(&m);
    int32_t in[] = { 10, 10, 10, 500, 10, 10, -400, 10, 11 };
    int32_t out[9];
    for (int i = 0; i < 9; i++) out[i] = median3_step(&m, in[i]);
    for (int i = 2; i < 9; i++) CHECK(out[i] >= 10 && out[i] <= 11);
}

TEST(median_follows_steps_with_one_sample_delay)
{
    median3_t m;
    median3_init(&m);
    for (int i = 0; i < 5; i++) median3_step(&m, 0);
    CHECK_EQ_INT(median3_step(&m, 100), 0);
    CHECK_EQ_INT(median3_step(&m, 100), 100);
}

TEST(iir_primes_with_first_sample)
{
    iir_t f;
    iir_init(&f, 8192);
    CHECK_EQ_INT(iir_step(&f, 1234), 1234);
}

TEST(iir_step_response_converges)
{
    iir_t f;
    iir_init(&f, 8192); /* alpha 0.25 */
    iir_step(&f, 0);
    int32_t y = 0;
    y = iir_step(&f, 1000);
    CHECK_EQ_INT(y, 250); /* one step: 0 + 0.25 * 1000 */
    for (int i = 0; i < 100; i++) y = iir_step(&f, 1000);
    CHECK_NEAR(y, 1000, 1);
    for (int i = 0; i < 100; i++) y = iir_step(&f, -1000);
    CHECK_NEAR(y, -1000, 1);
}

TEST(iir_alpha_is_clamped)
{
    iir_t f;
    iir_init(&f, 99999);
    CHECK_EQ_INT(f.alpha_q15, 32767);
    iir_init(&f, -5);
    CHECK_EQ_INT(f.alpha_q15, 0);
    iir_step(&f, 7);
    CHECK_EQ_INT(iir_step(&f, 1000), 7); /* alpha 0 = frozen */
}

TEST(decimator_averages_blocks)
{
    decim_t d;
    decim_init(&d, 4);
    int32_t o = -1;
    CHECK(!decim_step(&d, 1, &o));
    CHECK(!decim_step(&d, 2, &o));
    CHECK(!decim_step(&d, 3, &o));
    CHECK(decim_step(&d, 6, &o));
    CHECK_EQ_INT(o, 3);
    decim_init(&d, 100);
    CHECK_EQ_INT(d.n, 16);
}

TEST(hysteresis_edges)
{
    hyst_t h;
    hyst_init(&h, 100, 80);
    CHECK_EQ_INT(hyst_step(&h, 99), 0);
    CHECK_EQ_INT(hyst_step(&h, 100), 1);
    CHECK_EQ_INT(hyst_step(&h, 150), 0);
    CHECK_EQ_INT(hyst_step(&h, 90), 0);  /* inside band: stays on */
    CHECK_EQ_INT(hyst_step(&h, 80), -1);
    CHECK_EQ_INT(hyst_step(&h, 95), 0);  /* inside band: stays off */
}

TEST(generator_is_deterministic)
{
    siggen_t a, b;
    siggen_init(&a, 1);
    siggen_init(&b, 1);
    int spikes = 0;
    for (int i = 0; i < 370; i++) {
        int s;
        CHECK_EQ_INT(siggen_next(&a, &s), siggen_next(&b, NULL));
        spikes += s;
    }
    CHECK_EQ_INT(spikes, 10);
}

TEST(chain_rejects_spikes_end_to_end)
{
    siggen_t g;
    chain_t c;
    siggen_init(&g, 5);
    chain_init(&c);
    int32_t o, mx = 0;
    int outs = 0;
    for (int i = 0; i < 2048; i++)
        if (chain_step(&c, siggen_next(&g, NULL), &o)) { outs++; if (o > mx) mx = o; }
    CHECK_EQ_INT(outs, 512);
    CHECK(mx < 3100);
    CHECK(mx > 2800);
}

int main(void)
{
    RUN(median_removes_single_spike);
    RUN(median_follows_steps_with_one_sample_delay);
    RUN(iir_primes_with_first_sample);
    RUN(iir_step_response_converges);
    RUN(iir_alpha_is_clamped);
    RUN(decimator_averages_blocks);
    RUN(hysteresis_edges);
    RUN(generator_is_deterministic);
    RUN(chain_rejects_spikes_end_to_end);
    return minitest_summary();
}
