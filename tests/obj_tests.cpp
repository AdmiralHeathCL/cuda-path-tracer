#include <chrono>
#include <iostream>
#include "support/reference_render.h"
#include "app/render_options.h"
#include "scene/obj_loader.h"
#include "support/mesh_scene.h"

int failures = 0;
int checks = 0;

void check(bool condition, const char *message) {
    checks++;
    if(!condition) {
        failures++;
        std::cerr << "FAIL: " << message << "\n";
    }
}

bool close(float a, float b) { return fabs(a-b) < 0.0001f; }
bool close(const vec3& a, const vec3& b) { return (a-b).length() < 0.0001f; }

struct temporary_files {
    std::filesystem::path path;
    temporary_files() {
        auto id = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path()/std::filesystem::path("ray-tracer-obj-tests-"+std::to_string(id));
        std::filesystem::create_directory(path);
    }
    ~temporary_files() {
        std::error_code error;
        std::filesystem::remove_all(path,error);
    }
    std::string write(const std::string& name, const std::string& contents) {
        auto file = path/name;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream output(file);
        output << contents;
        return file.string();
    }
};

void expect_failure(const std::string& path) {
    bool rejected = false;
    try {
        auto model = load_obj(path);
    } catch(const std::runtime_error&) {
        rejected = true;
    }
    check(rejected,"malformed or missing OBJ assets rejected");
}

void test_cube() {
    auto imported = load_obj(RAY_TRACER_ASSET_DIR "/models/cube/cube.obj");
    check(imported->source_vertices == 8,"cube vertex count");
    check(imported->mesh->triangle_count() == 12,"six OBJ quads triangulate into twelve triangles");
    check(imported->materials.size() == 3,"MTL materials and fallback loaded");
    auto faces = cube_triangles(imported->materials[1].get());
    faces[10].mat_ptr = faces[11].mat_ptr = imported->materials[2].get();
    triangle_mesh reference(std::move(faces));
    for(int axis = 0; axis < 3; axis++) {
        for(int side : {-1,1}) {
            for(int i = -7; i <= 7; i++) {
                for(int j = -7; j <= 7; j++) {
                    vec3 origin(0,0,0), direction(0,0,0);
                    origin[axis] = 3*side;
                    origin[(axis+1)%3] = i*0.17f;
                    origin[(axis+2)%3] = j*0.13f;
                    direction[axis] = -side;
                    ray r(origin,direction);
                    hit_record a,b;
                    bool ha = imported->hit(r,0.001,10,a);
                    bool hb = reference.hit(r,0.001,10,b);
                    check(ha == hb,"imported and manual cube hit/miss agreement");
                    if(ha && hb) {
                        check(close(a.t,b.t) && close(a.p,b.p),"imported and manual cube closest intersection");
                        check(close(a.normal,b.normal),"imported cube winding and normals");
                        check(a.mat_ptr == b.mat_ptr,"per-face material assignment");
                    }
                }
            }
        }
    }
}

void test_uv_and_images() {
    auto imported = load_obj(RAY_TRACER_ASSET_DIR "/models/textured_triangle/triangle.obj");
    check(imported->mesh->triangle_count() == 1,"negative OBJ vertex indices resolved");
    ray r(vec3(0.25,0.25,1),vec3(0,0,-1),0.8);
    hit_record rec;
    bool hit = imported->hit(r,0,10,rec);
    check(hit,"textured triangle hit");
    if(hit) {
        check(close(rec.u,0.3) && close(rec.v,0.35),"OBJ UVs interpolated barycentrically");
        ray scattered;
        vec3 attenuation;
        check(rec.mat_ptr->scatter(r,rec,attenuation,scattered),"imported diffuse material scatters");
        check(close(attenuation,vec3(0,0,1)),"Lambertian uses UVs, MTL-relative texture path, and image V orientation");
        check(close(scattered.time(),0.8),"textured scatter preserves time");
    }
    ray other(vec3(0.75,0.1,1),vec3(0,0,-1));
    hit = imported->hit(other,0,10,rec);
    check(hit,"second textured triangle hit");
    if(hit) {
        ray scattered;
        vec3 attenuation;
        rec.mat_ptr->scatter(other,rec,attenuation,scattered);
        check(close(attenuation,vec3(0.5,0.75,1)),"map_Kd is multiplied by Kd");
    }

    image_texture rgba(RAY_TRACER_ASSET_DIR "/../tests/fixtures/rgba.png");
    check(close(rgba.value(0.25,0.5,vec3(0,0,0)),vec3(1,0,0)),"RGBA converted to RGB; alpha not mistaken for color");
    check(close(rgba.value(0.75,0.5,vec3(0,0,0)),vec3(0,1,0)),"RGBA pixel stride is RGB after loading");
    image_texture gray(RAY_TRACER_ASSET_DIR "/../tests/fixtures/gray.png");
    check(close(gray.value(0.25,0.5,vec3(0,0,0)),vec3(64,64,64)/255),"grayscale converted to RGB");
    check(close(gray.value(1e30f,0.5,vec3(0,0,0)),vec3(192,192,192)/255),"large UV clamps before integer conversion");
    image_texture empty;
    check(close(empty.value(0,0,vec3(0,0,0)),vec3(0,1,1)),"uninitialized image texture is safe");
    bool rejected = false;
    try { image_texture missing("/nonexistent-ray-tracer-image.png"); }
    catch(const std::runtime_error&) { rejected = true; }
    check(rejected,"missing image reports an error");
}

