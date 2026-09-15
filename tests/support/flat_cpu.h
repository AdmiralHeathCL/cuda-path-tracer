#ifndef FLATCPUH
#define FLATCPUH

#include <cmath>
#include <stdexcept>
#include "flat/flat_data.h"
#include "flat/flat_intersect.h"
#include "bvh/aabb.h"
#include "geometry/sphere_uv.h"
#include "materials/material.h"
#include "core/camera.h"

// CPU reference operations on plain records. No geometry/material virtual calls.
inline bool flat_hit_world(const flat_scene& scene, const ray& r, float t_min, float t_max, flat_hit& rec) {
    if(scene.root < 0) return false;
    int32_t stack[flat_stack_capacity];
    int size = 0;
    stack[size++] = scene.root;
    bool found = false;
    float closest = t_max;
    while(size) {
        const flat_node& node = scene.nodes[stack[--size]];
        if(node.type == FLAT_BRANCH) {
            // Keep the source interval: exact ties choose the rightmost DFS hit,
            // matching the original recursive BVH without primitive-leaf culling.
            if(!aabb(node.minimum,node.maximum).hit(r,t_min,t_max)) continue;
            int needed = node.left == node.right ? 1 : 2;
            if(size+needed > flat_stack_capacity) throw std::runtime_error("flat traversal stack overflow");
            if(node.left != node.right) stack[size++] = node.right;
            stack[size++] = node.left;
        } else {
            flat_hit candidate;
            bool hit = node.type == FLAT_TRIANGLE ?
                flat_triangle_hit(scene.triangles[node.primitive],r,t_min,t_max,candidate) :
                flat_sphere_hit(scene.spheres[node.primitive],r,t_min,t_max,candidate);
            if(hit && (!found || candidate.t <= closest)) {
                candidate.primitive_type = node.type;
                candidate.primitive_id = node.primitive;
                rec = candidate;
                closest = candidate.t;
                found = true;
            }
        }
    }
    return found;
}

inline vec3 flat_image_value(const flat_scene& scene, const flat_texture& tex, float u, float v) {
    u = std::fmin(1.0f,std::fmax(0.0f,u));
    v = std::fmin(1.0f,std::fmax(0.0f,v));
    int i = u*tex.width;
    int j = (1-v)*tex.height-0.001;
    if(i < 0) i = 0;
    if(j < 0) j = 0;
    if(i > tex.width-1) i = tex.width-1;
    if(j > tex.height-1) j = tex.height-1;
    size_t offset = size_t(tex.byte_offset)+3*size_t(i)+3*size_t(tex.width)*size_t(j);
    float red = int(scene.pixels[offset])/255.0;
    float green = int(scene.pixels[offset+1])/255.0;
    float blue = int(scene.pixels[offset+2])/255.0;
    return vec3(red,green,blue);
}

inline vec3 flat_texture_value(const flat_scene& scene, int32_t id, float u, float v) {
    const flat_texture& tex = scene.textures[id];
    if(tex.type == FLAT_CONSTANT) return tex.color;
    if(tex.type == FLAT_IMAGE) return flat_image_value(scene,tex,u,v);
    u = u*tex.scale.x()+tex.offset.x();
    v = v*tex.scale.y()+tex.offset.y();
    if(!tex.clamp) { u -= floor(u); v -= floor(v); }
    return tex.color*flat_image_value(scene,scene.textures[tex.image_id],u,v);
}

inline bool flat_scatter(const flat_scene& scene, const flat_material& mat, const ray& incoming,
                         const flat_hit& rec, vec3& attenuation, ray& scattered) {
    switch(mat.type) {
        case FLAT_DIFFUSE: {
            vec3 target = rec.p+rec.normal+random_in_unit_sphere();
            scattered = ray(rec.p,target-rec.p,incoming.time());
            attenuation = flat_texture_value(scene,mat.texture_id,rec.u,rec.v);
            return true;
        }
        case FLAT_METAL: {
            vec3 reflected = reflect(unit_vector(incoming.direction()),rec.normal);
            scattered = ray(rec.p,reflected+mat.fuzz*random_in_unit_sphere(),incoming.time());
            attenuation = mat.albedo;
            return dot(scattered.direction(),rec.normal) > 0;
        }
        case FLAT_GLASS: {
            vec3 outward, refracted;
            vec3 reflected = reflect(incoming.direction(),rec.normal);
            attenuation = vec3(1,1,1);
            float ratio, cosine;
            if(dot(incoming.direction(),rec.normal) > 0) {
                outward = -rec.normal; ratio = mat.ref_idx;
                cosine = mat.ref_idx*dot(incoming.direction(),rec.normal)/incoming.direction().length();
            } else {
                outward = rec.normal; ratio = 1.0/mat.ref_idx;
                cosine = -dot(incoming.direction(),rec.normal)/incoming.direction().length();
            }
            float probability = refract(incoming.direction(),outward,ratio,refracted) ? schlick(cosine,mat.ref_idx) : 1.0;
            if(drand48() < probability) scattered = ray(rec.p,reflected,incoming.time());
            else scattered = ray(rec.p,refracted,incoming.time());
            return true;
        }
        case FLAT_LIGHT: return false;
    }
    throw std::runtime_error("invalid flat material type");
}

inline vec3 flat_color(const ray& initial, const flat_scene& scene) {
    // Reverse-fold attenuation to match the reference recursion's arithmetic order.
    vec3 attenuation[50];
    int depth = 0;
    ray current = initial;
    vec3 result(0,0,0);
    while(true) {
        flat_hit rec;
        if(!flat_hit_world(scene,current,0.001f,MAXFLOAT,rec)) break;
        const auto& mat = scene.materials[rec.material_id];
        if(mat.type == FLAT_LIGHT) {
            result = flat_texture_value(scene,mat.texture_id,rec.u,rec.v);
            break;
        }
        if(depth == 50) break;
        ray scattered;
        if(!flat_scatter(scene,mat,current,rec,attenuation[depth],scattered)) break;
        depth++;
        current = scattered;
    }
    while(depth) result = vec3(0,0,0)+attenuation[--depth]*result;
    return result;
}

inline ray flat_camera_ray(const flat_camera& c, float s, float t) {
    vec3 rd = c.lens_radius*random_in_unit_disk();
    vec3 offset = c.u*rd.x()+c.v*rd.y();
    float time = c.time0+drand48()*(c.time1-c.time0);
    return ray(c.origin+offset,c.lower_left_corner+s*c.horizontal+t*c.vertical-c.origin-offset,time);
}

#endif
