#ifndef RENDEROPTIONSH
#define RENDEROPTIONSH

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include "transforms/mesh_transform.h"
#include "renderer/transport.h"

struct render_options {
    int width = 500;
    int height = 500;
    int samples = 100;
    int seed = 7;
    int batch_samples = 16;
    render_debug debug = DEBUG_SHADED;
    std::string obj_file = RAY_TRACER_ASSET_DIR "/models/blender_sphere/sphere.obj";
    std::string backend = "cuda";
    vec3 obj_scale = vec3(1,1,1);
    vec3 obj_rotate = vec3(0,0,0);
    vec3 obj_translate = vec3(0,0,0);
    bool normals = false;
    bool help = false;
    bool direct_light = true;
};

inline int parse_integer(const char *value, int minimum) {
    char *end;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if(errno || end == value || *end != '\0' || parsed < minimum || parsed > INT_MAX) {
        throw std::invalid_argument(std::string("invalid integer: ") + value);
    }
    return int(parsed);
}

inline float parse_float(const char *value) {
    char *end;
    errno = 0;
    float parsed = strtof(value,&end);
    if(errno || end == value || *end != '\0' || !std::isfinite(parsed)) {
        throw std::invalid_argument(std::string("invalid finite number: ") + value);
    }
    return parsed;
}

inline render_options parse_options(int argc, char **argv) {
    render_options options;
    bool has_transform = false;
    bool has_obj = false;
    for(int i = 1; i < argc; i++) {
        std::string argument = argv[i];
        if(argument == "--help") {
            options.help = true;
        } else if(argument == "--lighting") {
            if(i+1 >= argc) throw std::invalid_argument("missing value for --lighting");
            std::string value = argv[++i];
            if(value != "classic" && value != "mis") throw std::invalid_argument("lighting must be classic or mis");
            options.direct_light = value == "mis";
        } else if(argument == "--normals") {
            options.normals = true;
            options.debug = DEBUG_NORMALS;
        } else if(argument == "--obj-scale" || argument == "--obj-rotate" || argument == "--obj-translate") {
            if(i + 3 >= argc) throw std::invalid_argument("expected three values for " + argument);
            vec3 value;
            for(int axis = 0; axis < 3; axis++) value[axis] = parse_float(argv[++i]);
            if(argument == "--obj-scale") options.obj_scale = value;
            else if(argument == "--obj-rotate") options.obj_rotate = value;
            else options.obj_translate = value;
            has_transform = true;
        } else if(argument == "--width" ||
                  argument == "--height" || argument == "--samples" || argument == "--seed" || argument == "--obj" || argument == "--backend" || argument == "--debug" || argument == "--batch-samples") {
            if(i + 1 >= argc) throw std::invalid_argument("missing value for " + argument);
            const char *value = argv[++i];
            if(argument == "--obj") {
                options.obj_file = value;
                has_obj = true;
            } else if(argument == "--backend") options.backend = value;
            else if(argument == "--debug") {
                std::string mode = value;
                if(mode == "shaded") options.debug = DEBUG_SHADED;
                else if(mode == "normals") options.debug = DEBUG_NORMALS;
                else if(mode == "triangle-id") options.debug = DEBUG_TRIANGLE;
                else if(mode == "material-id") options.debug = DEBUG_MATERIAL;
                else if(mode == "bvh") options.debug = DEBUG_BVH;
                else if(mode == "intersections") options.debug = DEBUG_INTERSECTIONS;
                else throw std::invalid_argument("unknown debug mode: "+mode);
                options.normals = options.debug == DEBUG_NORMALS;
            } else if(argument == "--batch-samples") {
                options.batch_samples = parse_integer(value,1);
            } else if(argument == "--width") options.width = parse_integer(value, 1);
            else if(argument == "--height") options.height = parse_integer(value, 1);
            else if(argument == "--samples") options.samples = parse_integer(value, 1);
            else options.seed = parse_integer(value, 0);
        } else {
            throw std::invalid_argument("unknown option: " + argument);
        }
    }

    if(options.obj_file.empty()) throw std::invalid_argument("OBJ filename must not be empty");
    if(has_transform && !has_obj) throw std::invalid_argument("OBJ transforms require --obj FILE");
    mesh_transform validation(options.obj_scale,options.obj_rotate,options.obj_translate);
    if(options.backend != "cuda") throw std::invalid_argument("CUDA is the only rendering backend");
    return options;
}

#endif
