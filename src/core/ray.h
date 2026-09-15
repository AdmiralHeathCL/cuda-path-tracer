#ifndef RAYH
#define RAYH

#include "core/vec3.h"

class ray {
    public:


        RT_HD ray() {}
        RT_HD ray(const vec3& a, const vec3& b, float ti = 0.0) {
            A = a;
            B = b;
            _time = ti;
        }
        RT_HD vec3 origin() const {
            return A;
        }
        RT_HD vec3 direction() const {
            return B;
        }
        RT_HD float time() const {
            return _time;
        }

        RT_HD vec3 point_at_parameter(float t) const {
            return A + t*B;
        }

        vec3 A;
        vec3 B;
        float _time;
};

#endif
