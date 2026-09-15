#ifndef MESHTRANSFORMH
#define MESHTRANSFORMH

#include <cmath>
#include <stdexcept>
#include <utility>
#include "geometry/triangle.h"

// Bake scale, then X/Y/Z rotations in degrees, then translation about the OBJ origin.
class mesh_transform {
    public:
        mesh_transform(vec3 scale = vec3(1,1,1), vec3 degrees = vec3(0,0,0),
                       vec3 translation = vec3(0,0,0)) : factors(scale), offset(translation) {
            unchanged = true;
            mirrored = false;
            for(int i = 0; i < 3; i++) {
                if(!std::isfinite(scale[i]) || scale[i] == 0 ||
                   !std::isfinite(degrees[i]) || !std::isfinite(translation[i])) {
                    throw std::invalid_argument("mesh transform requires finite values and nonzero scale");
                }
                double angle = std::remainder(double(degrees[i]),360.0)*3.14159265358979323846/180.0;
                sine[i] = std::sin(angle);
                cosine[i] = std::cos(angle);
                if(scale[i] != 1 || degrees[i] != 0 || translation[i] != 0) unchanged = false;
                if(scale[i] < 0) mirrored = !mirrored;
            }
        }

        void apply(triangle& face) const {
            if(unchanged) return;
            face.v0 = position(face.v0);
            face.v1 = position(face.v1);
            face.v2 = position(face.v2);
            if(face.smooth) {
                face.n0 = normal(face.n0);
                face.n1 = normal(face.n1);
                face.n2 = normal(face.n2);
            }
            if(mirrored) {
                std::swap(face.v1,face.v2);
                std::swap(face.n1,face.n2);
                std::swap(face.uv1,face.uv2);
            }
            float area = cross(face.v1-face.v0,face.v2-face.v0).squared_length();
            if(!(area > 0) || !std::isfinite(area)) {
                throw std::runtime_error("mesh transform collapses geometry or exceeds supported float range");
            }
        }

    private:
        vec3 factors, offset, sine, cosine;
        bool unchanged, mirrored;

        vec3 rotate(vec3 v) const {
            v = vec3(v.x(),cosine.x()*v.y()-sine.x()*v.z(),sine.x()*v.y()+cosine.x()*v.z());
            v = vec3(cosine.y()*v.x()+sine.y()*v.z(),v.y(),-sine.y()*v.x()+cosine.y()*v.z());
            return vec3(cosine.z()*v.x()-sine.z()*v.y(),sine.z()*v.x()+cosine.z()*v.y(),v.z());
        }

        vec3 position(const vec3& p) const {
            vec3 result = rotate(p*factors) + offset;
            for(int i = 0; i < 3; i++) {
                if(!std::isfinite(result[i])) throw std::runtime_error("transformed position exceeds float range");
            }
            return result;
        }

        vec3 normal(const vec3& n) const {
            // Double intermediates keep reciprocal scale and normalization in range.
            double x = double(n.x())/factors.x();
            double y = double(n.y())/factors.y();
            double z = double(n.z())/factors.z();
            double length = std::sqrt(x*x+y*y+z*z);
            if(!(length > 0) || !std::isfinite(length)) throw std::runtime_error("invalid transformed normal");
            return unit_vector(rotate(vec3(x/length,y/length,z/length)));
        }
};

#endif
