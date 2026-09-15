#include <iostream>
#include <random>
#include "flat/flat_export.h"
#include "support/flat_cpu.h"
#include "support/portable_renderer.h"

int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    checks++;
    if(!ok) { failures++; if(failures < 20) std::cerr << "FAIL: " << message << "\n"; }
}
bool close(float a, float b) { return fabs(a-b) < 0.0001f*fmaxf(1,fmaxf(fabsf(a),fabsf(b))); }
bool close(vec3 a, vec3 b) { return close(a.x(),b.x()) && close(a.y(),b.y()) && close(a.z(),b.z()); }

void compare(const flat_scene& scene, const ray& r) {
    flat_hit ref, accelerated, linear;
    int error = 0; trace_stats a_stats, b_stats;
    bool a = flat_hit_world(scene,r,0.001f,FLT_MAX,ref);
    bool b = trace_world(host_view(scene),r,0.001f,FLT_MAX,accelerated,false,error,a_stats);
    bool c = trace_world(host_view(scene),r,0.001f,FLT_MAX,linear,true,error,b_stats);
    check(error == 0 && a == b && b == c,"reference/portable BVH/brute hit agreement");
    if(a && b && c) {
        for(auto hit : {accelerated,linear}) {
            check(close(ref.t,hit.t) && close(ref.p,hit.p) && close(ref.normal,hit.normal),"shared geometry hit records");
            check(close(ref.u,hit.u) && close(ref.v,hit.v) && ref.material_id == hit.material_id &&
                  ref.primitive_type == hit.primitive_type && ref.primitive_id == hit.primitive_id,"shared UV/material/primitive IDs");
        }
    }
}

void test_assets() {
    for(const char *path : {"/models/cube/cube.obj","/models/textured_triangle/triangle.obj","/models/blender_sphere/sphere.obj"}) {
        auto model = load_obj(std::string(RAY_TRACER_ASSET_DIR)+path,
            mesh_transform(vec3(-1.5,0.75,1),vec3(20,35,10),vec3(2,0,-1)));
        auto scene = flatten_scene(model.get());
        aabb bounds; model->bounding_box(0,1,bounds);
        vec3 center = 0.5*bounds.min()+0.5*bounds.max();
        float radius = (bounds.max()-bounds.min()).length();
        std::mt19937 random(52);
        auto u = [&]() { return float(double(random())/4294967296.0); };
        for(int i = 0; i < 1024; i++) {
            vec3 origin, direction;
            for(int axis = 0; axis < 3; axis++) { origin[axis] = center[axis]+(u()-0.5f)*2*radius; direction[axis] = u()-0.5f; }
            if(i%2) direction = center-origin;
            else { direction = vec3(0,0,0); direction[i%3] = (i%4 ? 1 : -1); }
            compare(scene,ray(origin,direction,0.42));
        }
        for(size_t i = 0; i < scene.textures.size(); i++) {
            for(float x : {-1.2f,0.0f,0.31f,1.0f,2.1f}) {
                check(close(flat_texture_value(scene,int(i),x,0.73),trace_texture(host_view(scene),int(i),x,0.73)),"shared texture lookup matches flat reference");
            }
        }
    }
    flat_node box{vec3(-1,-1,-1),vec3(1,1,1),FLAT_BRANCH,-1,-1,-1};
    check(trace_box(box,ray(vec3(1,0,2),vec3(0,0,-1)),0,10),"parallel ray on slab face accepted");
    check(!trace_box(box,ray(vec3(2,0,2),vec3(0,0,-1)),0,10),"parallel ray outside slab rejected");
    check(trace_box(box,ray(vec3(0,0,0),vec3(-0.0f,1,0)),0,10),"inside ray and signed-zero direction");
}

void test_pruning() {
    // Explicit tree order makes the exact-distance winner deterministic.
    flat_scene scene;
    scene.spheres = {{vec3(0,0,0),1,0},{vec3(0,0,0),1,1},{vec3(0,0,-8),1,0}};
    scene.nodes = {
        {vec3(-1,-1,-9),vec3(1,1,1),FLAT_BRANCH,1,4,-1},
        {vec3(-1,-1,-1),vec3(1,1,1),FLAT_BRANCH,2,3,-1},
        {vec3(-1,-1,-1),vec3(1,1,1),FLAT_SPHERE,-1,-1,0},
        {vec3(-1,-1,-1),vec3(1,1,1),FLAT_SPHERE,-1,-1,1},
        {vec3(-1,-1,-9),vec3(1,1,-7),FLAT_BRANCH,5,5,-1},
        {vec3(-1,-1,-9),vec3(1,1,-7),FLAT_SPHERE,-1,-1,2}};
    scene.root = 0;
    flat_hit a,b; trace_stats pruned,brute; int error = 0;
    ray r(vec3(0,0,3),vec3(0,0,-1));
    bool hit = trace_world(host_view(scene),r,0.001f,FLT_MAX,a,false,error,pruned);
    bool reference = trace_world(host_view(scene),r,0.001f,FLT_MAX,b,true,error,brute);
    check(hit && reference && !error && a.t == b.t && a.material_id == 1 && b.material_id == 1,"pruning preserves later exact-distance hit");
    check(pruned.primitives == 2 && brute.primitives == 3,"closest hit prunes farther branch before primitive test");
}

