#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#include "cuda/cuda_gl_display.h"
#include "cuda/cuda_renderer.h"
#include "display/gl_display.h"

namespace {
void checked_interop(cudaError_t result, const char *operation) {
    if(result != cudaSuccess) throw std::runtime_error(std::string(operation)+": "+cudaGetErrorString(result));
}
}

bool cuda_gl_display::available(std::string& reason) {
    unsigned int count = 0; int devices[16], current = -1;
    cudaError_t error = cudaGLGetDevices(&count,devices,16,cudaGLDeviceListCurrentFrame);
    if(error != cudaSuccess) {
        reason = std::string("current OpenGL context cannot share CUDA resources: ")+cudaGetErrorString(error);
        cudaGetLastError(); return false;
    }
    error = cudaGetDevice(&current);
    if(error != cudaSuccess) { reason = cudaGetErrorString(error); cudaGetLastError(); return false; }
    for(unsigned int i = 0; i < count; i++) if(devices[i] == current) { reason.clear(); return true; }
    reason = "OpenGL context and CUDA renderer do not use the same NVIDIA device";
    return false;
}

struct cuda_gl_display::implementation {
    GLuint buffer = 0;
    cudaGraphicsResource *resource = nullptr;
    size_t bytes = 0;
    ~implementation() {
        if(resource) cudaGraphicsUnregisterResource(resource);
        if(buffer) glDeleteBuffers(1,&buffer);
    }
};

cuda_gl_display::cuda_gl_display() {
    std::string reason;
    if(!available(reason)) throw std::runtime_error("CUDA/OpenGL interop unavailable: "+reason);
    state = std::make_unique<implementation>();
    glGenBuffers(1,&state->buffer);
    if(!state->buffer || glGetError() != GL_NO_ERROR) throw std::runtime_error("could not create GL pixel buffer");
}
cuda_gl_display::~cuda_gl_display() = default;

void cuda_gl_display::draw(gl_display& display, const cuda_renderer& renderer, int width, int height) {
    if(width <= 0 || height <= 0 || uint64_t(width)*height > INT32_MAX) throw std::invalid_argument("invalid interop dimensions");
    size_t bytes = size_t(width)*height*4;
    if(bytes != state->bytes || !state->resource) {
        if(state->resource) {
            checked_interop(cudaGraphicsUnregisterResource(state->resource),"unregister old pixel buffer");
            state->resource = nullptr;
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,state->buffer);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,GLsizeiptr(bytes),nullptr,GL_STREAM_DRAW);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        if(glGetError() != GL_NO_ERROR) throw std::runtime_error("could not allocate GL pixel buffer");
        checked_interop(cudaGraphicsGLRegisterBuffer(&state->resource,state->buffer,cudaGraphicsRegisterFlagsWriteDiscard),"register pixel buffer");
        state->bytes = bytes;
    }
    checked_interop(cudaGraphicsMapResources(1,&state->resource),"map pixel buffer");
    try {
        unsigned char *pixels = nullptr; size_t capacity = 0;
        checked_interop(cudaGraphicsResourceGetMappedPointer(reinterpret_cast<void**>(&pixels),&capacity,state->resource),"get mapped pixel buffer");
        renderer.write_rgba(pixels,capacity);
    } catch(...) { cudaGraphicsUnmapResources(1,&state->resource); throw; }
    checked_interop(cudaGraphicsUnmapResources(1,&state->resource),"unmap pixel buffer");
    display.draw_buffer(state->buffer,width,height);
}
