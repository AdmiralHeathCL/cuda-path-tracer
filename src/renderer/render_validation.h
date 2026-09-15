#ifndef RENDERVALIDATIONH
#define RENDERVALIDATIONH

#include "flat/flat_validate.h"
#include "renderer/transport.h"

inline size_t checked_pixel_count(int width, int height) {
    if(width <= 0 || height <= 0 || uint64_t(width)*uint64_t(height) > uint64_t(INT32_MAX)) {
        throw std::invalid_argument("image dimensions exceed supported pixel count");
    }
    return size_t(width)*size_t(height);
}
inline void validate_render_mode(render_debug mode) {
    if(mode < DEBUG_SHADED || mode > DEBUG_INTERSECTIONS) throw std::invalid_argument("invalid render debug mode");
}
inline void validate_render_camera(const flat_camera& c) {
    for(vec3 v : {c.origin,c.lower_left_corner,c.horizontal,c.vertical,c.u,c.v}) {
        for(int axis = 0; axis < 3; axis++) if(!std::isfinite(v[axis])) throw std::invalid_argument("camera contains nonfinite values");
    }
    if(!std::isfinite(c.lens_radius) || c.lens_radius < 0 || !std::isfinite(c.time0) ||
       !std::isfinite(c.time1) || c.time1 < c.time0) throw std::invalid_argument("invalid camera lens/shutter");
}

#endif
