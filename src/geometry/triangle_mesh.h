#ifndef TRIANGLEMESHH
#define TRIANGLEMESHH

#include <climits>
#include <memory>
#include <utility>
#include <vector>
#include "bvh/bvh.h"
#include "geometry/triangle.h"

class triangle_mesh : public hitable {
    public:
        explicit triangle_mesh(std::vector<triangle> faces) : triangles(std::move(faces)) {
            if(triangles.empty()) return;
            if(triangles.size() > INT_MAX) {
                throw std::invalid_argument("too many triangles for the CPU BVH");
            }

            objects.reserve(triangles.size());
            for(triangle& face : triangles) objects.push_back(&face);
            root.reset(new bvh_node(objects.data(), int(objects.size()), 0, 1));
        }

        // The BVH points into our triangle storage, which must remain fixed.
        // Geometry is immutable after construction; materials are borrowed.
        triangle_mesh(const triangle_mesh&) = delete;
        triangle_mesh& operator=(const triangle_mesh&) = delete;
        triangle_mesh(triangle_mesh&&) = delete;
        triangle_mesh& operator=(triangle_mesh&&) = delete;

        virtual bool hit(
            const ray& r,
            float t_min,
            float t_max,
            hit_record& rec
        ) const override {
            return root && root->hit(r, t_min, t_max, rec);
        }

        virtual bool bounding_box(float t0, float t1, aabb& box) const override {
            return root && root->bounding_box(t0, t1, box);
        }

        size_t triangle_count() const {
            return triangles.size();
        }

        // Read-only access for reference traversal; geometry stays fixed after construction.
        const triangle& triangle_at(size_t index) const {
            return triangles.at(index);
        }

        const bvh_node *bvh_root() const { return root.get(); }

    private:
        std::vector<triangle> triangles;
        std::vector<hitable*> objects;
        std::unique_ptr<bvh_node> root;
};

#endif
