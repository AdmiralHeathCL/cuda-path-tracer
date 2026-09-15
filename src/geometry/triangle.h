#ifndef TRIANGLEH
#define TRIANGLEH

#include <cmath>
#include <limits>
#include "core/hitable.h"

class triangle : public hitable {
    public:
        triangle(vec3 a, vec3 b, vec3 c, material *m) :
            v0(a), v1(b), v2(c),
            n0(0,0,0), n1(0,0,0), n2(0,0,0),
            mat_ptr(m), smooth(false) {}

        triangle(
            vec3 a, vec3 b, vec3 c,
            vec3 normal0, vec3 normal1, vec3 normal2,
            material *m
        ) :
            v0(a), v1(b), v2(c),
            n0(normal0), n1(normal1), n2(normal2),
            mat_ptr(m), smooth(true) {}

        virtual bool hit(
            const ray& r,
            float t_min,
            float t_max,
            hit_record& rec
        ) const override;

        virtual bool bounding_box(
            float t0,
            float t1,
            aabb& box
        ) const override;

        vec3 v0, v1, v2;
        vec3 n0, n1, n2;
        material *mat_ptr;
        bool smooth;
        // The third component is unused; vec3 keeps the existing math vocabulary.
        vec3 uv0 = vec3(0,0,0);
        vec3 uv1 = vec3(1,0,0);
        vec3 uv2 = vec3(0,1,0);
};

inline bool triangle::hit(const ray& r, float t_min, float t_max, hit_record& rec) const {
    vec3 edge1 = v1 - v0;
    vec3 edge2 = v2 - v0;
    vec3 outward_normal = cross(edge1, edge2);
    float normal_length = outward_normal.length();
    float direction_length = r.direction().length();

    if(!(normal_length > 0) || !(direction_length > 0)) return false;

    // Moller-Trumbore, without back-face culling. Scale the parallel tolerance
    // because our camera and scattered rays are not necessarily unit length.
    vec3 pvec = cross(r.direction(), edge2);
    float det = dot(edge1, pvec);
    if(fabs(det) <= 0.0000001f * normal_length * direction_length) return false;

    float inv_det = 1.0f / det;
    vec3 tvec = r.origin() - v0;
    float u = dot(tvec, pvec) * inv_det;
    if(!(u >= 0 && u <= 1)) return false;

    vec3 qvec = cross(tvec, edge1);
    float v = dot(r.direction(), qvec) * inv_det;
    if(!(v >= 0 && u + v <= 1)) return false;

    float t = dot(edge2, qvec) * inv_det;
    if(!(t > t_min && t < t_max)) return false;

    outward_normal /= normal_length;
    vec3 normal = outward_normal;
    if(smooth) {
        vec3 interpolated = (1-u-v)*n0 + u*n1 + v*n2;
        float length_squared = interpolated.squared_length();
        if(length_squared > 0 && std::isfinite(length_squared)) {
            normal = unit_vector(interpolated);
            if(dot(normal, outward_normal) < 0) normal = -normal;
            if(dot(normal, outward_normal) == 0) normal = outward_normal;
        }
    }

    rec.t = t;
    rec.p = r.point_at_parameter(t);
    rec.normal = normal;
    rec.mat_ptr = mat_ptr;
    vec3 uv = (1-u-v)*uv0 + u*uv1 + v*uv2;
    rec.u = uv.x();
    rec.v = uv.y();
    return true;
}

inline bool triangle::bounding_box(float, float, aabb& box) const {
    vec3 minimum, maximum;
    float infinity = std::numeric_limits<float>::infinity();
    for(int axis = 0; axis < 3; axis++) {
        minimum[axis] = ffmin(v0[axis], ffmin(v1[axis], v2[axis]));
        maximum[axis] = ffmax(v0[axis], ffmax(v1[axis], v2[axis]));
        // Pad every axis for grazing rays as well as zero-thickness bounds.
        // nextafter also expands bounds when 0.0001 rounds away at large coordinates.
        minimum[axis] = std::nextafter(minimum[axis] - 0.0001f, -infinity);
        maximum[axis] = std::nextafter(maximum[axis] + 0.0001f, infinity);
    }
    box = aabb(minimum, maximum);
    return true;
}

#endif
