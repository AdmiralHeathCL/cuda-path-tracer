#ifndef CUDAGLDISPLAYH
#define CUDAGLDISPLAYH

#include <memory>
#include <string>
class gl_display;
class cuda_renderer;

// Must be created/destroyed while the owning GL context is current.
class cuda_gl_display {
    public:
        static bool available(std::string& reason);
        cuda_gl_display();
        ~cuda_gl_display();
        cuda_gl_display(const cuda_gl_display&) = delete;
        cuda_gl_display& operator=(const cuda_gl_display&) = delete;
        void draw(gl_display& display, const cuda_renderer& renderer, int width, int height);
    private:
        struct implementation;
        std::unique_ptr<implementation> state;
};

#endif
