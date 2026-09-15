#include <cmath>
#include <iostream>
#include "bvh/bvh.h"
#include "core/hitable_list.h"
#include "support/reference_render.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "geometry/xy_rect.h"
#include "transforms/flip_normals.h"
#include "transforms/rotate_y.h"
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

bool close(float a, float b, float tolerance = 0.00001f) {
    return fabs(a-b) <= tolerance;
}

bool close(const vec3& a, const vec3& b, float tolerance = 0.00001f) {
    return (a-b).length() <= tolerance;
}

void compare_hit(const hit_record& a, const hit_record& b) {
    check(close(a.t, b.t), "closest distance agrees");
    check(close(a.p, b.p), "hit position agrees");
    check(close(a.normal, b.normal), "normal agrees");
    check(close(a.u, b.u) && close(a.v, b.v), "UVs agree");
    check(a.mat_ptr == b.mat_ptr, "material identity agrees");
}

void test_intersections(material *mat) {
    triangle tri(vec3(0,0,0), vec3(1,0,0), vec3(0,1,0), mat);
    ray front(vec3(0.25, 0.25, 2), vec3(0,0,-2), 0.4);
    hit_record rec;
    bool hit = tri.hit(front, 0, 10, rec);
    check(hit, "front face is hit with a non-unit ray");
    if(hit) {
        check(close(rec.t, 1), "analytical distance");
        check(close(rec.p, vec3(0.25,0.25,0)), "analytical position");
        check(close(rec.normal, vec3(0,0,1)), "winding gives outward unit normal");
        check(close(rec.u, 0.25) && close(rec.v, 0.25), "canonical UV interpolation");
        check(rec.mat_ptr == mat, "triangle material assigned");
    }

    hit = tri.hit(ray(vec3(0.25,0.25,-2), vec3(0,0,2)), 0, 10, rec);
    check(hit, "back face is hit");
    if(hit) check(close(rec.normal, vec3(0,0,1)), "back face keeps outward normal");
    triangle reversed(tri.v0, tri.v2, tri.v1, mat);
    hit = reversed.hit(front, 0, 10, rec);
    check(hit, "reversed winding still intersects");
    if(hit) check(close(rec.normal, vec3(0,0,-1)), "reversed winding reverses normal");

    check(!tri.hit(ray(vec3(0.8,0.8,1), vec3(0,0,-1)), 0, 10, rec), "outside triangle misses");
    check(!tri.hit(ray(vec3(-0.1,0.2,1), vec3(0,0,-1)), 0, 10, rec), "negative barycentric misses");
    check(!tri.hit(ray(vec3(0.2,0.2,1), vec3(1,0,0)), 0, 10, rec), "parallel ray misses");
    check(!tri.hit(ray(vec3(0.2,0.2,0), vec3(1,0,0)), 0, 10, rec), "coplanar ray misses");
    check(!tri.hit(ray(vec3(0.2,0.2,1), vec3(0,0,0)), 0, 10, rec), "zero direction misses");
    check(!tri.hit(ray(vec3(0.2,0.2,1), vec3(0,0,1)), 0, 10, rec), "hit behind ray excluded");
    check(!tri.hit(front, 1, 10, rec), "exclusive t_min");
    check(!tri.hit(front, 0, 1, rec), "exclusive t_max");
    check(!tri.hit(front, 2, 1, rec), "empty interval misses");

    vec3 boundary_points[] = {tri.v0, tri.v1, tri.v2, vec3(0.5,0.5,0), vec3(0.5,0,0)};
    for(const vec3& p : boundary_points) {
        check(tri.hit(ray(p + vec3(0,0,1), vec3(0,0,-1)), 0, 10, rec), "edge or vertex included");
    }

    triangle degenerate(vec3(0,0,0), vec3(1,0,0), vec3(2,0,0), mat);
    check(!degenerate.hit(front, 0, 10, rec), "collinear triangle rejected");
    triangle collapsed(vec3(0,0,0), vec3(0,0,0), vec3(0,0,0), mat);
    check(!collapsed.hit(front, 0, 10, rec), "collapsed triangle rejected");

    triangle small(vec3(0,0,0), vec3(0.00001,0,0), vec3(0,0.00001,0), mat);
    check(small.hit(ray(vec3(0.000002,0.000002,1), vec3(0,0,-1)), 0, 10, rec), "small triangle is not rejected by fixed determinant epsilon");
    hit = tri.hit(ray(vec3(0.25,0.25,1), vec3(0,0,-0.00000001)), 0, 200000000, rec);
    check(hit, "small non-unit direction is supported");
    if(hit) check(close(rec.p, vec3(0.25,0.25,0)), "small direction preserves hit point");
}

