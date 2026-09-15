#ifndef OBJLOADERH
#define OBJLOADERH

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include "geometry/triangle_mesh.h"
#include "transforms/mesh_transform.h"
#include "materials/material.h"
#include "tinyobjloader/tiny_obj_loader.h"

// Destruction order matters: mesh first, then materials, then their textures.
class obj_model : public hitable {
    public:
        std::vector<std::unique_ptr<texture>> textures;
        std::vector<std::unique_ptr<material>> materials;
        std::vector<std::string> material_names;
        std::string warnings;
        size_t source_vertices = 0;
        size_t shape_count = 0;
        std::unique_ptr<triangle_mesh> mesh;

        virtual bool hit(const ray& r, float t_min, float t_max, hit_record& rec) const override {
            return mesh && mesh->hit(r,t_min,t_max,rec);
        }

        virtual bool bounding_box(float t0, float t1, aabb& box) const override {
            return mesh && mesh->bounding_box(t0,t1,box);
        }
};

namespace obj_detail {

inline std::filesystem::path asset_path(std::string path) {
    for(char& c : path) if(c == '\\') c = '/';
    return std::filesystem::path(path);
}

// Texture references resolve relative to the MTL file that declares them.
class material_reader : public tinyobj::MaterialReader {
    public:
        explicit material_reader(std::filesystem::path directory) : base(directory) {}

        bool operator()(
            const std::string& name,
            std::vector<tinyobj::material_t> *materials,
            std::map<std::string,int> *mapping,
            std::string *warning,
            std::string *error
        ) override {
            std::filesystem::path path = base / asset_path(name);
            std::ifstream input(path);
            if(!input) {
                failure = "could not load MTL: " + path.string() + "\nCheck the OBJ mtllib filename and keep the referenced MTL beside the OBJ (or at that relative path).";
                if(error) *error += failure + "\n";
                return false;
            }
            size_t first = materials->size();
            tinyobj::LoadMtl(mapping,materials,&input,warning,error);
            for(size_t i = first; i < materials->size(); i++) directories.push_back(path.parent_path());
            return true;
        }

        std::filesystem::path base;
        std::vector<std::filesystem::path> directories;
        std::string failure;
};

inline bool finite(const vec3& v) {
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
}

inline vec3 attribute(const std::vector<tinyobj::real_t>& values, int index, int width) {
    if(index < 0 || size_t(index) >= values.size()/width) {
        throw std::runtime_error("OBJ attribute index is out of range");
    }
    size_t offset = size_t(index)*width;
    vec3 result(values[offset],values[offset+1],width == 3 ? values[offset+2] : 0);
    if(!finite(result)) throw std::runtime_error("OBJ contains a non-finite attribute");
    return result;
}

inline vec3 mtl_color(const tinyobj::real_t color[3], bool reflectance) {
    vec3 value(color[0],color[1],color[2]);
    if(!finite(value)) throw std::runtime_error("MTL contains a non-finite color");
    for(int i = 0; i < 3; i++) {
        value[i] = ffmax(0,value[i]);
        if(reflectance) value[i] = ffmin(1,value[i]);
    }
    return value;
}

class diffuse_map : public texture {
    public:
        diffuse_map(texture *pixels, vec3 color, const tinyobj::texture_option_t& options) :
            image(pixels), tint(color),
            scale(options.scale[0],options.scale[1],0),
            offset(options.origin_offset[0],options.origin_offset[1],0), clamp(options.clamp) {
            if(!finite(scale) || !finite(offset)) throw std::runtime_error("invalid MTL UV transform");
        }

        virtual vec3 value(float u, float v, const vec3& p) const override {
            u = u*scale.x() + offset.x();
            v = v*scale.y() + offset.y();
            if(!clamp) {
                u -= floor(u);
                v -= floor(v);
            }
            return tint * image->value(u,v,p);
        }

        texture *image;
        vec3 tint, scale, offset;
        bool clamp;
};

} // namespace obj_detail