void test_normals_and_materials(temporary_files& files) {
    const std::string vertices = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    auto path = files.write("smooth.obj",vertices+"vn 0 0 2\nvn 0 2 2\nvn 2 0 2\nf 1//1 2//2 3//3\n");
    auto model = load_obj(path);
    hit_record rec;
    ray r(vec3(0.25,0.25,1),vec3(0,0,-1));
    bool hit = model->hit(r,0,10,rec);
    check(hit,"OBJ smooth triangle hit");
    if(hit) check(close(rec.normal,unit_vector(vec3(0,0,0.5)+0.25*unit_vector(vec3(0,2,2))+0.25*unit_vector(vec3(2,0,2)))),"imported vertex normals normalized and interpolated");
    path = files.write("partial.obj",vertices+"vn 0 1 0\nf 1//1 2 3\n");
    model = load_obj(path);
    hit = model->hit(r,0,10,rec);
    check(hit && close(rec.normal,vec3(0,0,1)),"partially missing normals fall back to geometric normal");
    check(!model->warnings.empty(),"missing attributes produce a warning");

    files.write("surfaces.mtl","newmtl lamp\nKe 2 1 0.5\nnewmtl glass\nillum 7\nNi 1.5\nnewmtl mirror\nillum 5\nKs 0.8 0.7 0.6\n");
    path = files.write("surfaces.obj","mtllib surfaces.mtl\n"+vertices+"usemtl lamp\nf 1 2 3\n");
    model = load_obj(path);
    check(dynamic_cast<diffuse_light*>(model->materials[1].get()) != nullptr,"Ke maps to diffuse_light");
    auto glass = dynamic_cast<dielectric*>(model->materials[2].get());
    check(glass && close(glass->ref_idx,1.5),"glass illumination mode maps Ni to dielectric");
    auto mirror = dynamic_cast<metal*>(model->materials[3].get());
    check(mirror && close(mirror->albedo,vec3(0.8,0.7,0.6)) && close(mirror->fuzz,0),"mirror mode maps Ks to ideal metal");
    check(close(color(r,model.get(),0),vec3(2,1,0.5)),"CPU integrator sees imported emission");
    check(color_channel(2) == 255 && color_channel(-1) == 0,"output clamps radiance to valid PPM channels");
    check(color_channel(std::numeric_limits<float>::quiet_NaN()) == 0,"invalid output sample is safe");
}

