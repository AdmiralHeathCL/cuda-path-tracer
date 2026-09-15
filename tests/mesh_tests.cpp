#include <iostream>
#include <stdexcept>
#include <vector>
#include "geometry/box.h"
#include "support/mesh_scene.h"
#include "transforms/translate.h"

int failures = 0;
int checks = 0;

void check(bool condition, const char *message) {
    checks++;
    if(!condition) {
        std::cerr << "FAIL: " << message << "\n";
        failures++;
    }
}

bool close(float a, float b) {
    return fabs(a-b) < 0.0001;
}

bool close(const vec3& a, const vec3& b) {
    return (a-b).length() < 0.0001;
}

void compare(hitable& mesh, hitable& reference, const ray& r, bool compare_uv = false) {
    hit_record a, b;
    bool ha = mesh.hit(r, 0.001, 100, a);
    bool hb = reference.hit(r, 0.001, 100, b);
    check(ha == hb, "mesh/reference hit and miss agreement");
    if(ha && hb) {
        check(close(a.t,b.t), "mesh/reference closest distance");
        check(close(a.p,b.p), "mesh/reference position");
        check(close(a.normal,b.normal), "mesh/reference outward normal");
        check(a.mat_ptr == b.mat_ptr, "mesh/reference material");
        if(compare_uv) check(close(a.u,b.u) && close(a.v,b.v), "mesh/list UVs");
    }
}

void test_cube(material *mat) {
    std::vector<triangle> faces = cube_triangles(mat);
    triangle_mesh mesh(faces);
    check(mesh.triangle_count() == 12, "cube consists of twelve triangles");
    box reference(vec3(-1,-1,-1),vec3(1,1,1),mat);

    std::vector<hitable*> objects;
    for(triangle& face : faces) objects.push_back(&face);
    hitable_list list(objects.data(),int(objects.size()));
    aabb bounds;
    check(mesh.bounding_box(0,1,bounds), "mesh has a bounding box");
    for(int axis = 0; axis < 3; axis++) {
        check(bounds.min()[axis] < -1 && bounds.max()[axis] > 1, "mesh bounds enclose the cube with padding");
        for(int side = -1; side <= 1; side += 2) {
            // Probe the interior of each face and surrounding misses from both sides.
            // Avoid cube edges where multiple equally close face normals are valid.
            for(int i = -8; i <= 8; i++) {
                for(int j = -8; j <= 8; j++) {
                    vec3 origin(0,0,0), direction(0,0,0);
                    origin[axis] = 3*side;
                    origin[(axis+1)%3] = i*0.17f;
                    origin[(axis+2)%3] = j*0.13f;
                    direction[axis] = -2*side;
                    ray r(origin,direction,0.6);
                    compare(mesh,reference,r);
                    // Away from the shared diagonal, there is one unique triangle/UV.
                    if(fabs(origin[(axis+1)%3]) < 1 && fabs(origin[(axis+2)%3]) < 1 &&
                       fabs(fabs(origin[(axis+1)%3])-fabs(origin[(axis+2)%3])) > 0.01) {
                        compare(mesh,list,r,true);
                    }
                }
            }
            vec3 direction(0,0,0);
            direction[axis] = side;
            compare(mesh,reference,ray(vec3(0,0,0),direction));
            hit_record rec;
            bool hit = mesh.hit(ray(vec3(0,0,0),direction),0,10,rec);
            check(hit && close(rec.normal,direction), "inside ray exits with outward normal");
        }
    }

    // Transform wrappers apply to both a complete mesh and a box.
    rotate_y mesh_rotation(&mesh,31);
    rotate_y reference_rotation(&reference,31);
    translate moved_mesh(&mesh_rotation,vec3(2,0.5,-1));
    translate moved_reference(&reference_rotation,vec3(2,0.5,-1));
    for(int j = -9; j <= 9; j++) {
        for(int i = -9; i <= 9; i++) {
            compare(moved_mesh,moved_reference,ray(vec3(2+i*0.2f,0.5+j*0.17f,4),vec3(0.01,0.02,-2)));
        }
    }

    // Geometry is copied into stable mesh storage; changing the caller's vector
    // cannot invalidate the BVH or change its geometry.
    faces.clear();
    faces.shrink_to_fit();
    hit_record rec;
    bool hit = mesh.hit(ray(vec3(0,0,3),vec3(0,0,-1)),0,10,rec);
    check(hit && close(rec.t,2), "mesh remains valid after source storage is released");
}

