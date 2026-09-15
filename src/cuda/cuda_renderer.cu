#include <cuda_runtime.h>
#include <limits>
#include <sstream>
#include "cuda/cuda_renderer.h"
#include "renderer/render_validation.h"

namespace {
void checked(cudaError_t result, const char *operation) {
    if(result != cudaSuccess) throw std::runtime_error(std::string(operation)+": "+cudaGetErrorString(result));
}
#define CUDA_CHECK(call) checked((call),#call)

template<class T> class device_buffer {
    public:
        explicit device_buffer(size_t n = 0) : count(n) {
            if(n > std::numeric_limits<size_t>::max()/sizeof(T)) throw std::invalid_argument("device allocation size overflow");
            if(n) CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&ptr),n*sizeof(T)));
        }
        explicit device_buffer(const std::vector<T>& source) : device_buffer(source.size()) {
            if(count) CUDA_CHECK(cudaMemcpy(ptr,source.data(),count*sizeof(T),cudaMemcpyHostToDevice));
        }
        ~device_buffer() { if(ptr) cudaFree(ptr); }
        device_buffer(const device_buffer&) = delete;
        device_buffer& operator=(const device_buffer&) = delete;
        T *get() const { return ptr; }
        std::vector<T> download() const {
            std::vector<T> result(count);
            if(count) CUDA_CHECK(cudaMemcpy(result.data(),ptr,count*sizeof(T),cudaMemcpyDeviceToHost));
            return result;
        }
    private:
        T *ptr = nullptr;
        size_t count;
};

class device_scene {
    public:
        explicit device_scene(const flat_scene& s) : triangles(s.triangles), spheres(s.spheres), nodes(s.nodes),
            materials(s.materials), textures(s.textures), pixels(s.pixels), host_lights(scene_lights(s)), lights(host_lights) {
            view = {triangles.get(),spheres.get(),nodes.get(),materials.get(),textures.get(),pixels.get(),s.root,int32_t(s.nodes.size())};
            view.lights = lights.get(); view.light_count = int32_t(host_lights.size());
            view.light_area = host_lights.empty() ? 0 : host_lights.back().cumulative_area;
        }
        scene_view view;
    private:
        device_buffer<flat_triangle> triangles;
        device_buffer<flat_sphere> spheres;
        device_buffer<flat_node> nodes;
        device_buffer<flat_material> materials;
        device_buffer<flat_texture> textures;
        device_buffer<unsigned char> pixels;
        std::vector<flat_light> host_lights;
        device_buffer<flat_light> lights;
};

struct event {
    cudaEvent_t value;
    event() { CUDA_CHECK(cudaEventCreate(&value)); }
    ~event() { cudaEventDestroy(value); }
    event(const event&) = delete;
    event& operator=(const event&) = delete;
};

__global__ void initialize_pixels(vec3 *sum, pixel_rng *random, size_t count, uint64_t seed) {
    size_t i = size_t(blockIdx.x)*blockDim.x+threadIdx.x;
    if(i >= count) return;
    sum[i] = vec3(0,0,0); random[i].seed(seed,i);
}

__global__ void render_pixels(scene_view scene, flat_camera camera, int width, int height, int samples,
                              vec3 *sum, pixel_rng *random, bool brute, render_debug debug, int *failure,
                              bool lighting, uint64_t *ray_counts) {
    int x = int(blockIdx.x*blockDim.x+threadIdx.x);
    int y = int(blockIdx.y*blockDim.y+threadIdx.y);
    if(x >= width || y >= height) return;
    size_t i = size_t(y)*width+x;
    pixel_rng rng = random[i];
    vec3 accumulated = sum[i];
    int error = 0;
    uint64_t rays = 0;
    for(int s = 0; s < samples; s++) accumulated += trace_pixel_sample(scene,camera,x,y,width,height,rng,brute,debug,error,lighting,&rays);
    if(error) atomicExch(failure,error);
    sum[i] = accumulated; random[i] = rng;
    ray_counts[i] = rays;
}

__global__ void reduce_rays(const uint64_t *counts, size_t size, unsigned long long *total) {
    __shared__ unsigned long long local[256];
    size_t i = size_t(blockIdx.x)*blockDim.x+threadIdx.x;
    local[threadIdx.x] = i < size ? counts[i] : 0;
    __syncthreads();
    for(int stride = 128; stride; stride /= 2) {
        if(threadIdx.x < stride) local[threadIdx.x] += local[threadIdx.x+stride];
        __syncthreads();
    }
    if(threadIdx.x == 0) atomicAdd(total,local[0]);
}