void test_smooth_normals(material *mat) {
    vec3 n0(0,0,1);
    vec3 n1 = unit_vector(vec3(1,0,1));
    vec3 n2 = unit_vector(vec3(0,1,1));
    triangle tri(vec3(0,0,0), vec3(1,0,0), vec3(0,1,0), n0, n1, n2, mat);
    ray r(vec3(0.25,0.25,1), vec3(0,0,-1));
    hit_record rec;
    bool hit = tri.hit(r, 0, 10, rec);
    check(hit, "smooth triangle hit");
    if(hit) {
        check(close(rec.normal, unit_vector(0.5*n0 + 0.25*n1 + 0.25*n2)), "barycentric normal interpolation");
        check(close(rec.normal.length(), 1), "interpolated normal normalized");
    }
    tri.n0 = -n0;
    tri.n1 = -n1;
    tri.n2 = -n2;
    hit = tri.hit(r, 0, 10, rec);
    check(hit && rec.normal.z() > 0, "smooth normals respect winding");
    tri.n0 = tri.n1 = tri.n2 = vec3(0,0,0);
    hit = tri.hit(r, 0, 10, rec);
    check(hit && close(rec.normal, vec3(0,0,1)), "zero interpolated normal falls back to face normal");
}

void test_bounds(material *mat) {
    triangle triangles[] = {
        triangle(vec3(0,0,0), vec3(1,0,0), vec3(0,1,0), mat),
        triangle(vec3(0,0,0), vec3(0,1,0), vec3(0,0,1), mat),
        triangle(vec3(0,0,0), vec3(0,0,1), vec3(1,0,0), mat),
        triangle(vec3(100000,100000,100000), vec3(100001,100000,100000), vec3(100000,100001,100000), mat)
    };
    for(triangle& tri : triangles) {
        aabb bounds;
        check(tri.bounding_box(0, 1, bounds), "triangle supplies bounds");
        for(int axis = 0; axis < 3; axis++) {
            check(bounds.min()[axis] < bounds.max()[axis], "bounds have nonzero thickness");
            check(bounds.min()[axis] < ffmin(tri.v0[axis], ffmin(tri.v1[axis], tri.v2[axis])), "minimum padded beyond every vertex");
            check(bounds.max()[axis] > ffmax(tri.v0[axis], ffmax(tri.v1[axis], tri.v2[axis])), "maximum padded beyond every vertex");
        }
        vec3 normal = unit_vector(cross(tri.v1-tri.v0, tri.v2-tri.v0));
        hitable *objects[] = {&tri};
        bvh_node tree(objects, 1, 0, 1);
        vec3 points[] = {tri.v0, tri.v1, tri.v2, (tri.v0+tri.v1+tri.v2)/3};
        for(const vec3& p : points) {
            ray r(p + normal, -normal);
            hit_record rec;
            check(bounds.hit(r, 0, 10), "bounds accept perpendicular vertex and interior rays");
            check(tree.hit(r, 0, 10, rec), "one-leaf BVH preserves planar triangle hits");
        }
    }
}

void test_existing_primitives(material *mat) {
    triangle lower(vec3(0,0,0), vec3(1,0,0), vec3(0,1,0), mat);
    triangle upper(vec3(1,1,0), vec3(0,1,0), vec3(1,0,0), mat);
    hitable *parts[] = {&lower, &upper};
    bvh_node tree(parts, 2, 0, 1);
    xy_rect rectangle(0, 1, 0, 1, 0, mat);
    for(int j = 0; j <= 10; j++) {
        for(int i = 0; i <= 10; i++) {
            ray r(vec3(i/10.0f,j/10.0f,2), vec3(0,0,-1));
            hit_record a, b;
            bool ha = tree.hit(r, 0, 10, a);
            bool hb = rectangle.hit(r, 0, 10, b);
            check(ha && hb, "two triangles cover the existing rectangle including the shared edge");
            if(ha && hb) {
                check(close(a.t,b.t) && close(a.p,b.p) && close(a.normal,b.normal), "rectangle provides independent triangle hit reference");
            }
        }
    }
}