void test_shared_edges(material *mat) {
    triangle_mesh cube(cube_triangles(mat));
    vec3 points[] = {
        vec3(0,0,1), vec3(1,0,1), vec3(1,1,1), vec3(-1,-1,1),
        vec3(0,0,-1), vec3(-1,0,-1), vec3(-1,1,-1)
    };
    for(const vec3& p : points) {
        vec3 offset(0,0,p.z());
        hit_record rec;
        bool hit = cube.hit(ray(p + offset,-offset),0,10,rec);
        check(hit, "cube shared edge/vertex ray hits");
        if(hit) check(close(rec.p,p), "cube shared edge/vertex position");
    }
}

void test_empty_and_single(material *mat) {
    triangle_mesh empty(std::vector<triangle>{});
    ray r(vec3(0.25,0.25,1),vec3(0,0,-1));
    hit_record rec;
    aabb bounds;
    check(empty.triangle_count() == 0, "empty mesh has zero triangles");
    check(!empty.hit(r,0,10,rec), "empty mesh misses");
    check(!empty.bounding_box(0,1,bounds), "empty mesh has no bounds");

    triangle face(vec3(0,0,0),vec3(1,0,0),vec3(0,1,0),mat);
    triangle_mesh single(std::vector<triangle>{face});
    compare(single,face,r,true);

    bvh_node default_tree;
    check(!default_tree.hit(r,0,10,rec), "default BVH safely misses");
    check(!default_tree.bounding_box(0,1,bounds), "default BVH has no bounds");
    hitable *objects[] = {&face};
    for(int count : {-1,0}) {
        bool rejected = false;
        try {
            bvh_node invalid(objects,count,0,1);
        } catch(const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "nonpositive BVH object count rejected");
    }
    bool rejected = false;
    try {
        bvh_node invalid(nullptr,1,0,1);
    } catch(const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "null BVH object array rejected");
    hitable *invalid_objects[] = {nullptr,&empty};
    for(hitable *object : invalid_objects) {
        rejected = false;
        try {
            bvh_node invalid(&object,1,0,1);
        } catch(const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, "null primitives and unbounded empty meshes rejected by BVH");
    }
}

void test_equal_keys_and_ownership(material *mat) {
    triangle a(vec3(0,0,0),vec3(1,0,0),vec3(0,1,0),mat);
    triangle b(vec3(0,0,0),vec3(2,0,0),vec3(0,2,0),mat);
    triangle c(vec3(2,2,2),vec3(3,2,2),vec3(2,3,2),mat);
    hitable *pa = &a, *pb = &b, *pc = &c;
    using comparator = int (*)(const void*,const void*);
    for(comparator cmp : {box_x_compare,box_y_compare,box_z_compare}) {
        check(cmp(&pa,&pa) == 0, "comparator self equality");
        check(cmp(&pa,&pb) == 0 && cmp(&pb,&pa) == 0, "comparator equal bounds");
        check(cmp(&pa,&pc) < 0 && cmp(&pc,&pa) > 0, "comparator ordering is antisymmetric");
    }

    hitable *leaves[] = {&a,&b};
    bvh_node borrowed_tree(leaves,2,0,1);
    {
        hitable *objects[] = {&borrowed_tree,&c,&a,&b};
        bvh_node parent(objects,4,0,1);
    }
    hit_record rec;
    check(borrowed_tree.hit(ray(vec3(0.25,0.25,1),vec3(0,0,-1)),0,10,rec), "destroying parent preserves borrowed BVH root and primitives");
}

int main() {
    constant_texture albedo(vec3(0.7,0.4,0.2));
    lambertian mat(&albedo);
    srand48(42);
    test_cube(&mat);
    test_shared_edges(&mat);
    test_empty_and_single(&mat);
    test_equal_keys_and_ownership(&mat);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