__global__ void pack_pixels(const vec3 *sum, unsigned char *rgba, size_t count, int samples) {
    size_t i = size_t(blockIdx.x)*blockDim.x+threadIdx.x;
    if(i >= count) return;
    vec3 color = samples ? sum[i]/float(samples) : vec3(0,0,0);
    for(int channel = 0; channel < 3; channel++) {
        float value = color[channel];
        float gamma = value > 0 && value < 1 ? sqrtf(value) : 0;
        rgba[4*i+channel] = !(value > 0) ? 0 : value >= 1 ? 255 : int(255.99*gamma);
    }
    rgba[4*i+3] = 255;
}

__global__ void query_rays(scene_view scene, const ray *rays, trace_result *results, size_t count, bool brute) {
    size_t i = size_t(blockIdx.x)*blockDim.x+threadIdx.x;
    if(i >= count) return;
    trace_result result{};
    trace_stats stats;
    int error = 0;
    result.hit = trace_world(scene,rays[i],0.001f,FLT_MAX,result.rec,brute,error,stats);
    result.error = error;
    results[i] = result;
}

void require_device() {
    std::string description;
    if(!cuda_device_available(description)) throw std::runtime_error("CUDA device unavailable: "+description);
}
} // namespace

bool cuda_device_available(std::string& description) {
    int count = 0;
    cudaError_t status = cudaGetDeviceCount(&count);
    if(status != cudaSuccess) { description = cudaGetErrorString(status); return false; }
    if(count < 1) { description = "no CUDA devices"; return false; }
    int current = 0;
    status = cudaGetDevice(&current);
    cudaDeviceProp properties{};
    if(status == cudaSuccess) status = cudaGetDeviceProperties(&properties,current);
    if(status != cudaSuccess) { description = cudaGetErrorString(status); return false; }
    description = properties.name;
    return true;
}

struct cuda_renderer::implementation {
    device_scene scene;
    flat_camera camera;
    int width, height, samples = 0;
    uint64_t seed;
    bool brute;
    render_debug debug;
    device_buffer<vec3> sum;
    device_buffer<pixel_rng> random;
    device_buffer<int> failure;
    device_buffer<uint64_t> ray_counts;
    device_buffer<unsigned long long> ray_total;
    device_buffer<unsigned char> rgba;
    uint64_t rays = 0;
    bool lighting;
    event start, stop;
    float milliseconds = 0;
    dim3 blocks;
    dim3 threads = dim3(16,16);

    implementation(const flat_scene& s, const flat_camera& c, int w, int h, uint64_t seed_value, bool brute_value, render_debug mode, bool direct_light) :
        scene(s), camera(c), width(w), height(h), seed(seed_value), brute(brute_value), debug(mode),
        sum(checked_pixel_count(w,h)), random(checked_pixel_count(w,h)), failure(1),
        ray_counts(checked_pixel_count(w,h)), ray_total(1), rgba(4*checked_pixel_count(w,h)), lighting(direct_light),
        blocks((unsigned(w)+15)/16,(unsigned(h)+15)/16) {
        int current;
        CUDA_CHECK(cudaGetDevice(&current));
        cudaDeviceProp properties{};
        CUDA_CHECK(cudaGetDeviceProperties(&properties,current));
        if(blocks.x > unsigned(properties.maxGridSize[0]) || blocks.y > unsigned(properties.maxGridSize[1])) {
            throw std::invalid_argument("image exceeds CUDA grid limits");
        }
    }
};

cuda_renderer::cuda_renderer(const flat_scene& scene, const flat_camera& camera, int width, int height,
                             uint64_t seed, bool brute, render_debug debug, bool direct_light) {
    validate_flat_scene(scene); validate_render_camera(camera); validate_render_mode(debug); checked_pixel_count(width,height);
    require_device();
    state = std::make_unique<implementation>(scene,camera,width,height,seed,brute,debug,direct_light);
    reset(camera);
}
cuda_renderer::~cuda_renderer() = default;

