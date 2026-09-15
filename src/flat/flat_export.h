#ifndef FLATEXPORTH
#define FLATEXPORTH

#include <unordered_map>
#include <unordered_set>
#include <limits>
#include "flat/flat_data.h"
#include "flat/flat_validate.h"
#include "scene/obj_scene.h"
#include "core/camera.h"

// RTTI, allocation, and source pointers are confined to this host-side exporter.
class flat_exporter {
    public:
        flat_scene build(const hitable *world) {
            scene = flat_scene();
            material_ids.clear(); texture_ids.clear(); active.clear();
            scene.root = object(world,1);
            validate_flat_scene(scene);
            return std::move(scene);
        }

    private:
        flat_scene scene;
        std::unordered_map<const material*,int32_t> material_ids;
        std::unordered_map<const texture*,int32_t> texture_ids;
        std::unordered_set<const hitable*> active;

        static int32_t index(size_t size) {
            if(size >= size_t(INT32_MAX)) throw std::runtime_error("flat array exceeds 32-bit index range");
            return int32_t(size);
        }

        int32_t texture_id(const texture *source) {
            if(!source) throw std::runtime_error("cannot flatten null texture");
            auto found = texture_ids.find(source);
            if(found != texture_ids.end()) return found->second;
            flat_texture record{vec3(0,0,0),vec3(1,1,0),vec3(0,0,0),FLAT_CONSTANT,-1,0,0,0,1};
            if(auto value = dynamic_cast<const constant_texture*>(source)) {
                record.color = value->color;
            } else if(auto value = dynamic_cast<const image_texture*>(source)) {
                if(!value->data || value->nx <= 0 || value->ny <= 0) throw std::runtime_error("cannot flatten empty image");
                uint64_t bytes = uint64_t(value->nx)*uint64_t(value->ny)*3;
                if(bytes + scene.pixels.size() >= uint64_t(INT32_MAX)) throw std::runtime_error("flat image storage exceeds 32-bit offsets");
                record.type = FLAT_IMAGE;
                record.width = value->nx;
                record.height = value->ny;
                record.byte_offset = index(scene.pixels.size());
                scene.pixels.insert(scene.pixels.end(),value->data,value->data+size_t(bytes));
            } else if(auto value = dynamic_cast<const obj_detail::diffuse_map*>(source)) {
                // Import maps have an image child, not an arbitrary recursive graph.
                if(!dynamic_cast<const image_texture*>(value->image)) throw std::runtime_error("flat diffuse map requires an image child");
                record.type = FLAT_MAP;
                record.image_id = texture_id(value->image);
                record.color = value->tint;
                record.scale = value->scale;
                record.offset = value->offset;
                record.clamp = value->clamp;
            } else throw std::runtime_error("unsupported flat texture: only constant, image and OBJ diffuse maps are supported");
            int32_t id = index(scene.textures.size());
            scene.textures.push_back(record);
            texture_ids[source] = id;
            return id;
        }

        int32_t material_id(const material *source) {
            if(!source) throw std::runtime_error("cannot flatten null material");
            auto found = material_ids.find(source);
            if(found != material_ids.end()) return found->second;
            flat_material record{vec3(0,0,0),0,1,FLAT_DIFFUSE,-1};
            if(auto value = dynamic_cast<const lambertian*>(source)) record.texture_id = texture_id(value->albedo);
            else if(auto value = dynamic_cast<const metal*>(source)) {
                record.type = FLAT_METAL; record.albedo = value->albedo; record.fuzz = value->fuzz;
            } else if(auto value = dynamic_cast<const dielectric*>(source)) {
                record.type = FLAT_GLASS; record.ref_idx = value->ref_idx;
            } else if(auto value = dynamic_cast<const diffuse_light*>(source)) {
                record.type = FLAT_LIGHT; record.texture_id = texture_id(value->emit);
            } else throw std::runtime_error("unsupported flat material");
            int32_t id = index(scene.materials.size());
            scene.materials.push_back(record);
            material_ids[source] = id;
            return id;
        }

        int32_t object(const hitable *source, int depth) {
            if(!source) return -1;
            if(!active.insert(source).second) throw std::runtime_error("cycle in source scene");
            int32_t result;
            if(auto value = dynamic_cast<const obj_scene*>(source)) result = object(value->bvh_root(),depth);
            else if(auto value = dynamic_cast<const obj_model*>(source)) result = object(value->mesh.get(),depth);
            else if(auto value = dynamic_cast<const triangle_mesh*>(source)) result = object(value->bvh_root(),depth);
            else {
                if(depth > flat_stack_capacity) throw std::runtime_error("source BVH exceeds flat traversal stack capacity");
                aabb bounds;
                if(!source->bounding_box(0,1,bounds)) {
                    if(dynamic_cast<const bvh_node*>(source)) { active.erase(source); return -1; }
                    throw std::runtime_error("flat object has no bounds");
                }
                if(!obj_detail::finite(bounds.min()) || !obj_detail::finite(bounds.max())) throw std::runtime_error("nonfinite flat bounds");
                result = index(scene.nodes.size());
                scene.nodes.push_back({bounds.min(),bounds.max(),FLAT_BRANCH,-1,-1,-1});
                scene.max_depth = std::max(scene.max_depth,depth);
                if(auto value = dynamic_cast<const bvh_node*>(source)) {
                    int32_t left = object(value->left,depth+1);
                    int32_t right = value->left == value->right ? left : object(value->right,depth+1);
                    if(left < 0 || right < 0) throw std::runtime_error("empty child in source BVH");
                    scene.nodes[result].left = left;
                    scene.nodes[result].right = right;
                } else if(auto value = dynamic_cast<const triangle*>(source)) {
                    flat_triangle face{value->v0,value->v1,value->v2,value->n0,value->n1,value->n2,
                                       value->uv0,value->uv1,value->uv2,material_id(value->mat_ptr),value->smooth};
                    scene.nodes[result].type = FLAT_TRIANGLE;
                    scene.nodes[result].primitive = index(scene.triangles.size());
                    scene.triangles.push_back(face);
                } else if(auto value = dynamic_cast<const sphere*>(source)) {
                    scene.nodes[result].type = FLAT_SPHERE;
                    scene.nodes[result].primitive = index(scene.spheres.size());
                    scene.spheres.push_back({value->center,value->radius,material_id(value->mat_ptr)});
                } else throw std::runtime_error("unsupported flat geometry: use a baked OBJ mesh or static spheres in the existing BVH");
            }
            active.erase(source);
            return result;
        }
};

inline flat_scene flatten_scene(const hitable *world) { return flat_exporter().build(world); }
inline flat_camera flatten_camera(const camera& c) {
    return {c.origin,c.lower_left_corner,c.horizontal,c.vertical,c.u,c.v,c.lens_radius,c.time0,c.time1};
}

#endif
