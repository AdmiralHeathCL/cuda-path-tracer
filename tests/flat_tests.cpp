#include <iostream>
#include <random>
#include "flat/flat_export.h"
#include "support/flat_cpu.h"
#include "support/reference_render.h"
#include "transforms/rotate_y.h"

int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    checks++;
    if(!ok) { failures++; if(failures < 20) std::cerr << "FAIL: " << message << "\n"; }
}
bool close(float a, float b) { return fabs(a-b) <= 0.00001f*ffmax(1,ffmax(fabs(a),fabs(b))); }
bool close(vec3 a, vec3 b) { return close(a.x(),b.x()) && close(a.y(),b.y()) && close(a.z(),b.z()); }
template<class Function> void rejected(Function fn) {
    bool failed = false;
    try { fn(); } catch(const std::exception&) { failed = true; }
    check(failed,"invalid/unsupported data rejected");
}

void topology(const hitable *source, const flat_scene& flat, int id) {
    if(auto s = dynamic_cast<const obj_scene*>(source)) { topology(s->bvh_root(),flat,id); return; }
    if(auto s = dynamic_cast<const obj_model*>(source)) { topology(s->mesh.get(),flat,id); return; }
    if(auto s = dynamic_cast<const triangle_mesh*>(source)) { topology(s->bvh_root(),flat,id); return; }
    aabb bounds;
    check(source->bounding_box(0,1,bounds),"source bounds exist");
    const auto& n = flat.nodes.at(id);
    check(close(n.minimum,bounds.min()) && close(n.maximum,bounds.max()),"source bounding boxes copied");
    if(auto s = dynamic_cast<const bvh_node*>(source)) {
        check(n.type == FLAT_BRANCH,"source branches retained");
        check((s->left == s->right) == (n.left == n.right),"single-child alias retained");
        topology(s->left,flat,n.left);
        if(s->left != s->right) topology(s->right,flat,n.right);
    } else if(auto s = dynamic_cast<const triangle*>(source)) {
        check(n.type == FLAT_TRIANGLE,"triangle leaf retained");
        const auto& t = flat.triangles.at(n.primitive);
        check(close(t.v0,s->v0) && close(t.v1,s->v1) && close(t.v2,s->v2),"source vertex order retained");
    } else check(n.type == FLAT_SPHERE,"sphere leaf retained");
}

void compare_ray(const hitable& source, const flat_scene& flat, const ray& r, int seed) {
    hit_record a;
    flat_hit b;
    bool ha = source.hit(r,0.001,MAXFLOAT,a);
    bool hb = flat_hit_world(flat,r,0.001,MAXFLOAT,b);
    check(ha == hb,"flat/reference hit agreement");
    if(!ha || !hb) return;
    check(close(a.t,b.t) && close(a.p,b.p),"flat closest distance/position");
    check(close(a.normal,b.normal) && close(a.u,b.u) && close(a.v,b.v),"flat normals and UVs");
    const auto& mat = flat.materials.at(b.material_id);
    vec3 emitted = mat.type == FLAT_LIGHT ? flat_texture_value(flat,mat.texture_id,b.u,b.v) : vec3(0,0,0);
    check(close(emitted,a.mat_ptr->emitted(a.u,a.v,a.p)),"flat material emission");
    ray ar,br;
    vec3 aa,ba;
    srand48(seed);
    bool sa = a.mat_ptr->scatter(r,a,aa,ar);
    srand48(seed);
    bool sb = flat_scatter(flat,mat,r,b,ba,br);
    check(sa == sb,"flat material scatter decision");
    if(sa && sb) check(close(aa,ba) && close(ar.origin(),br.origin()) && close(ar.direction(),br.direction()) && close(ar.time(),br.time()),"flat attenuation, scatter direction, and time");
}

void scene_rays(const hitable& source, const flat_scene& flat, int count) {
    aabb bounds;
    source.bounding_box(0,1,bounds);
    vec3 center = 0.5*bounds.min()+0.5*bounds.max();
    vec3 extent = bounds.max()-bounds.min();
    float radius = extent.length();
    std::mt19937 rng(314159);
    auto random = [&]() { return float(double(rng())/4294967296.0); };
    for(int i = 0; i < count; i++) {
        vec3 origin, target;
        for(int axis = 0; axis < 3; axis++) {
            origin[axis] = center[axis]+(random()-0.5f)*extent[axis];
            target[axis] = center[axis]+(random()-0.5f)*extent[axis];
        }
        if(i%3 == 0) origin[i%3] += radius;
        if(i%3 == 1) {
            int axis = (i/3)%3;
            origin[axis] = center[axis]+((i/9)%2 ? radius : -radius);
            target = origin;
            target[axis] = center[axis];
        }
        compare_ray(source,flat,ray(origin,target-origin,0.37),i);
    }
}

