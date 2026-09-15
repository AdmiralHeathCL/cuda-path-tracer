#ifndef BVHH
#define BVHH

#include <memory>
#include <stdexcept>
#include "core/hitable.h"
#include "bvh/aabb.h"

class bvh_node : public hitable {
public:
    bvh_node() : left(nullptr), right(nullptr) {}
    bvh_node(hitable **l, int n, float time0, float time1);
    bvh_node(const bvh_node&) = delete;
    bvh_node& operator=(const bvh_node&) = delete;
    bvh_node(bvh_node&&) = delete;
    bvh_node& operator=(bvh_node&&) = delete;

    virtual bool hit(
        const ray& r,
        float tmin,
        float tmax,
        hit_record& rec
    ) const;

    virtual bool bounding_box(
        float t0,
        float t1,
        aabb& box
    ) const;

    hitable *left;
    hitable *right;
    aabb box;

private:
    // Only recursively constructed nodes are owned here. Primitive pointers,
    // including pre-existing BVH roots passed in as objects, remain borrowed.
    std::unique_ptr<bvh_node> left_node;
    std::unique_ptr<bvh_node> right_node;
};

bool bvh_node::bounding_box(float t0, float t1, aabb& b) const {
    if(!left) return false;
    b = box;
    return true;
}

bool bvh_node::hit(const ray& r, float t_min, float t_max, hit_record& rec) const {
    if(!left) return false;
    if(box.hit(r, t_min, t_max)) {
        hit_record left_rec, right_rec;
        bool hit_left = left->hit(r, t_min, t_max, left_rec);
        bool hit_right = right->hit(r, t_min, t_max, right_rec);
        if(hit_left && hit_right) {
            if(left_rec.t < right_rec.t) {
                rec = left_rec;
            } else {
                rec = right_rec;
            }
            return true;
        } else if(hit_left) {
            rec = left_rec;
            return true;
        } else if(hit_right) {
            rec = right_rec;
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

int box_x_compare(const void *a, const void *b) {
    aabb box_left, box_right;

    hitable *ah = *(hitable**)a;
    hitable *bh = *(hitable**)b;

    if(!ah->bounding_box(0, 0, box_left) || !bh->bounding_box(0, 0, box_right)) {
        std::cerr << "no bounding box in bvh_node constructor\n";
        return 0;
    }

    if(box_left.min().x() - box_right.min().x() < 0.0) {
        return -1;
    } else if(box_left.min().x() > box_right.min().x()) {
        return 1;
    }
    return 0;
}

int box_y_compare(const void *a, const void *b) {
    aabb box_left, box_right;

    hitable *ah = *(hitable**)a;
    hitable *bh = *(hitable**)b;

    if(!ah->bounding_box(0, 0, box_left) || !bh->bounding_box(0, 0, box_right)) {
        std::cerr << "no bounding box in bvh_node constructor\n";
        return 0;
    }

    if(box_left.min().y() - box_right.min().y() < 0.0) {
        return -1;
    } else if(box_left.min().y() > box_right.min().y()) {
        return 1;
    }
    return 0;
}

int box_z_compare(const void *a, const void *b) {
    aabb box_left, box_right;

    hitable *ah = *(hitable**)a;
    hitable *bh = *(hitable**)b;

    if(!ah->bounding_box(0, 0, box_left) || !bh->bounding_box(0, 0, box_right)) {
        std::cerr << "no bounding box in bvh_node constructor\n";
        return 0;
    }

    if(box_left.min().z() - box_right.min().z() < 0.0) {
        return -1;
    } else if(box_left.min().z() > box_right.min().z()) {
        return 1;
    }
    return 0;
}

bvh_node::bvh_node(hitable **l, int n, float time0, float time1) {
    if(n <= 0 || !l) {
        throw std::invalid_argument("BVH requires a nonempty object array");
    }
    for(int i = 0; i < n; i++) {
        aabb bounds;
        if(!l[i] || !l[i]->bounding_box(time0, time1, bounds)) {
            throw std::invalid_argument("BVH requires non-null objects with bounding boxes");
        }
    }
    int axis = int(3 * drand48());

    if(axis == 0) {
        qsort(l, n, sizeof(hitable *), box_x_compare);
    } else if(axis == 1) {
        qsort(l, n, sizeof(hitable *), box_y_compare);
    } else {
        qsort(l, n, sizeof(hitable *), box_z_compare);

    }

    if(n == 1) {
        left = right = l[0];
    } else if(n == 2) {
        left = l[0];
        right = l[1];
    } else {
        left_node.reset(new bvh_node(l, n / 2, time0, time1));
        right_node.reset(new bvh_node(l + n / 2, n - n / 2, time0, time1));
        left = left_node.get();
        right = right_node.get();
    }

    aabb box_left, box_right;

    if(!left->bounding_box(time0, time1, box_left) || !right->bounding_box(time0, time1, box_right)) {
        throw std::invalid_argument("no bounding box in bvh_node constructor");
    }
    box = surrounding_box(box_left, box_right);
}

#endif
