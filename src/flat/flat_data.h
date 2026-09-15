#ifndef FLATDATAH
#define FLATDATAH

#include <cstdint>
#include <type_traits>
#include <vector>
#include "core/ray.h"

// Transfer records contain values and indices only. Host vectors own the arrays.
enum flat_primitive_type : int32_t { FLAT_BRANCH, FLAT_TRIANGLE, FLAT_SPHERE };
enum flat_material_type : int32_t { FLAT_DIFFUSE, FLAT_METAL, FLAT_GLASS, FLAT_LIGHT };
enum flat_texture_type : int32_t { FLAT_CONSTANT, FLAT_IMAGE, FLAT_MAP };

struct flat_triangle {
    vec3 v0, v1, v2, n0, n1, n2, uv0, uv1, uv2;
    int32_t material_id, smooth;
};
struct flat_sphere { vec3 center; float radius; int32_t material_id; };
struct flat_node {
    vec3 minimum, maximum;
    int32_t type, left, right, primitive;
};
struct flat_material {
    vec3 albedo;
    float fuzz, ref_idx;
    int32_t type, texture_id;
};
struct flat_texture {
    vec3 color, scale, offset;
    int32_t type, image_id, width, height, byte_offset, clamp;
};
struct flat_camera {
    vec3 origin, lower_left_corner, horizontal, vertical, u, v;
    float lens_radius, time0, time1;
};
struct flat_hit {
    float t, u, v;
    vec3 p, normal;
    int32_t material_id, primitive_type, primitive_id;
};

#define FLAT_RECORD_CHECK(T) static_assert(std::is_trivially_copyable<T>::value && std::is_standard_layout<T>::value, #T " must be a plain transfer record")
FLAT_RECORD_CHECK(flat_triangle);
FLAT_RECORD_CHECK(flat_sphere);
FLAT_RECORD_CHECK(flat_node);
FLAT_RECORD_CHECK(flat_material);
FLAT_RECORD_CHECK(flat_texture);
FLAT_RECORD_CHECK(flat_camera);
FLAT_RECORD_CHECK(flat_hit);
#undef FLAT_RECORD_CHECK

constexpr int flat_stack_capacity = 64;
struct flat_scene {
    std::vector<flat_triangle> triangles;
    std::vector<flat_sphere> spheres;
    std::vector<flat_node> nodes;
    std::vector<flat_material> materials;
    std::vector<flat_texture> textures;
    std::vector<unsigned char> pixels;
    int32_t root = -1;
    int32_t max_depth = 0;
};

#endif