void test_random() {
    pixel_rng a,b,c;
    a.seed(42,54); b.seed(42,54); c.seed(42,55);
    double sum = 0; int different = 0;
    for(int i = 0; i < 100000; i++) {
        float x = a.uniform(), y = b.uniform(), z = c.uniform();
        check(x == y && x >= 0 && x < 1,"RNG repeatability and half-open range");
        if(x != z) different++;
        sum += x;
    }
    check(different > 99000 && fabs(sum/100000-0.5) < 0.01,"separate pixel streams and uniform mean sanity");
    vec3 center(0,0,0);
    for(int i = 0; i < 1000; i++) {
        vec3 ball = sample_ball(a), disk = sample_disk(a);
        check(ball.squared_length() < 1 && disk.squared_length() < 1 && disk.z() == 0,"rejection samplers stay in their domains");
        center += ball;
    }
    check((center/1000).length() < 0.1,"ball samples have near-zero mean");
}

void test_progressive() {
    obj_scene source(RAY_TRACER_ASSET_DIR "/models/blender_sphere/sphere.obj",1.5);
    auto scene = flatten_scene(&source);
    camera reference(source.lookfrom,source.lookat,vec3(0,1,0),40,1.5,0.2,source.focus_dist,0.2,0.8);
    auto camera = flatten_camera(reference);
    for(auto mode : {DEBUG_SHADED,DEBUG_NORMALS,DEBUG_TRIANGLE,DEBUG_MATERIAL,DEBUG_BVH,DEBUG_INTERSECTIONS}) {
        portable_renderer whole(scene,camera,19,13,7,false,mode), batched(scene,camera,19,13,7,false,mode);
        whole.add_samples(7);
        batched.add_samples(2); batched.add_samples(1); batched.add_samples(4);
        auto a = whole.image(), b = batched.image();
        bool visible = false;
        for(size_t i = 0; i < a.size(); i++) {
            check(a[i].x() == b[i].x() && a[i].y() == b[i].y() && a[i].z() == b[i].z(),"progressive sample batching is byte-stable");
            check(std::isfinite(a[i].length()) && a[i].x() >= 0 && a[i].y() >= 0 && a[i].z() >= 0,"finite nonnegative debug/shaded output");
            visible = visible || a[i].squared_length() > 0;
        }
        check(visible,"render contains visible data");
        batched.reset(camera); check(batched.sample_count() == 0,"reset clears sample count");
        for(auto pixel : batched.image()) check(pixel.squared_length() == 0,"reset clears accumulation");
        batched.add_samples(7); b = batched.image();
        for(size_t i = 0; i < a.size(); i++) check(close(a[i],b[i]),"reset restores deterministic RNG");
        if(mode <= DEBUG_MATERIAL) {
            portable_renderer brute(scene,camera,19,13,7,true,mode); brute.add_samples(7);
            b = brute.image();
            for(size_t i = 0; i < a.size(); i++) check(close(a[i],b[i]),"BVH/brute complete image parity");
        }
    }
    portable_renderer reset(scene,camera,19,13,7);
    reset.add_samples(2);
    camera.origin += vec3(0.4,0,0);
    reset.reset(camera); reset.add_samples(3);
    portable_renderer fresh(scene,camera,19,13,7); fresh.add_samples(3);
    auto a = reset.image(), b = fresh.image();
    for(size_t i = 0; i < a.size(); i++) check(close(a[i],b[i]),"changed-camera reset matches fresh renderer");
    bool rejected = false;
    try { reset.add_samples(0); } catch(const std::invalid_argument&) { rejected = true; }
    check(rejected,"invalid sample batch rejected");
}

int main() {
    try { test_assets(); test_pruning(); test_random(); test_progressive(); }
    catch(const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
