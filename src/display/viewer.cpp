#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "display/gl_display.h"
#include "display/viewer_input.h"
#include "display/viewer_options.h"
#include "display/preview_session.h"
#include "cuda/cuda_gl_display.h"

void save_screenshot(const std::string& filename, const std::vector<unsigned char>& pixels, int width, int height) {
    std::filesystem::path path(filename);
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << "P3\n" << width << " " << height << "\n255\n";
    for(int y = height-1; y >= 0; y--) {
        for(int x = 0; x < width; x++) {
            size_t i = 4*(size_t(y)*width+x);
            out << int(pixels[i]) << " " << int(pixels[i+1]) << " " << int(pixels[i+2]) << "\n";
        }
    }
    out.close();
}

void run_viewer(gl_display& display, const viewer_options& options) {
    GLFWwindow *window = display.handle();
    int width, height; glfwGetFramebufferSize(window,&width,&height);
    if(width <= 0 || height <= 0) throw std::runtime_error("initial framebuffer has zero dimensions");
    const auto& config = options.render;
    srand48(config.seed);
    obj_scene source(config.obj_file,float(width)/height,mesh_transform(config.obj_scale,config.obj_rotate,config.obj_translate));
    std::cerr << "Loaded " << source.model->mesh->triangle_count() << " triangles\n" << source.model->warnings;
    auto scene = flatten_scene(&source);
    camera_controller control(source.lookfrom,source.lookat,40,float(width)/height,0,source.focus_dist,0,1,source.focus_dist*0.2f,0.1f,true);
    preview_session<cuda_renderer> session(scene,control,width,height,config.seed,config.batch_samples,false,config.debug,config.direct_light);
    bool direct_display = false;
    std::unique_ptr<cuda_gl_display> interop;
    if(options.display != "copy") {
        std::string reason;
        if(cuda_gl_display::available(reason)) {
            interop = std::make_unique<cuda_gl_display>(); direct_display = true;
            std::cerr << "Display: CUDA/OpenGL shared pixel buffer\n";
        } else {
            if(options.display == "interop") throw std::runtime_error("CUDA/OpenGL interop unavailable: "+reason);
            std::cerr << "Display: CPU transfer; " << reason << '\n';
        }
    }
    viewer_input input;
    input.focus(glfwGetWindowAttrib(window,GLFW_FOCUSED) == GLFW_TRUE);
    glfwSetWindowUserPointer(window,&input);
    glfwSetKeyCallback(window,[](GLFWwindow *w, int key, int, int action, int) {
        static_cast<viewer_input*>(glfwGetWindowUserPointer(w))->key(key,action);
    });
    glfwSetCursorPosCallback(window,[](GLFWwindow *w, double x, double y) {
        static_cast<viewer_input*>(glfwGetWindowUserPointer(w))->cursor(x,y);
    });
    glfwSetMouseButtonCallback(window,[](GLFWwindow *w, int button, int action, int) {
        static_cast<viewer_input*>(glfwGetWindowUserPointer(w))->mouse_button(button,action);
    });
    glfwSetCursorEnterCallback(window,[](GLFWwindow *w, int) {
        static_cast<viewer_input*>(glfwGetWindowUserPointer(w))->reset_cursor();
    });
    glfwSetWindowFocusCallback(window,[](GLFWwindow *w, int focused) {
        static_cast<viewer_input*>(glfwGetWindowUserPointer(w))->focus(focused == GLFW_TRUE);
    });
    // Detach callbacks before their stack-owned input object is destroyed.
    struct callbacks {
        GLFWwindow *window;
        ~callbacks() {
            glfwSetKeyCallback(window,nullptr); glfwSetCursorPosCallback(window,nullptr);
            glfwSetMouseButtonCallback(window,nullptr);
            glfwSetCursorEnterCallback(window,nullptr);
            glfwSetWindowFocusCallback(window,nullptr); glfwSetWindowUserPointer(window,nullptr);
        }
    } detach{window};
    int rendered = 0, title_frames = 0;
    double previous = glfwGetTime(), title_start = previous;
    std::vector<unsigned char> screenshot;
    int screenshot_width = 0, screenshot_height = 0;
    std::cerr << "Controls: W/S dolly, A/D pan horizontally, Q/E pan vertically, Shift fast, hold right mouse to orbit current view, R reset, 1-6 debug modes, Esc release/close\n";
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if(input.should_close() || glfwWindowShouldClose(window)) break;
        // Normal cursor mode keeps drag coordinates free of recentering offsets.
        double now = glfwGetTime(), seconds = std::min(now-previous,0.1);
        previous = now;
        glfwGetFramebufferSize(window,&width,&height);
        bool minimized = glfwGetWindowAttrib(window,GLFW_ICONIFIED) == GLFW_TRUE;
        session.resize(minimized ? 0 : width,minimized ? 0 : height);
        // Bounded capture/test runs intentionally progress without window focus.
        session.set_focused(options.frames > 0 || input.has_focus());
        int mode = input.take_mode();
        if(mode >= 0) session.set_debug(render_debug(mode));
        if(session.paused()) {
            input.take();
            glfwWaitEventsTimeout(0.05);
            title_start = glfwGetTime(); title_frames = 0;
            continue;
        }
        session.render_frame(input.take(),seconds,!direct_display);
        if(interop) interop->draw(display,session.backend(),width,height);
        if(!direct_display) display.draw(session.rgba(),width,height);
        // Read the actual GL back buffer before swap, not the CPU image buffer.
        // Retain the last frame even if the user closes a bounded run early.
        if(!options.screenshot.empty()) {
            screenshot = display.read_back(width,height);
            screenshot_width = width; screenshot_height = height;
        }
        glfwSwapBuffers(window);
        rendered++; title_frames++;
        double elapsed = std::max(1e-9,glfwGetTime()-title_start);
        if(elapsed >= 0.25 || rendered == 1) {
            static const char *names[] = {"shaded","normals","triangle ID","material ID","BVH cost","intersection cost"};
            std::ostringstream title;
            title << std::fixed << std::setprecision(1) << "Ray Tracer | " << config.backend << " | " << names[session.debug_mode()]
                  << " | " << session.sample_count() << " spp | " << title_frames/elapsed << " FPS | " << 1000*elapsed/title_frames
                  << " ms/frame | render " << session.frame_milliseconds() << " ms";
            if(auto kernel = session.kernel_frame_milliseconds()) title << " | kernel " << *kernel << " ms";
            if(session.frame_milliseconds() > 0) title << " | " << session.rays_last_frame()/session.frame_milliseconds()/1000.0 << " Mrays/s (render)";
            title << " | " << scene.triangles.size() << " tris | " << scene.nodes.size() << " nodes | Hold RMB: rotate";
            glfwSetWindowTitle(window,title.str().c_str());
            title_start = glfwGetTime(); title_frames = 0;
        }
        if(options.frames && rendered >= options.frames) break;
    }
    if(!options.screenshot.empty()) {
        if(screenshot.empty()) throw std::runtime_error("no presented frame was available for screenshot");
        save_screenshot(options.screenshot,screenshot,screenshot_width,screenshot_height);
    }
    std::cout << "Rendered frames: " << rendered << ", samples/pixel: " << session.sample_count()
              << ", framebuffer: " << session.image_width() << "x" << session.image_height() << "\n";
}