void test_assets(const std::string& path, int count) {
    auto model = load_obj(path);
    auto flat = flatten_scene(model.get());
    check(flat.triangles.size() == model->mesh->triangle_count(),"each imported triangle exported once");
    topology(model.get(),flat,flat.root);
    scene_rays(*model,flat,count);
    // Arrays own all payloads and remain usable after the pointer graph dies.
    aabb bounds;
    model->bounding_box(0,1,bounds);
    vec3 center = 0.5*bounds.min()+0.5*bounds.max();
    ray r(center+vec3(0,0,2*(bounds.max()-bounds.min()).length()),vec3(0,0,-1));
    flat_hit before, after;
    bool hit = flat_hit_world(flat,r,0,MAXFLOAT,before);
    model.reset();
    auto copy = flat;
    flat = flat_scene();
    bool copied_hit = flat_hit_world(copy,r,0,MAXFLOAT,after);
    check(hit == copied_hit && (!hit || close(before.t,after.t)),"copied arrays independent of source lifetime");
}

void test_material_scene() {
    constant_texture paint(vec3(0.4,0.7,0.2)), emission(vec3(4,3,2));
    lambertian diffuse(&paint);
    metal mirror(vec3(0.8,0.7,0.6),0.2);
    dielectric glass(1.5);
    diffuse_light light(&emission);
    sphere a(vec3(-2,0,0),1,&diffuse), b(vec3(0,0,0),1,&glass), c(vec3(2,0,0),1,&mirror);
    sphere lamp(vec3(0,4,2),2,&light), ground(vec3(0,-101,0),100,&diffuse);
    hitable *objects[] = {&a,&b,&c,&lamp,&ground};
    bvh_node world(objects,5,0,1);
    auto flat = flatten_scene(&world);
    check(flat.materials.size() == 4 && flat.textures.size() == 2,"shared materials/textures deduplicated");
    topology(&world,flat,flat.root);
    for(int j = -8; j <= 8; j++) {
        for(int i = -20; i <= 20; i++) {
            ray r(vec3(0,1,8),vec3(i*0.2f,j*0.2f,-8),0.6);
            compare_ray(world,flat,r,i+100*j);
            srand48(91+i+100*j);
            vec3 expected = color(r,&world,0);
            srand48(91+i+100*j);
            check(close(expected,flat_color(r,flat)),"iterative radiance matches recursive material scene");
        }
    }
    // Internal glass ray exercises total internal reflection.
    compare_ray(world,flat,ray(vec3(0,0,0.9),vec3(1,0,0),0.4),7);
    camera cam(vec3(0,1,8),vec3(0,0,0),vec3(0,1,0),40,1.5,0.3,8,0.2,0.9);
    auto fc = flatten_camera(cam);
    for(int i = 0; i < 64; i++) {
        srand48(i); ray expected = cam.get_ray(0.13f*i,0.23f*i);
        srand48(i); ray actual = flat_camera_ray(fc,0.13f*i,0.23f*i);
        check(close(expected.origin(),actual.origin()) && close(expected.direction(),actual.direction()) && close(expected.time(),actual.time()),"flat camera lens/time sampling matches");
    }
}

