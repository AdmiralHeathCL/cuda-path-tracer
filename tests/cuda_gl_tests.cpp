#include <iostream>
#include "cuda/cuda_gl_display.h"
#include "cuda/cuda_renderer.h"
#include "display/gl_display.h"
#include "display/display_pixels.h"
#include "flat/flat_export.h"

int main() {
    try {
        gl_display display(19,13,true);
        std::string reason;
        if(!cuda_device_available(reason) || !cuda_gl_display::available(reason)) {
            std::cout << "Interop runtime validation unavailable: " << reason << '\n'; return 77;
        }
        srand48(7);
        obj_scene source(RAY_TRACER_ASSET_DIR "/models/blender_sphere/sphere.obj",19.0f/13);
        auto scene = flatten_scene(&source);
        camera camera(source.lookfrom,source.lookat,vec3(0,1,0),40,19.0f/13,0,source.focus_dist,0,1);
        cuda_gl_display interop;
        for(auto size : {std::pair<int,int>{19,13},{23,17},{19,13}}) {
            glfwSetWindowSize(display.handle(),size.first,size.second);
            int width = 0, height = 0; double end = glfwGetTime()+2;
            do { glfwPollEvents(); glfwGetFramebufferSize(display.handle(),&width,&height); }
            while((width != size.first || height != size.second) && glfwGetTime() < end);
            if(width != size.first || height != size.second) throw std::runtime_error("interop test resize did not arrive");
            cuda_renderer renderer(scene,flatten_camera(camera),width,height,7,false,DEBUG_SHADED,true);
            for(int pass = 0; pass < 2; pass++) {
                renderer.add_samples(2); interop.draw(display,renderer,width,height);
                auto actual = display.read_back(width,height), expected = display_pixels(renderer.image());
                for(size_t i = 0; i < actual.size(); i++) {
                    if(std::abs(int(actual[i])-int(expected[i])) > 1) throw std::runtime_error("interop framebuffer differs from linear CUDA image");
                }
                glfwSwapBuffers(display.handle());
            }
        }
        std::cout << "CUDA/OpenGL mapping, repeated frames, resizing and pixel comparisons passed\n";
    } catch(const display_unavailable& error) { std::cerr << error.what() << '\n'; return 77;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
