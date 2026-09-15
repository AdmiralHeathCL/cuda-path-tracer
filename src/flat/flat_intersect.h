#ifndef FLATINTERSECTH
#define FLATINTERSECTH

#include <cmath>
#include "flat/flat_data.h"
#include "geometry/sphere_uv.h"

RT_HD inline bool flat_triangle_hit_geometry(const flat_triangle& face, vec3 v0, vec3 v1, vec3 v2,
                                             const ray& r, float t_min, float t_max, flat_hit& rec) {
    vec3 edge1 = v1-v0;
    vec3 edge2 = v2-v0;
    vec3 outward = cross(edge1,edge2);
    float normal_length = outward.length();
    float direction_length = r.direction().length();
    if(!(normal_length > 0) || !(direction_length > 0)) return false;
    vec3 pvec = cross(r.direction(),edge2);
    float det = dot(edge1,pvec);
    if(fabs(det) <= 0.0000001f*normal_length*direction_length) return false;
    float inv_det = 1.0f/det;
    vec3 tvec = r.origin()-v0;
    float u = dot(tvec,pvec)*inv_det;
    if(!(u >= 0 && u <= 1)) return false;
    vec3 qvec = cross(tvec,edge1);
    float v = dot(r.direction(),qvec)*inv_det;
    if(!(v >= 0 && u+v <= 1)) return false;
    float t = dot(edge2,qvec)*inv_det;
    if(!(t > t_min && t < t_max)) return false;
    outward /= normal_length;
    vec3 normal = outward;
    if(face.smooth) {
        vec3 interpolated = (1-u-v)*face.n0+u*face.n1+v*face.n2;
        float length_squared = interpolated.squared_length();
        if(length_squared > 0 && std::isfinite(length_squared)) {
            normal = unit_vector(interpolated);
            if(dot(normal,outward) < 0) normal = -normal;
            if(dot(normal,outward) == 0) normal = outward;
        }
    }
    rec.t = t; rec.p = r.point_at_parameter(t); rec.normal = normal;
    vec3 uv = (1-u-v)*face.uv0+u*face.uv1+v*face.uv2;
    rec.u = uv.x(); rec.v = uv.y(); rec.material_id = face.material_id;
    return true;
}

RT_HD inline bool flat_triangle_hit(const flat_triangle& face, const ray& r, float t_min, float t_max, flat_hit& rec) {
    return flat_triangle_hit_geometry(face,face.v0,face.v1,face.v2,r,t_min,t_max,rec);
}

RT_HD inline bool flat_sphere_hit(const flat_sphere& sphere, const ray& r, float t_min, float t_max, flat_hit& rec) {
    vec3 oc = r.origin()-sphere.center;
    float a = dot(r.direction(),r.direction());
    float b = dot(oc,r.direction());
    float c = dot(oc,oc)-sphere.radius*sphere.radius;
    float discriminant = b*b-a*c;
    if(discriminant > 0) {
        float t = (-b-sqrt(discriminant))/a;
        if(!(t < t_max && t > t_min)) t = (-b+sqrt(discriminant))/a;
        if(t < t_max && t > t_min) {
            rec.t = t; rec.p = r.point_at_parameter(t);
            rec.normal = (rec.p-sphere.center)/sphere.radius;
            get_sphere_uv(rec.normal,rec.u,rec.v);
            rec.material_id = sphere.material_id;
            return true;
        }
    }
    return false;
}

#endif