void test_closest_hits(material *mat, material *other) {
    triangle far(vec3(-1,-1,0), vec3(1,-1,0), vec3(0,1,0), other);
    triangle near(vec3(-1,-1,1), vec3(1,-1,1), vec3(0,1,1), mat);
    sphere ball(vec3(2,0,0), 0.5, other);
    triangle side(vec3(-3,-1,0), vec3(-1,-1,0), vec3(-2,1,0), mat);
    hitable *objects[] = {&far, &ball, &near, &side};
    hitable_list list(objects, 4);
    bvh_node tree(objects, 4, 0, 1);
    hit_record rec;
    bool hit = tree.hit(ray(vec3(0,0,3), vec3(0,0,-1)), 0, 10, rec);
    check(hit, "overlapping triangles hit");
    if(hit) check(close(rec.t, 2) && rec.mat_ptr == mat, "nearest triangle wins independently of input order");

    for(int j = -12; j <= 12; j++) {
        for(int i = -35; i <= 30; i++) {
            ray r(vec3(i/10.0f,j/10.0f,3), vec3(0.03,0.02,-2), 0.7);
            hit_record a, b;
            bool ha = list.hit(r, 0.001, 100, a);
            bool hb = tree.hit(r, 0.001, 100, b);
            check(ha == hb, "mixed sphere/triangle list and recursive BVH agree on hits and misses");
            if(ha && hb) compare_hit(a, b);
        }
    }
}

void test_transforms_and_materials(material *mat) {
    triangle tri(vec3(0,0,0), vec3(1,0,0), vec3(0,1,0), mat);
    rotate_y rotated(&tri, 90);
    translate moved(&rotated, vec3(2,3,4));
    ray r(vec3(3,3.25,3.75), vec3(-1,0,0), 0.6);
    hit_record rec;
    bool hit = moved.hit(r, 0, 10, rec);
    check(hit, "existing wrappers transform a triangle");
    if(hit) {
        check(close(rec.p, vec3(2,3.25,3.75)), "transformed position");
        check(close(rec.normal, vec3(1,0,0)), "transformed outward normal");
        check(close(rec.u,0.25) && close(rec.v,0.25), "transforms preserve UVs");
    }
    aabb bounds;
    check(moved.bounding_box(0,1,bounds) && bounds.hit(r,0,10), "transformed bounds accept hit");
    flip_normals flipped(&tri);
    hit = flipped.hit(ray(vec3(0.25,0.25,1), vec3(0,0,-1)), 0, 10, rec);
    check(hit && close(rec.normal,vec3(0,0,-1)), "flip_normals works with triangle");

    dielectric glass(1.5);
    triangle glass_tri(vec3(-10,-10,0), vec3(10,-10,0), vec3(0,10,0), &glass);
    ray exiting(vec3(0,0,-1), vec3(0.8,0,0.6), 0.8);
    hit = glass_tri.hit(exiting,0,10,rec);
    check(hit, "glass exit ray hits back face");
    if(hit) {
        ray scattered;
        vec3 attenuation;
        check(glass.scatter(exiting,rec,attenuation,scattered), "existing dielectric scatters triangle hit");
        check(scattered.direction().z() < 0, "outward normal enables total internal reflection");
        check(close(scattered.time(),0.8), "scattering preserves ray time");
    }

    constant_texture emission(vec3(0.2,0.4,0.8));
    diffuse_light light(&emission);
    triangle emitter(tri.v0,tri.v1,tri.v2,&light);
    check(close(color(ray(vec3(0.25,0.25,1),vec3(0,0,-1)),&emitter,0),emission.color), "existing integrator renders emissive triangle");
}

int main() {
    constant_texture albedo(vec3(0.5,0.5,0.5));
    lambertian matte(&albedo);
    metal reflective(vec3(0.8,0.8,0.8),0);
    srand48(1234);
    test_intersections(&matte);
    test_smooth_normals(&matte);
    test_bounds(&matte);
    test_existing_primitives(&matte);
    test_closest_hits(&matte,&reflective);
    test_transforms_and_materials(&matte);
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