void cuda_renderer::reset(const flat_camera& camera) {
    validate_render_camera(camera);
    size_t count = checked_pixel_count(state->width,state->height);
    CUDA_CHECK(cudaMemset(state->failure.get(),0,sizeof(int)));
    initialize_pixels<<<unsigned((count+255)/256),256>>>(state->sum.get(),state->random.get(),count,state->seed);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    state->camera = camera; state->samples = 0; state->milliseconds = 0; state->rays = 0;
}

void cuda_renderer::add_samples(int count) {
    if(count <= 0 || count > INT32_MAX-state->samples) throw std::invalid_argument("invalid accumulated sample count");
    CUDA_CHECK(cudaEventRecord(state->start.value));
    render_pixels<<<state->blocks,state->threads>>>(state->scene.view,state->camera,state->width,state->height,count,
        state->sum.get(),state->random.get(),state->brute,state->debug,state->failure.get(),state->lighting,state->ray_counts.get());
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaEventRecord(state->stop.value));
    CUDA_CHECK(cudaEventSynchronize(state->stop.value));
    if(state->failure.download()[0]) throw std::runtime_error("CUDA traversal stack overflow");
    float elapsed;
    CUDA_CHECK(cudaEventElapsedTime(&elapsed,state->start.value,state->stop.value));
    state->milliseconds += elapsed; state->samples += count;
    CUDA_CHECK(cudaMemset(state->ray_total.get(),0,sizeof(unsigned long long)));
    size_t pixels = size_t(state->width)*state->height;
    reduce_rays<<<unsigned((pixels+255)/256),256>>>(state->ray_counts.get(),pixels,state->ray_total.get());
    CUDA_CHECK(cudaGetLastError());
    state->rays += state->ray_total.download()[0];
}

void cuda_renderer::set_block_shape(int width, int height) {
    if(!((width == 8 && height == 8) || (width == 16 && height == 8) ||
         (width == 16 && height == 16) || (width == 32 && height == 8))) {
        throw std::invalid_argument("CUDA block must be 8x8, 16x8, 16x16 or 32x8");
    }
    if(state->threads.x == unsigned(width) && state->threads.y == unsigned(height)) return;
    dim3 blocks((unsigned(state->width)+width-1)/width,(unsigned(state->height)+height-1)/height);
    int device; CUDA_CHECK(cudaGetDevice(&device));
    cudaDeviceProp properties{}; CUDA_CHECK(cudaGetDeviceProperties(&properties,device));
    if(blocks.x > unsigned(properties.maxGridSize[0]) || blocks.y > unsigned(properties.maxGridSize[1])) {
        throw std::invalid_argument("image exceeds CUDA grid limits");
    }
    state->threads = dim3(width,height); state->blocks = blocks;
}

std::vector<vec3> cuda_renderer::image() const {
    auto result = state->sum.download();
    if(state->samples) for(auto& value : result) value /= float(state->samples);
    return result;
}
void cuda_renderer::write_rgba(unsigned char *device_pixels, size_t bytes) const {
    size_t count = size_t(state->width)*state->height;
    if(!device_pixels || bytes < 4*count) throw std::invalid_argument("CUDA RGBA destination is too small");
    pack_pixels<<<unsigned((count+255)/256),256>>>(state->sum.get(),device_pixels,count,state->samples);
    CUDA_CHECK(cudaGetLastError());
}
std::vector<unsigned char> cuda_renderer::rgba_image() const {
    write_rgba(state->rgba.get(),4*size_t(state->width)*state->height);
    return state->rgba.download();
}
int cuda_renderer::sample_count() const { return state->samples; }
float cuda_renderer::kernel_milliseconds() const { return state->milliseconds; }
uint64_t cuda_renderer::rays_traced() const { return state->rays; }
std::vector<trace_result> cuda_query_rays(const flat_scene& scene, const std::vector<ray>& rays, bool brute) {
    validate_flat_scene(scene);
    require_device();
    if(rays.empty()) return {};
    if(rays.size() > size_t(INT32_MAX)) throw std::invalid_argument("too many validation rays");
    device_scene uploaded(scene);
    device_buffer<ray> input(rays);
    device_buffer<trace_result> output(rays.size());
    query_rays<<<unsigned((rays.size()+255)/256),256>>>(uploaded.view,input.get(),output.get(),rays.size(),brute);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    return output.download();
}
