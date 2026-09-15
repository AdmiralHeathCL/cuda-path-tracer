#ifndef DISPLAYPIXELSH
#define DISPLAYPIXELSH

#include <limits>
#include <stdexcept>
#include <vector>
#include "display/color.h"

// Tightly packed RGBA8, first row at the bottom, ready for a GL_RGBA upload.
// These bytes already have the same gamma conversion as the PPM writer.
inline std::vector<unsigned char> display_pixels(const std::vector<vec3>& linear) {
    if(linear.size() > std::numeric_limits<size_t>::max()/4) throw std::length_error("display image exceeds range");
    std::vector<unsigned char> bytes(linear.size()*4);
    for(size_t i = 0; i < linear.size(); i++) {
        bytes[4*i] = color_channel(linear[i].x());
        bytes[4*i+1] = color_channel(linear[i].y());
        bytes[4*i+2] = color_channel(linear[i].z());
        bytes[4*i+3] = 255;
    }
    return bytes;
}

#endif
