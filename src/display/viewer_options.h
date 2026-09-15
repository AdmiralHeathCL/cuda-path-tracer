#ifndef VIEWEROPTIONSH
#define VIEWEROPTIONSH

#include <vector>
#include "app/render_options.h"

struct viewer_options {
    render_options render;
    bool hidden = false;
    int frames = 0;
    std::string screenshot;
    std::string display = "auto";
};

inline viewer_options parse_viewer_options(int argc, char **argv) {
    viewer_options options;
    std::vector<std::string> forwarded = {"ray_tracer_viewer","--obj",RAY_TRACER_ASSET_DIR "/models/blender_sphere/sphere.obj",
        "--width","640","--height","400","--batch-samples","1","--seed","7","--lighting","mis"};
    for(int i = 1; i < argc; i++) {
        std::string argument = argv[i];
        if(argument == "--hidden") options.hidden = true;
        else if(argument == "--display") {
            if(i+1 >= argc) throw std::invalid_argument("missing value for --display");
            options.display = argv[++i];
            if(options.display != "auto" && options.display != "copy" && options.display != "interop") throw std::invalid_argument("display must be auto, copy or interop");
        } else if(argument == "--frames" || argument == "--screenshot") {
            if(i+1 >= argc) throw std::invalid_argument("missing value for "+argument);
            if(argument == "--frames") options.frames = parse_integer(argv[++i],1);
            else options.screenshot = argv[++i];
        } else if(argument == "--help" || argument == "--normals") forwarded.push_back(argument);
        else {
            int count = 1;
            if(argument == "--obj-scale" || argument == "--obj-rotate" || argument == "--obj-translate") count = 3;
            else if(argument != "--obj" && argument != "--backend" && argument != "--width" && argument != "--height" &&
                    argument != "--batch-samples" && argument != "--seed" && argument != "--debug" && argument != "--lighting") {
                throw std::invalid_argument("unknown viewer option: "+argument);
            }
            if(i+count >= argc) throw std::invalid_argument("missing value for "+argument);
            forwarded.push_back(argument);
            for(int n = 0; n < count; n++) forwarded.push_back(argv[++i]);
        }
    }
    std::vector<char*> arguments;
    for(auto& value : forwarded) arguments.push_back(value.data());
    options.render = parse_options(int(arguments.size()),arguments.data());
    if(options.hidden && !options.frames) throw std::invalid_argument("--hidden requires a bounded --frames count");
    return options;
}

#endif
