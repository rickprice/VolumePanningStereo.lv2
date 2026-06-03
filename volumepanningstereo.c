#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "lv2/core/lv2.h"

#if defined(__AVX__)
#  include <immintrin.h>
#elif defined(__SSE__)
#  include <xmmintrin.h>
#elif defined(__ARM_NEON)
#  include <arm_neon.h>
#endif

#define PLUGIN_URI "http://fprice.pricemail.ca/plugins/volumepanningstereo"

typedef enum {
    PORT_IN_L        = 0,
    PORT_IN_R        = 1,
    PORT_OUT_L       = 2,
    PORT_OUT_R       = 3,
    PORT_ENABLED     = 4,
    PORT_VOLUME      = 5,
    PORT_PAN         = 6,
    PORT_MUTE        = 7,
    PORT_MUTE_INVERT = 8,
} PortIndex;

/* Fields must stay in PortIndex order (0–8) — do not reorder. */
typedef struct {
    const float* in_l;        /* 0 PORT_IN_L        */
    const float* in_r;        /* 1 PORT_IN_R        */
    float*       out_l;       /* 2 PORT_OUT_L       */
    float*       out_r;       /* 3 PORT_OUT_R       */
    const float* enabled;     /* 4 PORT_ENABLED     */
    const float* volume;      /* 5 PORT_VOLUME      */
    const float* pan;         /* 6 PORT_PAN         */
    const float* mute;        /* 7 PORT_MUTE        */
    const float* mute_invert; /* 8 PORT_MUTE_INVERT */
} Plugin;

static LV2_Handle instantiate(
    const LV2_Descriptor*     descriptor,
    double                    rate,
    const char*               bundle_path,
    const LV2_Feature* const* features)
{
    (void)descriptor; (void)rate; (void)bundle_path; (void)features;
    return (LV2_Handle)calloc(1, sizeof(Plugin));
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data)
{
    Plugin* self = (Plugin*)instance;
    switch ((PortIndex)port) {
    case PORT_IN_L:      self->in_l      = (const float*)data; break;
    case PORT_IN_R:      self->in_r      = (const float*)data; break;
    case PORT_OUT_L:     self->out_l     = (float*)data;       break;
    case PORT_OUT_R:     self->out_r     = (float*)data;       break;
    case PORT_PAN:       self->pan       = (const float*)data; break;
    case PORT_VOLUME:    self->volume    = (const float*)data; break;
    case PORT_MUTE:         self->mute         = (const float*)data; break;
    case PORT_ENABLED:      self->enabled      = (const float*)data; break;
    case PORT_MUTE_INVERT:  self->mute_invert  = (const float*)data; break;
    }
}

static void activate(LV2_Handle instance) { (void)instance; }

static void run(LV2_Handle instance, uint32_t n_samples)
{
    const Plugin* self  = (const Plugin*)instance;
    const float*  in_l  = self->in_l;
    const float*  in_r  = self->in_r;
    float*        out_l = self->out_l;
    float*        out_r = self->out_r;

    const int muted = (*self->mute >= 0.5f) ^ (*self->mute_invert >= 0.5f);
    if (muted) {
        memset(out_l, 0, n_samples * sizeof(float));
        memset(out_r, 0, n_samples * sizeof(float));
        return;
    }

    /* Stereo balance control: pan=-1 → full left, pan=0 → centre (unity on
       both channels), pan=+1 → full right.  The dominant side stays at unity
       gain; the opposing side attenuates linearly to zero. */
    const float pan = *self->pan;
    const float vol = powf(10.0f, *self->volume / 20.0f);
    const float gl  = vol * (pan <= 0.0f ? 1.0f : 1.0f - pan);
    const float gr  = vol * (pan >= 0.0f ? 1.0f : 1.0f + pan);

    uint32_t i = 0;

#if defined(__AVX__)
    {
        const __m256 vgl = _mm256_set1_ps(gl);
        const __m256 vgr = _mm256_set1_ps(gr);
        for (; i + 8 <= n_samples; i += 8) {
            _mm256_storeu_ps(&out_l[i], _mm256_mul_ps(_mm256_loadu_ps(&in_l[i]), vgl));
            _mm256_storeu_ps(&out_r[i], _mm256_mul_ps(_mm256_loadu_ps(&in_r[i]), vgr));
        }
    }
#elif defined(__SSE__)
    {
        const __m128 vgl = _mm_set1_ps(gl);
        const __m128 vgr = _mm_set1_ps(gr);
        for (; i + 4 <= n_samples; i += 4) {
            _mm_storeu_ps(&out_l[i], _mm_mul_ps(_mm_loadu_ps(&in_l[i]), vgl));
            _mm_storeu_ps(&out_r[i], _mm_mul_ps(_mm_loadu_ps(&in_r[i]), vgr));
        }
    }
#elif defined(__ARM_NEON)
    {
        const float32x4_t vgl = vdupq_n_f32(gl);
        const float32x4_t vgr = vdupq_n_f32(gr);
        for (; i + 4 <= n_samples; i += 4) {
            vst1q_f32(&out_l[i], vmulq_f32(vld1q_f32(&in_l[i]), vgl));
            vst1q_f32(&out_r[i], vmulq_f32(vld1q_f32(&in_r[i]), vgr));
        }
    }
#endif

    for (; i < n_samples; ++i) {
        out_l[i] = in_l[i] * gl;
        out_r[i] = in_r[i] * gr;
    }
}

static void deactivate(LV2_Handle instance) { (void)instance; }

static void cleanup(LV2_Handle instance)
{
    free(instance);
}

static const void* extension_data(const char* uri)
{
    (void)uri;
    return NULL;
}

static const LV2_Descriptor descriptor = {
    PLUGIN_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data,
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : NULL;
}
