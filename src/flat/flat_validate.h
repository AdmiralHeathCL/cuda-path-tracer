#ifndef FLATVALIDATEH
#define FLATVALIDATEH

#include <cmath>
#include <functional>
#include <stdexcept>
#include "flat/flat_data.h"

// Host validation applies to scene arrays held immutable during traversal.
inline void validate_flat_scene(const flat_scene& scene) {
    auto require = [](bool ok) { if(!ok) throw std::runtime_error("invalid flat scene data"); };
    auto valid = [](int32_t id, size_t count) { return id >= 0 && size_t(id) < count; };
    auto finite = [](const vec3& p) { return std::isfinite(p.x()) && std::isfinite(p.y()) && std::isfinite(p.z()); };
    for(const auto& t : scene.textures) {
        require(finite(t.color) && finite(t.scale) && finite(t.offset));
        require(t.type >= FLAT_CONSTANT && t.type <= FLAT_MAP);
        if(t.type == FLAT_IMAGE) {
            require(t.width > 0 && t.height > 0 && t.byte_offset >= 0);
            uint64_t end = uint64_t(t.byte_offset)+3*uint64_t(t.width)*uint64_t(t.height);
            require(end <= scene.pixels.size());
        } else if(t.type == FLAT_MAP) {
            require(valid(t.image_id,scene.textures.size()));
            require(scene.textures[t.image_id].type == FLAT_IMAGE && (t.clamp == 0 || t.clamp == 1));
        }
    }
    for(const auto& m : scene.materials) {
        require(m.type >= FLAT_DIFFUSE && m.type <= FLAT_LIGHT && finite(m.albedo));
        require(std::isfinite(m.fuzz) && std::isfinite(m.ref_idx));
        if(m.type == FLAT_DIFFUSE || m.type == FLAT_LIGHT) require(valid(m.texture_id,scene.textures.size()));
        if(m.type == FLAT_GLASS) require(m.ref_idx > 0);
    }
    for(const auto& t : scene.triangles) {
        require(valid(t.material_id,scene.materials.size()) && (t.smooth == 0 || t.smooth == 1));
        require(finite(t.v0) && finite(t.v1) && finite(t.v2) && finite(t.n0) && finite(t.n1) && finite(t.n2));
        require(finite(t.uv0) && finite(t.uv1) && finite(t.uv2));
    }
    for(const auto& s : scene.spheres) {
        require(valid(s.material_id,scene.materials.size()) && finite(s.center) && std::isfinite(s.radius) && s.radius > 0);
    }
    if(scene.root == -1) {
        require(scene.nodes.empty() && scene.triangles.empty() && scene.spheres.empty() && scene.max_depth == 0);
        return;
    }
    require(valid(scene.root,scene.nodes.size()));
    std::vector<int> heights(scene.nodes.size(),0);
    std::vector<bool> active(scene.nodes.size(),false);
    size_t visited = 0;
    std::function<int(int32_t,int)> visit = [&](int32_t id, int depth) {
        require(valid(id,scene.nodes.size()) && depth <= flat_stack_capacity && !active[id]);
        if(heights[id]) { require(depth+heights[id]-1 <= flat_stack_capacity); return heights[id]; }
        active[id] = true;
        visited++;
        const auto& n = scene.nodes[id];
        require(finite(n.minimum) && finite(n.maximum));
        for(int axis = 0; axis < 3; axis++) require(n.minimum[axis] <= n.maximum[axis]);
        int height = 1;
        if(n.type == FLAT_BRANCH) {
            int left = visit(n.left,depth+1);
            int right = n.left == n.right ? left : visit(n.right,depth+1);
            height += std::max(left,right);
            for(int32_t child : {n.left,n.right}) {
                for(int axis = 0; axis < 3; axis++) {
                    require(n.minimum[axis] <= scene.nodes[child].minimum[axis] && n.maximum[axis] >= scene.nodes[child].maximum[axis]);
                }
            }
        } else if(n.type == FLAT_TRIANGLE) {
            require(valid(n.primitive,scene.triangles.size()));
            const auto& t = scene.triangles[n.primitive];
            for(vec3 p : {t.v0,t.v1,t.v2}) {
                for(int axis = 0; axis < 3; axis++) require(p[axis] >= n.minimum[axis] && p[axis] <= n.maximum[axis]);
            }
        } else if(n.type == FLAT_SPHERE) {
            require(valid(n.primitive,scene.spheres.size()));
            const auto& s = scene.spheres[n.primitive];
            for(int axis = 0; axis < 3; axis++) require(s.center[axis]-s.radius >= n.minimum[axis] && s.center[axis]+s.radius <= n.maximum[axis]);
        } else require(false);
        active[id] = false;
        heights[id] = height;
        return height;
    };
    int depth = visit(scene.root,1);
    require(depth == scene.max_depth && visited == scene.nodes.size());
}

#endif
