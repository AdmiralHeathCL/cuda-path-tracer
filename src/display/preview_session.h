#ifndef PREVIEWSESSIONH
#define PREVIEWSESSIONH

#include <chrono>
#include <memory>
#include <optional>
#include "display/camera_controller.h"
#include "display/display_pixels.h"
#include "flat/flat_export.h"
#include "renderer/render_validation.h"
#include "cuda/cuda_renderer.h"

template<class Renderer> inline std::optional<double> preview_kernel_time(const Renderer&) { return std::nullopt; }
inline std::optional<double> preview_kernel_time(const cuda_renderer& renderer) { return renderer.kernel_milliseconds(); }
template<class Renderer> inline std::vector<unsigned char> preview_pixels(const Renderer& renderer) { return display_pixels(renderer.image()); }
inline std::vector<unsigned char> preview_pixels(const cuda_renderer& renderer) { return renderer.rgba_image(); }

// Own accumulation and reset it when camera, framebuffer or debug mode changes.
template<class Renderer> class preview_session {
    public:
        preview_session(const flat_scene& source, const camera_controller& camera, int w, int h,
                        uint64_t seed = 7, int batch = 1, bool brute = false, render_debug debug = DEBUG_SHADED, bool direct_light = false) :
            scene(source), control(camera), width(w), height(h), seed_value(seed), samples_per_frame(batch),
            brute_force(brute), mode(debug), lighting(direct_light) {
            checked_pixel_count(w,h); validate_render_mode(mode); set_batch_samples(batch);
            control.resize(float(w)/h);
            renderer = make_renderer(control,w,h,mode);
            pixels = black_pixels(w,h);
        }

        void set_batch_samples(int count) {
            if(count <= 0) throw std::invalid_argument("frame sample count must be positive");
            samples_per_frame = count;
        }
        void set_focused(bool value) {
            if(value != focused) {
                focused = value; discard_input = true; milliseconds = 0;
            }
        }
        bool resize(int w, int h) {
            if(w < 0 || h < 0) throw std::invalid_argument("framebuffer dimensions must be nonnegative");
            if(w == 0 || h == 0) {
                minimized = true; discard_input = true; milliseconds = 0;
                return false;
            }
            checked_pixel_count(w,h);
            if(w == width && h == height) {
                minimized = false;
                return false;
            }
            camera_controller next = control;
            next.resize(float(w)/h);
            auto next_renderer = make_renderer(next,w,h,mode);
            auto next_pixels = black_pixels(w,h);
            control = next; renderer = std::move(next_renderer); pixels = std::move(next_pixels);
            width = w; height = h; minimized = false; discard_input = true; milliseconds = 0;
            return true;
        }
        bool set_debug(render_debug debug) {
            validate_render_mode(debug);
            if(debug == mode) return false;
            auto next_renderer = make_renderer(control,width,height,debug);
            auto next_pixels = black_pixels(width,height);
            renderer = std::move(next_renderer); pixels = std::move(next_pixels); mode = debug; milliseconds = 0;
            return true;
        }

        bool render_frame(camera_input input, double seconds, bool download = true) {
            frame_rays = 0;
            kernel_milliseconds.reset();
            if(!std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("frame time must be finite and nonnegative");
            if(!focused || minimized) { milliseconds = 0; return false; }
            if(discard_input) { input = {}; seconds = 0; }
            auto start = std::chrono::steady_clock::now();
            camera_controller next = control;
            if(next.update(input,seconds)) {
                renderer->reset(flatten_camera(next.frame()));
            }
            control = next;
            int count = std::min(samples_per_frame,INT32_MAX-renderer->sample_count());
            if(count == 0) { discard_input = false; milliseconds = 0; return false; }
            auto kernel_before = preview_kernel_time(*renderer);
            uint64_t before_rays = renderer->rays_traced();
            renderer->add_samples(count);
            frame_rays = renderer->rays_traced()-before_rays;
            if(kernel_before) kernel_milliseconds = *preview_kernel_time(*renderer)-*kernel_before;
            if(download) pixels = preview_pixels(*renderer);
            discard_input = false;
            milliseconds = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            return true;
        }

        int sample_count() const { return renderer->sample_count(); }
        Renderer& backend() { return *renderer; }
        int image_width() const { return width; }
        int image_height() const { return height; }
        bool paused() const { return !focused || minimized; }
        render_debug debug_mode() const { return mode; }
        const camera& camera_frame() const { return control.frame(); }
        const std::vector<unsigned char>& rgba() const { return pixels; }
        // Host update/reset + render + readback + RGBA conversion. No GL work.
        double frame_milliseconds() const { return milliseconds; }
        uint64_t rays_last_frame() const { return frame_rays; }
        std::optional<double> kernel_frame_milliseconds() const { return kernel_milliseconds; }

    private:
        std::unique_ptr<Renderer> make_renderer(const camera_controller& camera, int w, int h, render_debug debug) const {
            return std::make_unique<Renderer>(scene,flatten_camera(camera.frame()),w,h,seed_value,brute_force,debug,lighting);
        }
        static std::vector<unsigned char> black_pixels(int w, int h) {
            size_t count = checked_pixel_count(w,h);
            if(count > std::numeric_limits<size_t>::max()/4) throw std::length_error("display image exceeds range");
            std::vector<unsigned char> result(count*4,0);
            for(size_t i = 0; i < count; i++) result[4*i+3] = 255;
            return result;
        }

        flat_scene scene;
        camera_controller control;
        int width, height;
        uint64_t seed_value;
        int samples_per_frame;
        bool brute_force;
        render_debug mode;
        bool lighting;
        uint64_t frame_rays = 0;
        bool focused = true, minimized = false, discard_input = false;
        double milliseconds = 0;
        std::optional<double> kernel_milliseconds;
        std::unique_ptr<Renderer> renderer;
        std::vector<unsigned char> pixels;
};

#endif
