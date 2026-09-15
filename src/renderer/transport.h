#ifndef TRANSPORTH
#define TRANSPORTH

#include <cfloat>
#include "renderer/scene_view.h"
#include "renderer/random.h"
#include "flat/flat_intersect.h"
#include "renderer/light_sampling.h"

enum render_debug : int32_t { DEBUG_SHADED, DEBUG_NORMALS, DEBUG_TRIANGLE, DEBUG_MATERIAL, DEBUG_BVH, DEBUG_INTERSECTIONS };
struct trace_stats { int boxes = 0, primitives = 0; };
struct trace_result { flat_hit rec{0,0,0,vec3(0,0,0),vec3(0,0,0),-1,-1,-1}; int32_t hit = 0, error = 0; };

RT_HD inline bool trace_box(const flat_node& n, const ray& r, float low, float high) {
    for(int axis = 0; axis < 3; axis++) {
        float d = r.direction()[axis];
        // Explicitly handle parallel rays, including origins on a slab plane.
        if(d == 0) {
            if(r.origin()[axis] < n.minimum[axis] || r.origin()[axis] > n.maximum[axis]) return false;
            continue;
        }
        float inverse = 1.0f/d;
        float a = (n.minimum[axis]-r.origin()[axis])*inverse;
        float b = (n.maximum[axis]-r.origin()[axis])*inverse;
        if(inverse < 0) { float swap = a; a = b; b = swap; }
        low = a > low ? a : low;
        high = b < high ? b : high;
        if(high <= low) return false;
    }
    return high > low;
}

RT_HD inline bool trace_leaf(const scene_view& s, const flat_node& n, const ray& r,
                             float low, float high, flat_hit& hit) {
    bool found;
    if(n.type == FLAT_TRIANGLE) {
        found = flat_triangle_hit(s.triangles[n.primitive],r,low,high,hit);
    } else found = flat_sphere_hit(s.spheres[n.primitive],r,low,high,hit);
    if(found) { hit.primitive_type = n.type; hit.primitive_id = n.primitive; }
    return found;
}

RT_HD inline bool trace_world(const scene_view& s, const ray& r, float low, float high,
                              flat_hit& hit, bool brute, int& error, trace_stats& stats) {
    if(s.root < 0) return false;
    bool found = false;
    float closest = high;
    if(brute) {
        // Exported leaves appear in left-to-right depth-first order, preserving ties.
        for(int i = 0; i < s.node_count; i++) {
            const auto& n = s.nodes[i];
            if(n.type == FLAT_BRANCH) continue;
            stats.primitives++;
            flat_hit candidate;
            if(trace_leaf(s,n,r,low,high,candidate) && (!found || candidate.t <= closest)) {
                hit = candidate; closest = candidate.t; found = true;
            }
        }
        return found;
    }
    int stack[flat_stack_capacity], count = 0;
    stack[count++] = s.root;
    while(count) {
        const auto& n = s.nodes[stack[--count]];
        if(n.type == FLAT_BRANCH) {
            stats.boxes++;
            // Keep exact-distance ties eligible: primitive tests use a strict
            // upper bound and the original BVH lets the later leaf win a tie.
            float limit = found ? nextafterf(closest,high) : high;
            if(!trace_box(n,r,low,limit)) continue;
            int needed = n.left == n.right ? 1 : 2;
            if(count+needed > flat_stack_capacity) { error = 1; return false; }
            if(n.left != n.right) stack[count++] = n.right;
            stack[count++] = n.left;
        } else {
            stats.primitives++;
            flat_hit candidate;
            float limit = found ? nextafterf(closest,high) : high;
            if(trace_leaf(s,n,r,low,limit,candidate) && (!found || candidate.t <= closest)) {
                hit = candidate; closest = candidate.t; found = true;
            }
        }
    }
    return found;
}

