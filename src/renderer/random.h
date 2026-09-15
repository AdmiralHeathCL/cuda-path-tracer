#ifndef RENDERERRANDOMH
#define RENDERERRANDOMH

#include <cstdint>
#include "core/vec3.h"

// PCG-XSH-RR 64/32: each pixel has its own odd-increment stream.
struct pixel_rng {
    uint64_t state, increment;

    RT_HD uint32_t next() {
        uint64_t old = state;
        state = old*6364136223846793005ULL+increment;
        uint32_t shifted = uint32_t(((old >> 18u)^old) >> 27u);
        uint32_t rotation = uint32_t(old >> 59u);
        return (shifted >> rotation) | (shifted << ((-rotation)&31));
    }
    RT_HD void seed(uint64_t value, uint64_t pixel) {
        state = 0; increment = (pixel << 1u)|1u;
        next(); state += value; next();
    }
    RT_HD float uniform() { return (next() >> 8)*0x1p-24f; }
};

RT_HD inline vec3 sample_ball(pixel_rng& random) {
    vec3 p;
    do {
        // Sequence each draw explicitly for host/device evaluation-order parity.
        float x = random.uniform(), y = random.uniform(), z = random.uniform();
        p = 2.0f*vec3(x,y,z)-vec3(1,1,1);
    } while(p.squared_length() >= 1.0f);
    return p;
}

RT_HD inline vec3 sample_disk(pixel_rng& random) {
    vec3 p;
    do {
        float x = random.uniform(), y = random.uniform();
        p = 2.0f*vec3(x,y,0)-vec3(1,1,0);
    } while(p.squared_length() >= 1.0f);
    return p;
}

#endif