int main(int argc, char **argv) {
    try {
        auto options = parse_viewer_options(argc,argv);
        if(options.render.help) {
            std::cout << "Usage: ray_tracer_viewer [--obj FILE]\n"
                      << "  [--width N] [--height N] [--batch-samples N] [--seed N]\n"
                      << "  [--debug shaded|normals|triangle-id|material-id|bvh|intersections] [--normals]\n"
                      << "  [--obj-scale X Y Z] [--obj-rotate X Y Z] [--obj-translate X Y Z]\n"
                      << "  [--frames N] [--hidden] [--screenshot FILE.ppm]\n"
                      << "  [--lighting classic|mis] (default: mis)\n"
                      << "  [--display auto|copy|interop] (default: auto)\n"
                      << "Defaults: CUDA, Blender sphere, 640x400 window, 1 sample/frame, seed 7.\n"
                      << "--hidden requires --frames. Screenshot saves the last GL framebuffer on exit.\n"
                      << "W/S dolly; A/D pan horizontally; Q/E pan vertically; Shift fast; hold right mouse to orbit current view; R reset; 1-6 modes; Esc release/close.\n";
            return 0;
        }
        std::string reason;
        if(!cuda_device_available(reason)) { std::cerr << "CUDA device unavailable: " << reason << "\n"; return 77; }
        gl_display display(options.render.width,options.render.height,options.hidden);
        run_viewer(display,options);
    } catch(const display_unavailable& error) {
        std::cerr << error.what() << "\n"; return 77;
    } catch(const std::exception& error) {
        std::cerr << error.what() << "\n"; return 1;
    }
}