void test_ties_and_errors() {
    constant_texture red(vec3(1,0,0)), green(vec3(0,1,0));
    lambertian a(&red), b(&green);
    triangle first(vec3(0,0,0),vec3(1,0,0),vec3(0,1,0),&a);
    triangle second(vec3(0,0,0),vec3(1,0,0),vec3(0,1,0),&b);
    second.uv0 = second.uv1 = second.uv2 = vec3(0.8,0.9,0);
    hitable *objects[] = {&first,&second};
    bvh_node world(objects,2,0,1);
    auto flat = flatten_scene(&world);
    for(vec3 p : {vec3(0.2,0.3,1),vec3(0,0,1),vec3(0.5,0.5,1)}) compare_ray(world,flat,ray(p,vec3(0,0,-1)),7);
    auto bad = flat; bad.nodes[bad.root].left = 9999; rejected([&]() { validate_flat_scene(bad); });
    bad = flat; bad.nodes[bad.root].left = bad.root; rejected([&]() { validate_flat_scene(bad); });
    bad = flat; bad.triangles[0].material_id = -1; rejected([&]() { validate_flat_scene(bad); });
    bad = flat; bad.nodes[bad.root].maximum = vec3(-99,-99,-99); rejected([&]() { validate_flat_scene(bad); });
    bad = flat; bad.max_depth++; rejected([&]() { validate_flat_scene(bad); });
    auto image = load_obj(RAY_TRACER_ASSET_DIR "/models/textured_triangle/triangle.obj");
    bad = flatten_scene(image.get()); bad.pixels.clear(); rejected([&]() { validate_flat_scene(bad); });
    triangle_mesh empty({});
    auto empty_flat = flatten_scene(&empty);
    flat_hit rec;
    check(!flat_hit_world(empty_flat,ray(vec3(0,0,1),vec3(0,0,-1)),0,10,rec),"empty flat mesh safely misses");
    rotate_y rotated(&world,20); rejected([&]() { flatten_scene(&rotated); });
    checker_texture checker(&red,&green); lambertian unsupported(&checker);
    sphere unsupported_sphere(vec3(0,0,0),1,&unsupported);
    rejected([&]() { flatten_scene(&unsupported_sphere); });

    // Construct a legal deep pointer tree without replacing the production builder.
    std::vector<std::unique_ptr<bvh_node>> chain;
    hitable *child = &first;
    aabb bounds; first.bounding_box(0,1,bounds);
    for(int i = 0; i < flat_stack_capacity-1; i++) {
        auto node = std::make_unique<bvh_node>();
        node->left = child; node->right = &second; node->box = bounds;
        child = node.get(); chain.push_back(std::move(node));
    }
    auto deepest = flatten_scene(child);
    check(deepest.max_depth == flat_stack_capacity,"maximum supported stack depth accepted");
    compare_ray(*child,deepest,ray(vec3(0.2,0.2,1),vec3(0,0,-1)),7);
    bvh_node too_deep; too_deep.left = child; too_deep.right = &first; too_deep.box = bounds;
    rejected([&]() { flatten_scene(&too_deep); });
    bvh_node cyclic; cyclic.left = &cyclic; cyclic.right = &first; cyclic.box = bounds;
    rejected([&]() { flatten_scene(&cyclic); });
}


void test_texture_payloads() {
    unsigned char pixels[] = {255,0,0,0,255,0,0,0,255,255,255,255};
    image_texture image(pixels,2,2);
    tinyobj::texture_option_t options{};
    options.scale[0] = 2; options.scale[1] = 0.5;
    options.origin_offset[0] = -0.3; options.origin_offset[1] = 0.2;
    for(bool clamp : {false,true}) {
        options.clamp = clamp;
        obj_detail::diffuse_map map(&image,vec3(0.5,0.7,0.9),options);
        lambertian material(&map);
        sphere first(vec3(-2,0,0),1,&material), second(vec3(2,0,0),1,&material);
        hitable *objects[] = {&first,&second};
        bvh_node root(objects,2,0,1);
        auto flat = flatten_scene(&root);
        check(flat.pixels.size() == 12 && flat.textures.size() == 2 && flat.materials.size() == 1,
              "shared image, map, and material payloads stored once");
        int id = flat.materials[0].texture_id;
        for(float u : {-3.0f,-0.1f,0.0f,0.3f,1.0f,2.7f}) {
            for(float v : {-1.0f,0.0f,0.5f,1.0f,3.0f}) {
                check(close(map.value(u,v,vec3(0,0,0)),flat_texture_value(flat,id,u,v)),
                      "flat UV scale/offset, repeat/clamp, tint and V flip");
            }
        }
        auto before = flat_texture_value(flat,id,0.2,0.7);
        unsigned char saved = pixels[0]; pixels[0] = 0;
        check(close(before,flat_texture_value(flat,id,0.2,0.7)),"exported pixels do not borrow source image memory");
        pixels[0] = saved;
    }
}

int main(int argc, char **argv) {
    try {
        if(argc > 1) test_assets(argv[1],argc > 2 ? std::stoi(argv[2]) : 4096);
        else {
            for(const char *file : {"/models/cube/cube.obj","/models/textured_triangle/triangle.obj","/models/blender_sphere/sphere.obj"}) {
                test_assets(std::string(RAY_TRACER_ASSET_DIR)+file,1024);
            }
            auto transformed = load_obj(RAY_TRACER_ASSET_DIR "/models/blender_sphere/sphere.obj",
                mesh_transform(vec3(-2,0.5,1),vec3(20,35,10),vec3(2,1,-3)));
            auto flat = flatten_scene(transformed.get());
            scene_rays(*transformed,flat,1024);
            test_material_scene();
            test_texture_payloads();
            test_ties_and_errors();
        }
    } catch(const std::exception& e) { std::cerr << "Unexpected: " << e.what() << "\n"; return 1; }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
