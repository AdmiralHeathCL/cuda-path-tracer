#include <iostream>
#include "flat/flat_export.h"
#include "support/portable_renderer.h"
#include "display/display_pixels.h"
#ifdef RAY_TRACER_HAS_CUDA
#include "cuda/cuda_renderer.h"
#endif

int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    checks++;
    if(!ok) { failures++; std::cerr << "FAIL: " << message << '\n'; }
}

flat_scene fixture(bool occluded = false) {
    constant_texture white(vec3(0.7,0.7,0.7)), emission(vec3(10,10,10)), black(vec3(0,0,0));
    lambertian diffuse(&white), blocker(&black); diffuse_light light(&emission);
    std::vector<triangle> faces;
    faces.emplace_back(vec3(-10,-10,0),vec3(10,-10,0),vec3(0,10,0),&diffuse);
    faces.emplace_back(vec3(-0.2,-0.2,2),vec3(0.2,-0.2,2),vec3(0,0.2,2),&light);
    if(occluded) faces.emplace_back(vec3(-10,-10,1),vec3(10,-10,1),vec3(0,10,1),&blocker);
    triangle_mesh mesh(std::move(faces));
    return flatten_scene(&mesh);
}

double quadrature() {
    // Independent area integral for the parallel triangle at z=2, viewed by
    // a Lambertian point at the origin. No ray tracer or PDFs used here.
    double sum = 0;
    const int n = 256;
    for(int i = 0; i < n; i++) for(int j = 0; j < n; j++) {
        double root = sqrt((i+0.5)/n), v = (j+0.5)/n;
        double a = 1-root, b = root*(1-v), c = root*v;
        double x = -0.2*a+0.2*b, y = -0.2*a-0.2*b+0.2*c;
        double squared = x*x+y*y+4;
        sum += 0.7*10/M_PI*4/(squared*squared)*0.08;
    }
    return sum/(n*n);
}

int main() {
    srand48(7);
    auto scene = fixture(); auto lights = scene_lights(scene);
    auto view = host_view(scene,lights), bsdf = host_view(scene);
    check(lights.size() == 1 && fabs(view.light_area-0.08) < 1e-6,"emitter table has correct triangle area");
    check(fabs(mis_weight(2,3)+mis_weight(3,2)-1) < 1e-6,"MIS weights sum to one");
    double expected = quadrature(), error_mis = 0, error_bsdf = 0, mean = 0;
    for(int seed = 0; seed < 64; seed++) {
        pixel_rng a,b; a.seed(seed,0); b.seed(seed,0);
        double estimate = 0, reference = 0; int error = 0; uint64_t rays = 0;
        for(int sample = 0; sample < 128; sample++) {
            ray primary(vec3(0,0,0.1),vec3(0,0,-1));
            estimate += trace_color(primary,view,a,false,DEBUG_SHADED,error,true,&rays).x();
            reference += trace_color(primary,bsdf,b,false,DEBUG_SHADED,error,true).x();
        }
        estimate /= 128; reference /= 128; mean += estimate;
        error_mis += (estimate-expected)*(estimate-expected);
        error_bsdf += (reference-expected)*(reference-expected);
        check(error == 0 && rays >= 256,"MIS counts primary, bounce and shadow queries");
    }
    mean /= 64;
    check(fabs(mean-expected)/expected < 0.02,"MIS mean agrees with independent area quadrature");
    check(error_mis < 0.1*error_bsdf,"MIS reduces error versus cosine-only sampling at equal samples");
    std::cout << "Area-integral reference: " << expected << "; MIS mean: " << mean
              << "; MIS MSE: " << error_mis/64 << "; cosine-only MSE: " << error_bsdf/64 << '\n';
    auto blocked = fixture(true); auto blocked_lights = scene_lights(blocked);
    pixel_rng rng; rng.seed(7,0); int error = 0;
    vec3 shadow(0,0,0);
    for(int i = 0; i < 256; i++) shadow += trace_color(ray(vec3(0,0,0.1),vec3(0,0,-1)),host_view(blocked,blocked_lights),rng,false,DEBUG_SHADED,error,true);
    check(shadow.squared_length() == 0 && error == 0,"opaque black blocker prevents direct-light leaks");

    obj_scene model(RAY_TRACER_ASSET_DIR "/models/blender_sphere/sphere.obj",19.0f/13);
    auto imported = flatten_scene(&model);
    camera camera(model.lookfrom,model.lookat,vec3(0,1,0),40,19.0f/13,0,model.focus_dist,0,1);
    auto frame = flatten_camera(camera);
    portable_renderer cpu(imported,frame,19,13,7,false,DEBUG_SHADED,true);
    portable_renderer split(imported,frame,19,13,7,false,DEBUG_SHADED,true);
    cpu.add_samples(8); split.add_samples(3); split.add_samples(5);
    auto image = cpu.image(), other = split.image();
    check(cpu.rays_traced() == split.rays_traced(),"ray count is independent of progressive batching");
    for(size_t i = 0; i < image.size(); i++) check((image[i]-other[i]).squared_length() == 0,"MIS progressive batches reproduce same linear pixels");
    split.reset(frame); check(split.rays_traced() == 0,"reset clears ray counters");
#ifdef RAY_TRACER_HAS_CUDA
    std::string reason;
    if(!cuda_device_available(reason)) { std::cerr << reason << '\n'; return failures ? 1 : 77; }
    cuda_renderer gpu(imported,frame,19,13,7,false,DEBUG_SHADED,true);
    gpu.set_block_shape(8,8); gpu.add_samples(3); gpu.add_samples(5);
    other = gpu.image(); double difference = 0, norm = 0;
    for(size_t i = 0; i < image.size(); i++) { difference += (image[i]-other[i]).squared_length(); norm += image[i].squared_length(); }
    std::cout << "MIS CPU/GPU relative L2: " << sqrt(difference/std::max(1e-20,norm)) << '\n';
    check(sqrt(difference/std::max(1e-20,norm)) < 1e-4,"GPU MIS matches portable linear image");
    check(gpu.rays_traced() == cpu.rays_traced(),"GPU counts actual rays including shadow queries");
    auto packed = gpu.rgba_image(), expected_rgba = display_pixels(other);
    for(size_t i = 0; i < packed.size(); i++) check(std::abs(int(packed[i])-int(expected_rgba[i])) <= 1,"device gamma/RGBA conversion matches CPU packing");
    gpu.reset(frame); check(gpu.rays_traced() == 0,"GPU reset clears ray counters");
    for(bool occluded : {false,true}) {
        auto triangles = fixture(occluded);
        auto triangle_frame = flatten_camera(::camera(vec3(0,0,0.1),vec3(0,0,0),vec3(0,1,0),40,19.0f/13,0,0.1,0,1));
        portable_renderer reference(triangles,triangle_frame,19,13,11,false,DEBUG_SHADED,true);
        reference.add_samples(16);
        auto expected = reference.image();
        {
            cuda_renderer device(triangles,triangle_frame,19,13,11,false,DEBUG_SHADED,true);
            device.add_samples(16);
            auto actual = device.image();
            double squared_error = 0, energy = 0;
            for(size_t i = 0; i < actual.size(); i++) {
                squared_error += (actual[i]-expected[i]).squared_length();
                energy += expected[i].squared_length();
            }
            check(sqrt(squared_error/std::max(1e-20,energy)) < 1e-4,"triangle MIS and occlusion agree on CPU/GPU");
            check(device.rays_traced() == reference.rays_traced(),"triangle light sampling preserves ray count");
        }
    }
#endif
    std::cout << checks << " lighting checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
