#ifndef SCENEVIEWH
#define SCENEVIEWH

#include "flat/flat_data.h"
#include <cmath>
#include <stdexcept>

struct flat_light { int32_t type, primitive; float cumulative_area; };

inline std::vector<flat_light> scene_lights(const flat_scene& s) {
    std::vector<flat_light> lights;
    double area = 0;
    auto add = [&](int type, int id, double size) {
        if(!(size > 0) || !std::isfinite(size)) return;
        area += size;
        if(!std::isfinite(area) || area > MAXFLOAT) throw std::invalid_argument("light area exceeds range");
        lights.push_back({type,id,float(area)});
    };
    for(size_t i = 0; i < s.triangles.size(); i++) {
        const auto& t = s.triangles[i];
        if(s.materials[t.material_id].type == FLAT_LIGHT) add(FLAT_TRIANGLE,int(i),0.5*cross(t.v1-t.v0,t.v2-t.v0).length());
    }
    for(size_t i = 0; i < s.spheres.size(); i++) {
        const auto& p = s.spheres[i];
        if(s.materials[p.material_id].type == FLAT_LIGHT) add(FLAT_SPHERE,int(i),4*M_PI*double(p.radius)*p.radius);
    }
    return lights;
}

// These addresses refer to host arrays on the CPU and uploaded arrays on the GPU.
struct scene_view {
    const flat_triangle *triangles;
    const flat_sphere *spheres;
    const flat_node *nodes;
    const flat_material *materials;
    const flat_texture *textures;
    const unsigned char *pixels;
    int32_t root, node_count;
    const flat_light *lights = nullptr;
    int32_t light_count = 0;
    float light_area = 0;
};

inline scene_view host_view(const flat_scene& s) {
    return {s.triangles.data(),s.spheres.data(),s.nodes.data(),s.materials.data(),
            s.textures.data(),s.pixels.data(),s.root,int32_t(s.nodes.size())};
}

inline scene_view host_view(const flat_scene& s, const std::vector<flat_light>& lights) {
    auto view = host_view(s);
    view.lights = lights.data(); view.light_count = int32_t(lights.size());
    view.light_area = lights.empty() ? 0 : lights.back().cumulative_area;
    return view;
}

#endif
