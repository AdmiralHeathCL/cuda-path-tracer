#ifndef CUDARENDERERH
#define CUDARENDERERH

#include <memory>
#include <string>
#include "renderer/transport.h"

bool cuda_device_available(std::string& description);

class cuda_renderer {
    public:
        cuda_renderer(const flat_scene& scene, const flat_camera& camera, int width, int height,
                      uint64_t seed, bool brute = false, render_debug debug = DEBUG_SHADED, bool direct_light = false);
        ~cuda_renderer();
        cuda_renderer(const cuda_renderer&) = delete;
        cuda_renderer& operator=(const cuda_renderer&) = delete;
        void reset(const flat_camera& camera);
        void add_samples(int count);
        void set_block_shape(int width, int height);
        std::vector<vec3> image() const;
        std::vector<unsigned char> rgba_image() const;
        void write_rgba(unsigned char *device_pixels, size_t bytes) const;
        int sample_count() const;
        float kernel_milliseconds() const;
        uint64_t rays_traced() const;
    private:
        struct implementation;
        std::unique_ptr<implementation> state;
};

// Deterministic ray queries for host-reference/device comparisons.
std::vector<trace_result> cuda_query_rays(const flat_scene& scene, const std::vector<ray>& rays, bool brute = false);

#endif
