#ifndef RENDERH
#define RENDERH

#include "materials/material.h"

#include "display/color.h"

inline vec3 color(const ray& r, hitable *world, int depth) {
    hit_record rec;
    if(world -> hit(r, 0.001, MAXFLOAT, rec)) {
        ray scattered;
        vec3 attenuation;
        vec3 emitted = rec.mat_ptr->emitted(rec.u, rec.v, rec.p);
        if(depth < 50 && rec.mat_ptr->scatter(r, rec, attenuation, scattered)) {
            return emitted + attenuation * color(scattered, world, depth + 1);
        } else {
            return emitted;
        }
    } else {
        return vec3(0,0,0);
    }
}

#endif