void test_errors(temporary_files& files) {
    const std::string vertices = "v 0 0 0\nv 1 0 0\nv 0 1 0\n";
    expect_failure((files.path/"missing.obj").string());
    expect_failure(files.write("empty.obj","# Empty model\n"));
    expect_failure(files.write("index.obj",vertices+"f 1 2 20\n"));
    expect_failure(files.write("uv-index.obj",vertices+"vt 0 0\nf 1/1 2/2 3/1\n"));
    expect_failure(files.write("normal-index.obj",vertices+"vn 0 0 1\nf 1//1 2//2 3//1\n"));
    expect_failure(files.write("degenerate.obj","v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n"));
    expect_failure(files.write("missing-mtl.obj","mtllib missing.mtl\n"+vertices+"f 1 2 3\n"));
    try {
        load_obj(files.write("renamed.obj","mtllib Untitled.mtl\n"+vertices+"f 1 2 3\n"));
        check(false,"missing material library must reject import");
    } catch(const std::runtime_error& error) {
        std::string message = error.what();
        auto first = message.find("could not load MTL:");
        check(first != std::string::npos && message.find("could not load MTL:",first+1) == std::string::npos,
              "material library failure is reported once");
        check(message.find("mtllib filename") != std::string::npos,"material library failure explains the OBJ reference");
    }
    files.write("missing-image.mtl","newmtl paint\nKd 1 1 1\nmap_Kd missing.png\n");
    expect_failure(files.write("missing-image.obj","mtllib missing-image.mtl\n"+vertices+"usemtl paint\nf 1 2 3\n"));
    files.write("bad-image.png","not an image");
    files.write("bad-image.mtl","newmtl paint\nKd 1 1 1\nmap_Kd bad-image.png\n");
    expect_failure(files.write("bad-image.obj","mtllib bad-image.mtl\n"+vertices+"usemtl paint\nf 1 2 3\n"));
    auto model = load_obj(files.write("mixed.obj",vertices+"f 1 1 1\nf 1 2 3\n"));
    check(model->mesh->triangle_count() == 1,"degenerate face skipped while retaining valid geometry");
    check(model->warnings.find("degenerate") != std::string::npos,"degenerate face warning retained");
}

void test_blender_export() {
    auto model = load_obj(RAY_TRACER_ASSET_DIR "/models/blender_sphere/sphere.obj");
    check(model->mesh->triangle_count() == 224,"actual Blender export loads all triangles");
    hit_record rec;
    bool hit = model->hit(ray(vec3(0.1,0.1,3),vec3(0,0,-1)),0,10,rec);
    check(hit,"Blender sphere has visible surface");
    if(hit) {
        check(close(rec.normal.length(),1),"Blender smooth normal is unit length");
        check(rec.u >= 0 && rec.u <= 1 && rec.v >= 0 && rec.v <= 1,"Blender UVs are preserved");
    }
}


