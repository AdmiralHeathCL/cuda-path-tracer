#ifndef PORTABLERENDERERH
#define PORTABLERENDERERH

#include <limits>
#include "flat/flat_validate.h"
#include "renderer/transport.h"

#include "renderer/render_validation.h"

// Execute the same sampling/tracing code as CUDA on the CPU for validation.
class portable_renderer {
    public:
        portable_renderer(const flat_scene& source, const flat_camera& camera, int w, int h,
                          uint64_t seed, bool brute = false, render_debug debug = DEBUG_SHADED, bool direct_light = false) :
            scene(source), cam(camera), width(w), height(h), seed_value(seed), brute_force(brute), mode(debug),
            sum(checked_pixel_count(w,h),vec3(0,0,0)), random(sum.size()), lighting(direct_light) {
            validate_flat_scene(scene); validate_render_mode(mode); lights = scene_lights(scene); reset(camera);
        }
        void reset(const flat_camera& camera) {
            validate_render_camera(camera);
            cam = camera; samples = 0; rays = 0;
            for(size_t i = 0; i < sum.size(); i++) { sum[i] = vec3(0,0,0); random[i].seed(seed_value,i); }
        }
        void add_samples(int count) {
            if(count <= 0 || count > INT32_MAX-samples) throw std::invalid_argument("invalid accumulated sample count");
            scene_view view = host_view(scene,lights);
            for(int y = 0; y < height; y++) {
                for(int x = 0; x < width; x++) {
                    size_t i = size_t(y)*width+x;
                    int error = 0;
                    for(int s = 0; s < count; s++) sum[i] += trace_pixel_sample(view,cam,x,y,width,height,random[i],brute_force,mode,error,lighting,&rays);
                    if(error) throw std::runtime_error("portable traversal stack overflow");
                }
            }
            samples += count;
        }
        std::vector<vec3> image() const {
            std::vector<vec3> result = sum;
            if(samples) for(auto& pixel : result) pixel /= float(samples);
            return result;
        }
        int sample_count() const { return samples; }
        uint64_t rays_traced() const { return rays; }

    private:
        flat_scene scene;
        flat_camera cam;
        int width, height, samples = 0;
        uint64_t seed_value;
        bool brute_force;
        render_debug mode;
        std::vector<vec3> sum;
        std::vector<pixel_rng> random;
        bool lighting;
        uint64_t rays = 0;
        std::vector<flat_light> lights;
};

#endif