inline std::unique_ptr<obj_model> load_obj(const std::string& filename, const mesh_transform& transform = mesh_transform()) {
    std::filesystem::path path = obj_detail::asset_path(filename);
    std::ifstream input(path);
    if(!input) throw std::runtime_error("could not load OBJ: " + filename);

    auto result = std::make_unique<obj_model>();
    tinyobj::attrib_t attributes;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> source_materials;
    obj_detail::material_reader reader(path.parent_path());
    std::string error;
    bool loaded = tinyobj::LoadObj(&attributes,&shapes,&source_materials,&result->warnings,
                                  &error,&input,&reader,true,false);
    if(!loaded || !reader.failure.empty() || !error.empty()) {
        throw std::runtime_error("OBJ load failed: " + filename + "\n" + (error.empty() ? reader.failure : error));
    }
    result->source_vertices = attributes.vertices.size()/3;
    result->shape_count = shapes.size();

    // The fallback occupies slot zero. MTL material i occupies slot i+1.
    result->textures.push_back(std::make_unique<constant_texture>(vec3(0.7,0.7,0.7)));
    result->materials.push_back(std::make_unique<lambertian>(result->textures.back().get()));
    result->material_names.push_back("<default>");
    std::map<std::filesystem::path,image_texture*> images;
    for(size_t i = 0; i < source_materials.size(); i++) {
        const auto& source = source_materials[i];
        vec3 diffuse = obj_detail::mtl_color(source.diffuse,true);
        vec3 emission = obj_detail::mtl_color(source.emission,false);
        bool glass = source.illum == 4 || source.illum == 6 || source.illum == 7 || source.illum == 9;
        bool mirror = source.illum == 3 || source.illum == 5 || source.illum == 8;
        if(emission.squared_length() > 0) {
            result->textures.push_back(std::make_unique<constant_texture>(emission));
            result->materials.push_back(std::make_unique<diffuse_light>(result->textures.back().get()));
        } else if(glass) {
            if(!(source.ior > 0) || !std::isfinite(source.ior)) throw std::runtime_error("invalid MTL refractive index");
            result->materials.push_back(std::make_unique<dielectric>(source.ior));
        } else if(mirror) {
            // Explicit mirror illumination modes map to the existing ideal metal.
            result->materials.push_back(std::make_unique<metal>(obj_detail::mtl_color(source.specular,true),0));
        } else {
            texture *albedo;
            if(source.diffuse_texname.empty()) {
                result->textures.push_back(std::make_unique<constant_texture>(diffuse));
                albedo = result->textures.back().get();
            } else {
                auto texture_path = (reader.directories.at(i) / obj_detail::asset_path(source.diffuse_texname)).lexically_normal();
                if(!images.count(texture_path)) {
                    auto pixels = std::make_unique<image_texture>(texture_path.string());
                    images[texture_path] = pixels.get();
                    result->textures.push_back(std::move(pixels));
                }
                result->textures.push_back(std::make_unique<obj_detail::diffuse_map>(images.at(texture_path),diffuse,source.diffuse_texopt));
                albedo = result->textures.back().get();
            }
            result->materials.push_back(std::make_unique<lambertian>(albedo));
        }
        result->material_names.push_back(source.name);
        if(source.dissolve != 1 || !source.alpha_texname.empty() || !source.bump_texname.empty() ||
           !source.normal_texname.empty() || !source.emissive_texname.empty() || source.metallic != 0 ||
           source.roughness != 0 || source.shininess != 1 || source.specular[0] != 0 ||
           source.specular[1] != 0 || source.specular[2] != 0) {
            result->warnings += "MTL '" + source.name + "': simplified material mapping; see README for unsupported shading properties.\n";
        }
    }

    std::vector<triangle> faces;
    size_t degenerate = 0, missing_uv = 0, flat_normals = 0;
    for(const auto& shape : shapes) {
        if(!shape.lines.indices.empty() || !shape.points.indices.empty()) {
            result->warnings += "Ignoring OBJ lines/points in '" + shape.name + "'.\n";
        }
        size_t offset = 0;
        for(size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            if(shape.mesh.num_face_vertices[f] != 3 || offset+3 > shape.mesh.indices.size()) {
                throw std::runtime_error("OBJ triangulation did not produce valid triangles");
            }
            vec3 positions[3], normals[3], uvs[3];
            bool has_normals = true, has_uvs = true;
            for(int v = 0; v < 3; v++) {
                const auto& index = shape.mesh.indices[offset+v];
                positions[v] = obj_detail::attribute(attributes.vertices,index.vertex_index,3);
                if(index.normal_index == -1) has_normals = false;
                else {
                    normals[v] = obj_detail::attribute(attributes.normals,index.normal_index,3);
                    float length = normals[v].length();
                    if(!(length > 0) || !std::isfinite(length)) has_normals = false;
                    else normals[v] /= length;
                }
                if(index.texcoord_index == -1) has_uvs = false;
                else uvs[v] = obj_detail::attribute(attributes.texcoords,index.texcoord_index,2);
            }
            offset += 3;
            float area_squared = cross(positions[1]-positions[0],positions[2]-positions[0]).squared_length();
            if(!std::isfinite(area_squared)) throw std::runtime_error("OBJ geometry exceeds supported float range");
            if(!(area_squared > 0)) { degenerate++; continue; }

            int id = shape.mesh.material_ids.at(f);
            if(id < -1 || id >= int(source_materials.size())) throw std::runtime_error("invalid OBJ material index");
            material *mat = result->materials[size_t(id+1)].get();
            triangle face(positions[0],positions[1],positions[2],mat);
            if(has_normals) {
                face.smooth = true;
                face.n0 = normals[0]; face.n1 = normals[1]; face.n2 = normals[2];
            } else flat_normals++;
            if(has_uvs) {
                face.uv0 = uvs[0]; face.uv1 = uvs[1]; face.uv2 = uvs[2];
            } else missing_uv++;
            transform.apply(face);
            faces.push_back(face);
        }
    }
    if(faces.empty()) throw std::runtime_error("OBJ contains no usable triangles: " + filename);
    if(degenerate) result->warnings += "Skipped " + std::to_string(degenerate) + " degenerate triangles.\n";
    if(flat_normals) result->warnings += "Using face normals for " + std::to_string(flat_normals) + " triangles without complete usable normals.\n";
    if(missing_uv) result->warnings += "Using canonical UVs for " + std::to_string(missing_uv) + " triangles without complete UVs.\n";
    result->mesh = std::make_unique<triangle_mesh>(std::move(faces));
    return result;
}

#endif
