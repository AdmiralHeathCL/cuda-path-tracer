#ifndef DISPLAYCOLORH
#define DISPLAYCOLORH

#include <cmath>

inline int color_channel(float linear) {
    if(!(linear > 0)) return 0;
    if(linear >= 1) return 255;
    float gamma_corrected = sqrt(linear);
    return int(255.99 * gamma_corrected);
}

#endif
