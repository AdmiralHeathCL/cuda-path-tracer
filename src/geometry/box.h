#ifndef BOXH
#define BOXH

#include <array>
#include <memory>
#include "core/hitable.h"
#include "core/hitable_list.h"
#include "geometry/xy_rect.h"
#include "transforms/flip_normals.h"

class box: public hitable {
    public:
        box() : pmin(0,0,0), pmax(0,0,0) {}
        box(const vec3& p0, const vec3& p1, material *ptr);
        box(const box&) = delete;
        box& operator=(const box&) = delete;
        box(box&&) = delete;
        box& operator=(box&&) = delete;
        virtual bool hit(const ray& r, float t0, float t1, hit_record& rec) const;
        virtual bool bounding_box(float t0, float t1, aabb& bounds) const {
            if(!list_ptr) return false;
            bounds = aabb(pmin,pmax);
            return true;
        }
        vec3 pmin, pmax;

    private:
        // Wrappers and the hit list borrow faces; their owners outlive those views.
        std::array<std::unique_ptr<hitable>,6> faces;
        std::array<std::unique_ptr<flip_normals>,3> flipped;
        std::array<hitable*,6> views{};
        std::unique_ptr<hitable_list> list_ptr;
};

inline box::box(const vec3& p0, const vec3& p1, material *ptr) : pmin(p0), pmax(p1) {
    faces[0] = std::make_unique<xy_rect>(p0.x(),p1.x(),p0.y(),p1.y(),p1.z(),ptr);
    faces[1] = std::make_unique<xy_rect>(p0.x(),p1.x(),p0.y(),p1.y(),p0.z(),ptr);
    faces[2] = std::make_unique<xz_rect>(p0.x(),p1.x(),p0.z(),p1.z(),p1.y(),ptr);
    faces[3] = std::make_unique<xz_rect>(p0.x(),p1.x(),p0.z(),p1.z(),p0.y(),ptr);
    faces[4] = std::make_unique<yz_rect>(p0.y(),p1.y(),p0.z(),p1.z(),p1.x(),ptr);
    faces[5] = std::make_unique<yz_rect>(p0.y(),p1.y(),p0.z(),p1.z(),p0.x(),ptr);
    for(int i = 0; i < 3; i++) {
        flipped[i] = std::make_unique<flip_normals>(faces[2*i+1].get());
        views[2*i] = faces[2*i].get();
        views[2*i+1] = flipped[i].get();
    }
    list_ptr = std::make_unique<hitable_list>(views.data(),int(views.size()));
}

inline bool box::hit(const ray& r, float t0, float t1, hit_record& rec) const {
    return list_ptr && list_ptr->hit(r,t0,t1,rec);
}

#endif