void test_transforms(temporary_files& files) {
    const std::string cube = RAY_TRACER_ASSET_DIR "/models/cube/cube.obj";
    auto original = load_obj(cube);
    for(int sign : {-1,1}) {
        mesh_transform transform(vec3(sign*2,3,4),vec3(90,90,90),vec3(5,-2,7));
        auto model = load_obj(cube,transform);
        aabb bounds;
        check(model->bounding_box(0,1,bounds),"transformed mesh has bounds");
        check((bounds.min()-vec3(1,-5,5)).length() < 0.001f &&
              (bounds.max()-vec3(9,1,9)).length() < 0.001f,"BVH bounds contain baked transformed cube");
        // For these three quarter turns, R(x,y,z) = (z,y,-x).
        for(int axis = 0; axis < 3; axis++) {
            for(int side : {-1,1}) {
                for(int i = -4; i <= 4; i++) {
                    for(int j = -4; j <= 4; j++) {
                        vec3 origin(0,0,0), direction(0,0,0);
                        origin[axis] = side*3;
                        origin[(axis+1)%3] = i*0.29f;
                        origin[(axis+2)%3] = j*0.23f;
                        direction[axis] = -side;
                        vec3 transformed_origin(4*origin.z()+5,3*origin.y()-2,-sign*2*origin.x()+7);
                        vec3 transformed_direction(4*direction.z(),3*direction.y(),-sign*2*direction.x());
                        hit_record a,b;
                        bool ha = original->hit(ray(origin,direction),0,10,a);
                        bool hb = model->hit(ray(transformed_origin,transformed_direction),0,10,b);
                        check(ha == hb,"transformed BVH hit/miss matches original rays");
                        if(ha && hb) {
                            check(close(a.t,b.t),"affine transform preserves ray parameter");
                            check(close(b.normal,unit_vector(vec3(a.normal.z()/4,a.normal.y()/3,-a.normal.x()/(sign*2)))),
                                  "mirrored and rotated cube retain outward normals");
                            check(close(a.u,b.u) && close(a.v,b.v),"transformed and mirrored UV assignment preserved");
                            auto ma = dynamic_cast<lambertian*>(a.mat_ptr);
                            auto mb = dynamic_cast<lambertian*>(b.mat_ptr);
                            check(ma && mb && close(ma->albedo->value(a.u,a.v,a.p),mb->albedo->value(b.u,b.v,b.p)),
                                  "transformed face material preserved");
                        }
                    }
                }
            }
        }
    }

    auto path = files.write("sloped.obj","v 0 0 0\nv 1 0 -1\nv 0 1 -1\n"
                            "vn 1 1 1\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1/1 2/2/1 3/3/1\n");
    auto model = load_obj(path,mesh_transform(vec3(2,3,4),vec3(90,90,90),vec3(5,-2,7)));
    vec3 point(3,-1.25,6.5);
    vec3 expected_normal = unit_vector(vec3(0.25,1.0/3,-0.5));
    hit_record rec;
    bool hit = model->hit(ray(point+2*expected_normal,-expected_normal),0,10,rec);
    check(hit,"nonuniformly scaled sloped triangle hit");
    if(hit) {
        check(close(rec.p,point) && close(rec.t,2),"scale then XYZ rotation then translation");
        check(close(rec.normal,expected_normal),"smooth normal uses inverse transpose under nonuniform scale");
        check(close(rec.u,0.25) && close(rec.v,0.25),"UV interpolation survives transform");
    }
    auto flat_path = files.write("flat-mirror.obj","v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    model = load_obj(flat_path,mesh_transform(vec3(-1,1,1)));
    hit = model->hit(ray(vec3(-0.25,0.25,1),vec3(0,0,-1)),0,10,rec);
    check(hit && close(rec.normal,vec3(0,0,1)),"mirroring corrects winding even without vertex normals");
    check(hit && close(rec.u,0.25) && close(rec.v,0.25),"canonical UVs follow mirrored vertices");

    for(vec3 scale : {vec3(0,1,1),vec3(INFINITY,1,1),vec3(1e30f,1e30f,1e30f),vec3(1e-30f,1e-30f,1e-30f)}) {
        bool rejected = false;
        try { auto invalid = load_obj(path,mesh_transform(scale)); }
        catch(const std::exception&) { rejected = true; }
        check(rejected,"singular, nonfinite, overflowing or collapsed transforms rejected");
    }
}

void test_transform_options() {
    auto parse = [](std::vector<std::string> arguments) {
        std::vector<char*> argv;
        for(auto& value : arguments) argv.push_back(value.data());
        return parse_options(int(argv.size()),argv.data());
    };
    auto options = parse({"ray_tracer","--obj-scale","-2","3","4","--obj","cube.obj",
                          "--obj-rotate","90","0","-20","--obj-translate","1","2","3"});
    check(close(options.obj_scale,vec3(-2,3,4)) && close(options.obj_rotate,vec3(90,0,-20)) &&
          close(options.obj_translate,vec3(1,2,3)),"CLI accepts transform vectors before or after OBJ");
    for(auto args : std::vector<std::vector<std::string>>{
        {"ray_tracer","--obj-scale","1","2"},
        {"ray_tracer","--obj-scale","1","2","3"},
        {"ray_tracer","--obj","cube.obj","--obj-scale","1","0","1"},
        {"ray_tracer","--obj","cube.obj","--obj-rotate","nan","0","0"},
        {"ray_tracer","--obj","cube.obj","--obj-translate","0","inf","0"},
        {"ray_tracer","--obj","cube.obj","--obj-scale","1x","2","3"}}) {
        bool rejected = false;
        try { parse(args); }
        catch(const std::invalid_argument&) { rejected = true; }
        check(rejected,"CLI rejects invalid or incomplete transform options");
    }
}

int main() {
    try {
        temporary_files files;
        test_cube();
        test_uv_and_images();
        test_normals_and_materials(files);
        test_errors(files);
        test_blender_export();
        test_transforms(files);
        test_transform_options();
    } catch(const std::exception& error) {
        std::cerr << "Unexpected exception: " << error.what() << "\n";
        return 1;
    }
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
