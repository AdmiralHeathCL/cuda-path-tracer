#include <iostream>
#include <random>
#include "cuda/cuda_renderer.h"
#include "flat/flat_export.h"
#include "support/flat_cpu.h"
#include "support/portable_renderer.h"

int failures = 0, checks = 0;
void check(bool ok, const char *message) {
    checks++;
    if(!ok) { failures++; if(failures < 20) std::cerr << "FAIL: " << message << "\n"; }
}
bool close(float a, float b) { return fabs(a-b) <= 0.0002f*fmaxf(1,fmaxf(fabsf(a),fabsf(b))); }
bool close(vec3 a, vec3 b) { return close(a.x(),b.x()) && close(a.y(),b.y()) && close(a.z(),b.z()); }

void validate_image(const std::vector<vec3>& expected, const std::vector<vec3>& actual) {
    check(expected.size() == actual.size(),"device image size");
    double square_error = 0, energy = 0;
    for(size_t i = 0; i < expected.size() && i < actual.size(); i++) {
        check(std::isfinite(actual[i].length()) && actual[i].x() >= 0 && actual[i].y() >= 0 && actual[i].z() >= 0,"device finite nonnegative image");
        square_error += (expected[i]-actual[i]).squared_length();
        energy += expected[i].squared_length();
    }
    // Path decisions can diverge at grazing hits even with identical RNG streams.
    double relative = sqrt(square_error/fmax(1e-12,energy));
    std::cout << "Relative image L2 error: " << relative << "\n";
    check(relative < 0.05,"device/portable image error below 5 percent");
}

int main(int argc, char **argv) {
    std::string device;
    if(!cuda_device_available(device)) { std::cout << "SKIP: " << device << "\n"; return 77; }
    std::cout << "Device: " << device << "\n";
    try {
        std::vector<std::string> assets;
        if(argc > 1) assets.push_back(argv[1]);
        else for(const char *p : {"/models/cube/cube.obj","/models/textured_triangle/triangle.obj","/models/blender_sphere/sphere.obj"}) assets.push_back(std::string(RAY_TRACER_ASSET_DIR)+p);
        for(const auto& path : assets) {
            auto source = std::make_unique<obj_scene>(path,1.5);
            auto flat = flatten_scene(source.get());
            camera c(source->lookfrom,source->lookat,vec3(0,1,0),40,1.5,0,source->focus_dist,0,1);
            auto fc = flatten_camera(c);
            std::vector<ray> rays;
            std::mt19937 rng(7);
            for(int i = 0; i < 2048; i++) {
                float u = float(double(rng())/4294967296.0), v = float(double(rng())/4294967296.0);
                rays.push_back(c.get_ray(u,v));
            }
            auto accelerated = cuda_query_rays(flat,rays,false), linear = cuda_query_rays(flat,rays,true);
            for(size_t i = 0; i < rays.size(); i++) {
                flat_hit expected;
                bool hit = flat_hit_world(flat,rays[i],0.001f,FLT_MAX,expected);
                for(auto actual : {accelerated[i],linear[i]}) {
                    check(!actual.error && bool(actual.hit) == hit,"device/reference hit agreement");
                    if(hit && actual.hit) {
                        check(close(expected.t,actual.rec.t) && close(expected.p,actual.rec.p) && close(expected.normal,actual.rec.normal),"device closest hit position/normal");
                        check(close(expected.u,actual.rec.u) && close(expected.v,actual.rec.v) && expected.material_id == actual.rec.material_id,"device UV/material agreement");
                    }
                }
            }
            for(auto mode : {DEBUG_SHADED,DEBUG_NORMALS,DEBUG_TRIANGLE,DEBUG_MATERIAL,DEBUG_BVH,DEBUG_INTERSECTIONS}) {
                portable_renderer host(flat,fc,19,13,7,false,mode); host.add_samples(16);
                cuda_renderer gpu(flat,fc,19,13,7,false,mode);
                gpu.add_samples(5); gpu.add_samples(11);
                auto batched = gpu.image();
                validate_image(host.image(),batched);
                gpu.reset(fc); check(gpu.sample_count() == 0,"device reset sample count");
                for(auto p : gpu.image()) check(p.squared_length() == 0,"device reset pixels");
                gpu.add_samples(16);
                auto whole = gpu.image();
                for(size_t i = 0; i < whole.size(); i++) check(close(whole[i],batched[i]),"device progressive batching parity");
                if(mode <= DEBUG_MATERIAL) {
                    cuda_renderer brute(flat,fc,19,13,7,true,mode); brute.add_samples(16);
                    validate_image(whole,brute.image());
                }
                fc.origin += vec3(0.1,0,0);
                gpu.reset(fc); gpu.add_samples(3);
                portable_renderer moved(flat,fc,19,13,7,false,mode); moved.add_samples(3);
                validate_image(moved.image(),gpu.image());
                fc = flatten_camera(c);
            }
            // Upload ownership is independent of the source pointer graph and vectors.
            cuda_renderer independent(flat,fc,7,5,1);
            source.reset(); flat = flat_scene(); independent.add_samples(2);
            check(independent.image().size() == 35,"device scene owns uploaded payloads");
        }
        constant_texture paint(vec3(0.3,0.7,0.2)), lamp_color(vec3(4,3,2));
        lambertian diffuse(&paint); metal mirror(vec3(0.8,0.7,0.6),0.1); dielectric glass(1.5); diffuse_light lamp(&lamp_color);
        sphere a(vec3(-2,0,0),1,&diffuse), b(vec3(0,0,0),1,&glass), c(vec3(2,0,0),1,&mirror);
        sphere ground(vec3(0,-101,0),100,&diffuse), light(vec3(0,4,2),2,&lamp);
        hitable *objects[] = {&a,&b,&c,&ground,&light};
        bvh_node world(objects,5,0,1);
        auto flat = flatten_scene(&world);
        camera view(vec3(0,2,8),vec3(0,0,0),vec3(0,1,0),40,1.5,0.2,8,0.2,0.8);
        auto fc = flatten_camera(view);
        portable_renderer host(flat,fc,31,21,19); host.add_samples(64);
        cuda_renderer gpu(flat,fc,31,21,19); gpu.add_samples(16); gpu.add_samples(48);
        validate_image(host.image(),gpu.image());
        cuda_renderer brute(flat,fc,31,21,19,true); brute.add_samples(64);
        validate_image(gpu.image(),brute.image());
    } catch(const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