RT_HD inline vec3 trace_image(const scene_view& scene, const flat_texture& tex, float u, float v) {
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

RT_HD inline vec3 trace_texture(const scene_view& scene, int32_t id, float u, float v) {
    const flat_texture& tex = scene.textures[id];
    if(tex.type == FLAT_CONSTANT) return tex.color;
    if(tex.type == FLAT_IMAGE) return trace_image(scene,tex,u,v);
    u = u*tex.scale.x()+tex.offset.x();
    v = v*tex.scale.y()+tex.offset.y();
    if(!tex.clamp) { u -= floor(u); v -= floor(v); }
    return tex.color*trace_image(scene,scene.textures[tex.image_id],u,v);
}

RT_HD inline vec3 trace_reflect(const vec3& v, const vec3& n) { return v-2*dot(v,n)*n; }

RT_HD inline bool trace_scatter(const scene_view& scene, const flat_material& mat, const ray& incoming,
                                const flat_hit& rec, pixel_rng& random, vec3& attenuation, ray& scattered) {
    if(mat.type == FLAT_DIFFUSE) {
        vec3 target = rec.p+rec.normal+sample_ball(random);
        scattered = ray(rec.p,target-rec.p,incoming.time());
        attenuation = trace_texture(scene,mat.texture_id,rec.u,rec.v);
        return true;
    }
    if(mat.type == FLAT_METAL) {
        scattered = ray(rec.p,trace_reflect(unit_vector(incoming.direction()),rec.normal)+mat.fuzz*sample_ball(random),incoming.time());
        attenuation = mat.albedo;
        return dot(scattered.direction(),rec.normal) > 0;
    }
    if(mat.type == FLAT_GLASS) {
        vec3 outward, refracted(0,0,0);
        vec3 reflected = trace_reflect(incoming.direction(),rec.normal);
        float ratio, cosine;
        if(dot(incoming.direction(),rec.normal) > 0) {
            outward = -rec.normal; ratio = mat.ref_idx;
            cosine = mat.ref_idx*dot(incoming.direction(),rec.normal)/incoming.direction().length();
        } else {
            outward = rec.normal; ratio = 1.0f/mat.ref_idx;
            cosine = -dot(incoming.direction(),rec.normal)/incoming.direction().length();
        }
        vec3 uv = unit_vector(incoming.direction());
        float dt = dot(uv,outward);
        float discriminant = 1.0f-ratio*ratio*(1-dt*dt);
        float probability = 1;
        if(discriminant > 0) {
            refracted = ratio*(uv-outward*dt)-outward*sqrt(discriminant);
            float r0 = (1-mat.ref_idx)/(1+mat.ref_idx);
            r0 *= r0;
            probability = r0+(1-r0)*pow(1-cosine,5);
        }
        if(random.uniform() < probability) scattered = ray(rec.p,reflected,incoming.time());
        else scattered = ray(rec.p,refracted,incoming.time());
        attenuation = vec3(1,1,1);
        return true;
    }
    return false;
}

RT_HD inline ray trace_camera(const flat_camera& c, float u, float v, pixel_rng& random) {
    vec3 disk = c.lens_radius*sample_disk(random);
    vec3 offset = c.u*disk.x()+c.v*disk.y();
    float time = c.time0+random.uniform()*(c.time1-c.time0);
    return ray(c.origin+offset,c.lower_left_corner+u*c.horizontal+v*c.vertical-c.origin-offset,time);
}

RT_HD inline vec3 id_color(int id) {
    uint32_t bits = (uint32_t(id)+1u)*2654435761u;
    return vec3((bits&255)/255.0f,((bits >> 8)&255)/255.0f,((bits >> 16)&255)/255.0f);
}

RT_HD inline vec3 trace_color(ray current, const scene_view& scene, pixel_rng& random,
                              bool brute, render_debug debug, int& error, bool direct_light = false, uint64_t *rays = nullptr) {
    vec3 throughput(1,1,1);
    vec3 radiance(0,0,0);
    float previous_pdf = 0;
    for(int depth = 0; depth <= 50; depth++) {
        flat_hit rec;
        trace_stats stats;
        if(rays) ++*rays;
        bool hit = trace_world(scene,current,0.001f,FLT_MAX,rec,brute,error,stats);
        if(debug == DEBUG_BVH || debug == DEBUG_INTERSECTIONS) {
            float cost = log2f(1.0f+float(debug == DEBUG_BVH ? stats.boxes : stats.primitives))/10.0f;
            cost = fminf(1,cost);
            return vec3(cost,cost*cost,1-cost);
        }
        if(!hit || error) return radiance;
        if(debug == DEBUG_NORMALS) return 0.5f*(rec.normal+vec3(1,1,1));
        if(debug == DEBUG_TRIANGLE) return rec.primitive_type == FLAT_TRIANGLE ? id_color(rec.primitive_id) : vec3(0.3,0.3,0.3);
        if(debug == DEBUG_MATERIAL) return id_color(rec.material_id);
        const auto& mat = scene.materials[rec.material_id];
        if(mat.type == FLAT_LIGHT) {
            float weight = previous_pdf > 0 ? mis_weight(previous_pdf,hit_light_pdf(scene,current.origin(),rec)) : 1;
            return radiance+weight*throughput*trace_texture(scene,mat.texture_id,rec.u,rec.v);
        }
        if(depth == 50) break;
        ray scattered; vec3 attenuation;
        if(direct_light && mat.type == FLAT_DIFFUSE) {
            attenuation = trace_texture(scene,mat.texture_id,rec.u,rec.v);
            if(scene.light_count) {
                auto light = sample_light(scene,random);
                vec3 delta = light.point-rec.p;
                float distance = delta.length();
                if(distance > 0.002f && std::isfinite(distance)) {
                    vec3 direction = delta/distance;
                    float cosine = fmaxf(0,dot(rec.normal,direction));
                    float pdf = light_direction_pdf(scene.light_area,rec.p,light.point,light.normal);
                    if(cosine > 0 && pdf > 0) {
                        flat_hit blocker; trace_stats shadow_stats;
                        if(rays) ++*rays;
                        bool blocked = trace_world(scene,ray(rec.p,direction,current.time()),0.001f,distance-0.001f,blocker,brute,error,shadow_stats);
                        if(!blocked && !error) {
                            const auto& emitter = scene.materials[light.material];
                            vec3 emission = trace_texture(scene,emitter.texture_id,light.u,light.v);
                            float bsdf_pdf = cosine/float(M_PI);
                            radiance += throughput*attenuation*emission*(bsdf_pdf*mis_weight(pdf,bsdf_pdf)/pdf);
                        }
                    }
                }
            }
            vec3 direction = cosine_direction(rec.normal,random);
            previous_pdf = fmaxf(0,dot(rec.normal,direction))/float(M_PI);
            throughput *= attenuation;
            current = ray(rec.p,direction,current.time());
            continue;
        }
        previous_pdf = 0;
        if(!trace_scatter(scene,mat,current,rec,random,attenuation,scattered)) break;
        throughput *= attenuation;
        current = scattered;
    }
    return radiance;
}

RT_HD inline vec3 trace_pixel_sample(const scene_view& scene, const flat_camera& c, int x, int y,
                                     int width, int height, pixel_rng& random, bool brute,
                                     render_debug debug, int& error, bool direct_light = false, uint64_t *rays = nullptr) {
    float u = (x+random.uniform())/float(width);
    float v = (y+random.uniform())/float(height);
    ray r = trace_camera(c,u,v,random);
    return trace_color(r,scene,random,brute,debug,error,direct_light,rays);
}

#endif
