#ifndef TEXTUREH
#define TEXTUREH

#include <memory>
#include <cmath>
#include <stdexcept>
#include <string>
#include "stb_image.h"
#include "core/vec3.h"
#include "textures/perlin.h"

class texture {
    public:
    virtual ~texture() = default;
    virtual vec3 value(float u, float v, const vec3& p) const = 0;
};

class constant_texture : public texture {
    public:
        constant_texture() {}
        constant_texture(vec3 c) : color(c) {}

        virtual vec3 value(float u, float v, const vec3& p) const {
            return color;
        }

        vec3 color;
};

class checker_texture : public texture {
    public:
        checker_texture() {}
        checker_texture(texture *t0, texture *t1) : even(t0), odd(t1) {}

        virtual vec3 value(float u, float v, const vec3& p) const {
            float sines = sin(10*p.x()) * sin(10*p.y()) * sin(10*p.z());
            if(sines < 0) {
                return odd->value(u, v, p);
            } else {
                return even->value(u, v, p);
            }
        }
        texture *odd;
        texture *even;
};

class noise_texture : public texture {
    public:
        noise_texture() {}
        noise_texture(float sc) : scale(sc) {}
        virtual vec3 value(float u, float v, const vec3& p) const {
            return vec3(1,1,1)*0.5*(1 + sin(scale*p.z() + 10*noise.turb(p)));
        }
        perlin noise;
        float scale;
};

class image_texture : public texture {
public:
    image_texture() : data(nullptr), nx(0), ny(0) {}

    image_texture(unsigned char *pixels, int A, int B)
        : data(pixels), nx(A), ny(B) {}

    explicit image_texture(const std::string& filename) : data(nullptr), nx(0), ny(0) {
        int channels;
        owned_data.reset(stbi_load(filename.c_str(), &nx, &ny, &channels, STBI_rgb));
        data = owned_data.get();
        if(!data || nx <= 0 || ny <= 0) {
            throw std::runtime_error("could not load image: " + filename);
        }
    }

    virtual vec3 value(float u, float v, const vec3& p) const;

    unsigned char *data;
    int nx, ny;

private:
    struct image_deleter {
        void operator()(unsigned char *pixels) const { stbi_image_free(pixels); }
    };
    std::unique_ptr<unsigned char, image_deleter> owned_data;
};

vec3 image_texture::value(float u, float v, const vec3& p) const {
    if(!data || nx <= 0 || ny <= 0) return vec3(0,1,1);
    // Clamp before converting to integers, including very large or invalid UVs.
    u = std::fmin(1.0f, std::fmax(0.0f, u));
    v = std::fmin(1.0f, std::fmax(0.0f, v));
    int i = u * nx;
    int j = (1-v) * ny - 0.001;

    if(i < 0) i = 0;
    if(j < 0) j = 0;
    if(i > nx-1) i = nx-1;
    if(j > ny-1) j = ny-1;

    float r = int(data[3*i + 3*nx*j]) / 255.0;
    float g = int(data[3*i + 3*nx*j+1]) / 255.0;
    float b = int(data[3*i + 3*nx*j+2]) / 255.0;

    return vec3(r, g, b);
}

#endif
