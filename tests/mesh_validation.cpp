#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include "scene/obj_loader.h"
#include "app/render_options.h"

using timer = std::chrono::steady_clock;

double milliseconds(timer::time_point begin, timer::time_point end) {
    return std::chrono::duration<double,std::milli>(end-begin).count();
}

struct sample_hit {
    bool hit = false;
    hit_record rec;
};

int main(int argc, char **argv) {
    try {
        if(argc < 2 || argc > 3) {
            std::cerr << "Usage: mesh_validation FILE.obj [ray-count >= 12]\n";
            return 1;
        }
        int count = argc == 3 ? parse_integer(argv[2],12) : 1024;
        srand48(7);
        auto begin = timer::now();
        auto model = load_obj(argv[1]);
        auto loaded = timer::now();
        std::cerr << model->warnings;
        const auto& mesh = *model->mesh;
        aabb bounds;
        mesh.bounding_box(0,1,bounds);
        vec3 center = 0.5*bounds.min()+0.5*bounds.max();
        vec3 extent = bounds.max()-bounds.min();
        float radius = extent.length();
        size_t bounds_errors = 0;
        for(size_t i = 0; i < mesh.triangle_count(); i++) {
            const triangle& face = mesh.triangle_at(i);
            for(vec3 p : {face.v0,face.v1,face.v2}) {
                for(int axis = 0; axis < 3; axis++) {
                    if(p[axis] < bounds.min()[axis] || p[axis] > bounds.max()[axis]) bounds_errors++;
                }
            }
        }

        std::mt19937 random(7);
        auto uniform = [&]() { return double(random())/4294967296.0; };
        std::vector<ray> rays;
        rays.reserve(count);
        for(int i = 0; i < count; i++) {
            vec3 origin, direction;
            if(i % 3 == 0) {
                // Axis-parallel rays from all six sides, including outside the silhouette.
                int axis = (i/3)%3;
                int sign = (i/9)%2 ? -1 : 1;
                for(int a = 0; a < 3; a++) origin[a] = center[a]+float(1.4*(uniform()-0.5))*extent[a];
                origin[axis] = center[axis]+sign*radius;
                direction = vec3(0,0,0);
                direction[axis] = -sign;
            } else if(i % 3 == 1) {
                // Start throughout the bounds, with non-unit directions.
                for(int a = 0; a < 3; a++) {
                    origin[a] = bounds.min()[a]+float(uniform())*extent[a];
                    direction[a] = float(uniform()-0.5);
                }
            } else {
                const triangle& face = mesh.triangle_at(random()%mesh.triangle_count());
                vec3 target = 0.23f*face.v0+0.31f*face.v1+0.46f*face.v2;
                direction = -unit_vector(cross(face.v1-face.v0,face.v2-face.v0));
                origin = target-radius*direction;
            }
            rays.emplace_back(origin,direction,0.5);
        }

        std::vector<sample_hit> accelerated(count), reference(count);
        auto bvh_begin = timer::now();
        for(int i = 0; i < count; i++) {
            accelerated[i].hit = mesh.hit(rays[i],0.001f,MAXFLOAT,accelerated[i].rec);
        }
        auto bvh_end = timer::now();
        for(int i = 0; i < count; i++) {
            float closest = MAXFLOAT;
            for(size_t j = 0; j < mesh.triangle_count(); j++) {
                hit_record rec;
                if(mesh.triangle_at(j).hit(rays[i],0.001f,closest,rec)) {
                    reference[i].hit = true;
                    reference[i].rec = rec;
                    closest = rec.t;
                }
            }
        }
        auto linear_end = timer::now();
        size_t mismatches = 0, hits = 0;
        for(int i = 0; i < count; i++) {
            const auto& a = accelerated[i];
            const auto& b = reference[i];
            bool mismatch = a.hit != b.hit;
            if(a.hit && b.hit) {
                hits++;
                float tolerance = 0.0001f*ffmax(1,std::fabs(b.rec.t));
                mismatch = std::fabs(a.rec.t-b.rec.t) > tolerance ||
                    (a.rec.p-b.rec.p).length() > 0.0001f*ffmax(1,radius) ||
                    (a.rec.normal-b.rec.normal).length() > 0.001f ||
                    std::fabs(a.rec.u-b.rec.u) > 0.001f || std::fabs(a.rec.v-b.rec.v) > 0.001f ||
                    a.rec.mat_ptr != b.rec.mat_ptr || !std::isfinite(a.rec.t) ||
                    !obj_detail::finite(a.rec.normal) || !std::isfinite(a.rec.u) || !std::isfinite(a.rec.v) ||
                    std::fabs(a.rec.normal.length()-1) > 0.001f;
            }
            if(mismatch) {
                if(mismatches < 10) std::cerr << "Hit mismatch at ray " << i << "\n";
                mismatches++;
            }
        }
        std::cout << std::fixed << std::setprecision(3)
                  << "{\n  \"triangles\": " << mesh.triangle_count()
                  << ",\n  \"shapes\": " << model->shape_count
                  << ",\n  \"rays\": " << count << ",\n  \"hits\": " << hits
                  << ",\n  \"mismatches\": " << mismatches << ",\n  \"bounds_errors\": " << bounds_errors
                  << ",\n  \"load_and_build_ms\": " << milliseconds(begin,loaded)
                  << ",\n  \"bvh_traversal_ms\": " << milliseconds(bvh_begin,bvh_end)
                  << ",\n  \"linear_traversal_ms\": " << milliseconds(bvh_end,linear_end) << "\n}\n";
        return mismatches || bounds_errors || !hits ? 1 : 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
