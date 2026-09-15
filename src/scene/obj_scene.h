#ifndef OBJSCENEH
#define OBJSCENEH

#include "scene/obj_loader.h"
#include "geometry/sphere.h"

// Own the imported model and a simple preview stage for arbitrary-sized assets.
class obj_scene : public hitable {
    public:
        explicit obj_scene(const std::string& filename, float aspect,
                           const mesh_transform& transform = mesh_transform()) :
            model(load_obj(filename,transform)),
            floor_texture(vec3(0.55,0.55,0.55)), floor_material(&floor_texture),
            light_texture(vec3(3,3,3)), light_material(&light_texture) {
            aabb bounds;
            model->bounding_box(0,1,bounds);
            lookat = 0.5*bounds.min() + 0.5*bounds.max();
            float radius = ffmax(0.01f,0.5f*(bounds.max()-bounds.min()).length());
            if(!std::isfinite(radius)) throw std::runtime_error("OBJ bounds exceed camera range");
            float half_angle = atan(tan(20*M_PI/180)*ffmin(1,aspect));
            focus_dist = 1.2f*radius/sin(half_angle);
            lookfrom = lookat + focus_dist*unit_vector(vec3(0.6,0.35,1));
            ground = sphere(vec3(lookat.x(),bounds.min().y()-100*radius,lookat.z()),100*radius,&floor_material);
            light = sphere(lookat+radius*vec3(-1,3,3),1.5f*radius,&light_material);
            objects[0] = model.get();
            objects[1] = &ground;
            objects[2] = &light;
            root.reset(new bvh_node(objects,3,0,1));
        }

        virtual bool hit(const ray& r, float t_min, float t_max, hit_record& rec) const override {
            return root->hit(r,t_min,t_max,rec);
        }

        virtual bool bounding_box(float t0, float t1, aabb& box) const override {
            return root->bounding_box(t0,t1,box);
        }

        std::unique_ptr<obj_model> model;
        vec3 lookfrom, lookat;
        float focus_dist;

        const bvh_node *bvh_root() const { return root.get(); }

    private:
        constant_texture floor_texture;
        lambertian floor_material;
        constant_texture light_texture;
        diffuse_light light_material;
        sphere ground, light;
        hitable *objects[3];
        std::unique_ptr<bvh_node> root;
};

#endif
