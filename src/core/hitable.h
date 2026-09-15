#ifndef HITABLEH
#define HITABLEH

#include "core/ray.h"
#include "bvh/aabb.h"

class material;

struct hit_record {
    float t;
    float u; // texture coordinate 1
    float v; // texture coordinate 2
    vec3 p; // position of hit
    vec3 normal;
    material *mat_ptr;
};

class hitable {
    public:
        virtual ~hitable() = default;
        virtual bool hit(const ray& r, float t_min, float t_max, hit_record& rec) const = 0;
        virtual bool bounding_box(float t0, float t1, aabb& box) const = 0;
};

#endif
