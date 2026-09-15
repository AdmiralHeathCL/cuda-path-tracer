#include <iostream>
#include "app/render_options.h"
#include "core/camera.h"
#include "display/color.h"
#include "flat/flat_export.h"
#include "scene/obj_scene.h"
#include "cuda/cuda_renderer.h"

int main(int argc, char **argv) {
    try {
        auto options = parse_options(argc,argv);
        if(options.help) {
            std::cout << "Usage: ray_tracer [--obj FILE] [--width N] [--height N]\n"
                      << "  [--samples N] [--batch-samples N] [--seed N]\n"
                      << "  [--obj-scale X Y Z] [--obj-rotate X Y Z] [--obj-translate X Y Z]\n"
                      << "  [--debug shaded|normals|triangle-id|material-id|bvh|intersections] [--normals]\n"
                      << "  [--lighting classic|mis] (default: mis)\n"
                      << "Defaults: CUDA, Blender sphere, 500x500, 100 samples/pixel, seed 7.\n"
                      << "PPM output goes to stdout. OBJ transforms apply scale, XYZ rotation in degrees, then translation.\n";
            return 0;
        }
        std::string device;
        if(!cuda_device_available(device)) {
            std::cerr << "CUDA device unavailable: " << device << '\n';
            return 77;
        }
        srand48(options.seed);
        obj_scene source(options.obj_file,float(options.width)/options.height,
            mesh_transform(options.obj_scale,options.obj_rotate,options.obj_translate));
        auto scene = flatten_scene(&source);
        camera cam(source.lookfrom,source.lookat,vec3(0,1,0),40,float(options.width)/options.height,0,source.focus_dist,0,1);
        cuda_renderer renderer(scene,flatten_camera(cam),options.width,options.height,options.seed,false,options.debug,options.direct_light);
        std::cerr << "CUDA: " << device << "\nLoaded " << scene.triangles.size() << " triangles, " << scene.nodes.size()
                  << " BVH nodes\n" << source.model->warnings;
        while(renderer.sample_count() < options.samples) {
            renderer.add_samples(std::min(options.batch_samples,options.samples-renderer.sample_count()));
        }
        auto pixels = renderer.image();
        std::cerr << "Rendered " << options.width << "x" << options.height << ", " << renderer.sample_count() << " samples/pixel\n";
        std::cout << "P3\n" << options.width << " " << options.height << "\n255\n";
        for(int y = options.height-1; y >= 0; y--) {
            for(int x = 0; x < options.width; x++) {
                const auto& pixel = pixels[size_t(y)*options.width+x];
                std::cout << color_channel(pixel.x()) << " " << color_channel(pixel.y()) << " " << color_channel(pixel.z()) << '\n';
            }
        }
        if(!std::cout) throw std::runtime_error("could not write image output");
    } catch(const std::exception& error) {
        std::cerr << error.what() << "\nUse --help for usage.\n";
        return 1;
    }
}
