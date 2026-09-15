#ifndef GLDISPLAYH
#define GLDISPLAYH

#include <GLFW/glfw3.h>
#include <GL/glext.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>

class display_unavailable : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
};

// One GLFW window/context. GL objects are destroyed before that context ends.
// Linux builds expose core GL entry points through GL_GLEXT_PROTOTYPES.
class gl_display {
    public:
        gl_display(int width, int height, bool hidden = false) {
            try {
                glfwSetErrorCallback([](int code, const char *message) { std::cerr << "GLFW " << code << ": " << message << "\n"; });
                if(!glfwInit()) throw display_unavailable("GLFW could not initialize a display");
                initialized = true;
                glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
                glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
                glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
                glfwWindowHint(GLFW_VISIBLE,hidden ? GLFW_FALSE : GLFW_TRUE);
                glfwWindowHint(GLFW_FOCUSED,hidden ? GLFW_FALSE : GLFW_TRUE);
                glfwWindowHint(GLFW_SAMPLES,0);
                glfwWindowHint(GLFW_RED_BITS,8); glfwWindowHint(GLFW_GREEN_BITS,8);
                glfwWindowHint(GLFW_BLUE_BITS,8); glfwWindowHint(GLFW_ALPHA_BITS,8);
                window = glfwCreateWindow(width,height,"Ray Tracer",nullptr,nullptr);
                if(!window) throw display_unavailable("GLFW could not create an OpenGL 3.3 window");
                glfwMakeContextCurrent(window);
                glfwSwapInterval(hidden ? 0 : 1);
                std::cerr << "OpenGL: " << glGetString(GL_VERSION) << "; renderer: " << glGetString(GL_RENDERER) << "\n";
                create_program();
                glGenVertexArrays(1,&vao);
                glGenTextures(1,&texture);
                glBindTexture(GL_TEXTURE_2D,texture);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
                glDisable(GL_DITHER); glDisable(GL_MULTISAMPLE); glDisable(GL_FRAMEBUFFER_SRGB);
                glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_texture_size);
                check("display setup");
            } catch(...) { release(); throw; }
        }
        ~gl_display() { release(); }
        gl_display(const gl_display&) = delete;
        gl_display& operator=(const gl_display&) = delete;
        GLFWwindow *handle() const { return window; }

        void draw(const std::vector<unsigned char>& pixels, int width, int height) {
            if(width <= 0 || height <= 0 || width > max_texture_size || height > max_texture_size ||
               uint64_t(width)*uint64_t(height)*4 != pixels.size()) throw std::invalid_argument("invalid display image dimensions/storage");
            present(pixels.data(),0,width,height);
        }
        void draw_buffer(unsigned int buffer, int width, int height) {
            if(!buffer || width <= 0 || height <= 0 || width > max_texture_size || height > max_texture_size) {
                throw std::invalid_argument("invalid GL pixel buffer dimensions");
            }
            present(nullptr,buffer,width,height);
        }
    private:
        void present(const unsigned char *pixels, unsigned int buffer, int width, int height) {
            glViewport(0,0,width,height);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT,1);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
            if(width != texture_width || height != texture_height) {
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
                texture_width = width; texture_height = height;
            }
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
            glUseProgram(program); glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES,0,3);
            check("texture upload/draw");
        }
    public:
        std::vector<unsigned char> read_back(int width, int height) const {
            if(width <= 0 || height <= 0 || uint64_t(width)*height > INT32_MAX) throw std::invalid_argument("invalid screenshot size");
            std::vector<unsigned char> bytes(size_t(width)*height*4);
            glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,bytes.data());
            check("framebuffer readback");
            return bytes;
        }

    private:
        static void check(const char *operation) {
            GLenum error = glGetError();
            if(error != GL_NO_ERROR) throw std::runtime_error(std::string(operation)+": OpenGL error "+std::to_string(error));
        }
        static GLuint compile(GLenum type, const char *source) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader,1,&source,nullptr); glCompileShader(shader);
            GLint success = 0; glGetShaderiv(shader,GL_COMPILE_STATUS,&success);
            if(!success) {
                char log[4096]{}; glGetShaderInfoLog(shader,sizeof(log),nullptr,log);
                glDeleteShader(shader);
                throw std::runtime_error(std::string("display shader compilation failed: ")+log);
            }
            return shader;
        }
        void create_program() {
            const char *vertex = R"(#version 330 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 2.0 : 0.0, (gl_VertexID == 2) ? 2.0 : 0.0);
    uv = p;
    gl_Position = vec4(p*2.0-1.0,0.0,1.0);
})";
            const char *fragment = R"(#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D image;
void main() { color = texture(image,uv); }
)";
            GLuint vs = compile(GL_VERTEX_SHADER,vertex), fs = 0;
            try {
                fs = compile(GL_FRAGMENT_SHADER,fragment);
                program = glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
                GLint success = 0; glGetProgramiv(program,GL_LINK_STATUS,&success);
                if(!success) {
                    char log[4096]{}; glGetProgramInfoLog(program,sizeof(log),nullptr,log);
                    throw std::runtime_error(std::string("display shader link failed: ")+log);
                }
                glUseProgram(program); glUniform1i(glGetUniformLocation(program,"image"),0);
            } catch(...) { glDeleteShader(vs); if(fs) glDeleteShader(fs); throw; }
            glDeleteShader(vs); glDeleteShader(fs);
        }
        void release() {
            if(window) {
                glfwMakeContextCurrent(window);
                if(texture) glDeleteTextures(1,&texture);
                if(vao) glDeleteVertexArrays(1,&vao);
                if(program) glDeleteProgram(program);
                glfwDestroyWindow(window); window = nullptr;
            }
            if(initialized) { glfwTerminate(); initialized = false; }
        }
        bool initialized = false;
        GLFWwindow *window = nullptr;
        GLuint program = 0, vao = 0, texture = 0;
        int texture_width = 0, texture_height = 0, max_texture_size = 0;
};

#endif
