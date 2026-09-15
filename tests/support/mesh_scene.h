#ifndef MESHSCENEH
#define MESHSCENEH

#include "geometry/triangle_mesh.h"
#include "geometry/sphere.h"
#include "transforms/rotate_y.h"

// Twelve manually defined triangles forming a closed cube from -1 to +1.
// Each face is wound outward and keeps a hard edge at the adjacent face.
inline std::vector<triangle> cube_triangles(material *mat) {
    vec3 vertices[] = {
        vec3(-1,-1,-1), vec3(1,-1,-1), vec3(1,1,-1), vec3(-1,1,-1),
        vec3(-1,-1,1), vec3(1,-1,1), vec3(1,1,1), vec3(-1,1,1)
    };
    int indices[][3] = {
        {0,2,1}, {0,3,2}, // -Z
        {4,5,6}, {4,6,7}, // +Z
        {0,4,7}, {0,7,3}, // -X
        {1,2,6}, {1,6,5}, // +X
        {0,1,5}, {0,5,4}, // -Y
        {3,7,6}, {3,6,2}  // +Y
    };

    std::vector<triangle> faces;
    faces.reserve(12);
    for(const auto& face : indices) {
        faces.emplace_back(vertices[face[0]], vertices[face[1]], vertices[face[2]], mat);
    }
    return faces;
}

inline hitable *mesh_scene() {
    material *orange = new lambertian(new constant_texture(vec3(0.8, 0.4, 0.15)));
    hitable **list = new hitable*[3];
    list[0] = new rotate_y(new triangle_mesh(cube_triangles(orange)), -15);
    list[1] = new sphere(
        vec3(0,-1001,0), 1000,
        new lambertian(new constant_texture(vec3(0.55,0.55,0.55)))
    );
    list[2] = new sphere(
        vec3(0,5,6), 3,
        new diffuse_light(new constant_texture(vec3(1,1,1)))
    );
    return new bvh_node(list, 3, 0, 1);
}

#endif
