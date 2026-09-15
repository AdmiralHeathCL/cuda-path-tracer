#ifndef LIGHTSAMPLINGH
#define LIGHTSAMPLINGH

#include "renderer/scene_view.h"
#include "renderer/random.h"
#include "geometry/sphere_uv.h"

struct light_sample { vec3 point, normal; float u, v; int material; };

RT_HD inline float mis_weight(float a, float b) {
    // Scale before squaring so very small/large PDFs do not overflow or underflow.
    float scale = fmaxf(a,b);
    if(!(scale > 0)) return 0;
    a /= scale; b /= scale;
    return a*a/(a*a+b*b);
}

RT_HD inline vec3 cosine_direction(vec3 normal, pixel_rng& random) {
    float u = random.uniform(), v = random.uniform();
    float radius = sqrtf(u), angle = 2*float(M_PI)*v;
    vec3 helper = fabsf(normal.x()) > 0.9f ? vec3(0,1,0) : vec3(1,0,0);
    vec3 tangent = unit_vector(cross(helper,normal));
    vec3 bitangent = cross(normal,tangent);
    return radius*cosf(angle)*tangent+radius*sinf(angle)*bitangent+sqrtf(1-u)*normal;
}

RT_HD inline light_sample sample_light(const scene_view& scene, pixel_rng& random) {
    float choice = random.uniform()*scene.light_area;
    int low = 0, high = scene.light_count-1;
    while(low < high) {
        int middle = low+(high-low)/2;
        if(choice < scene.lights[middle].cumulative_area) high = middle;
        else low = middle+1;
    }
    const auto& light = scene.lights[low];
    float u = random.uniform(), v = random.uniform();
    light_sample result;
    if(light.type == FLAT_TRIANGLE) {
        const auto& t = scene.triangles[light.primitive];
        float root = sqrtf(u), a = 1-root, b = root*(1-v), c = root*v;
        result.point = a*t.v0+b*t.v1+c*t.v2;
        result.normal = unit_vector(cross(t.v1-t.v0,t.v2-t.v0));
        vec3 uv = a*t.uv0+b*t.uv1+c*t.uv2;
        result.u = uv.x(); result.v = uv.y(); result.material = t.material_id;
    } else {
        const auto& s = scene.spheres[light.primitive];
        float z = 1-2*u, radius = sqrtf(fmaxf(0,1-z*z)), angle = 2*float(M_PI)*v;
        result.normal = vec3(radius*cosf(angle),z,radius*sinf(angle));
        result.point = s.center+fabsf(s.radius)*result.normal;
        if(s.radius < 0) result.normal = -result.normal;
        get_sphere_uv(result.normal,result.u,result.v); result.material = s.material_id;
    }
    return result;
}

RT_HD inline float light_direction_pdf(float area, vec3 from, vec3 point, vec3 normal) {
    vec3 delta = point-from;
    float distance_squared = delta.squared_length();
    if(!(area > 0) || !(distance_squared > 0)) return 0;
    float cosine = fabsf(dot(normal,delta))/sqrtf(distance_squared);
    if(!(cosine > 0)) return 0;
    float pdf = distance_squared/(cosine*area);
    return std::isfinite(pdf) ? pdf : 0;
}

RT_HD inline float hit_light_pdf(const scene_view& scene, vec3 from, const flat_hit& hit) {
    vec3 normal = hit.normal;
    if(hit.primitive_type == FLAT_TRIANGLE) {
        const auto& t = scene.triangles[hit.primitive_id];
        normal = unit_vector(cross(t.v1-t.v0,t.v2-t.v0));
    }
    return light_direction_pdf(scene.light_area,from,hit.p,normal);
}

#endif
