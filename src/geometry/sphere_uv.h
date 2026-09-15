#ifndef SPHEREUVH
#define SPHEREUVH

#include <math.h>
#include "core/vec3.h"

RT_HD inline void get_sphere_uv(const vec3& p, float& u, float& v) {
    float phi = atan2(p.z(), p.x());
    float theta = asin(p.y());

    u = 1 - (phi + M_PI) / (2*M_PI);
    v = (theta + M_PI/2) / M_PI;
}

#endif