#include <cstdlib>
#include "display/gl_display.h"

int main() {
    try {
        gl_display display(19,13,true);
        size_t checks = 0;
        for(int pass = 0; pass < 3; pass++) {
            int requested_width = pass == 2 ? 23 : 19, requested_height = pass == 2 ? 17 : 13;
            glfwSetWindowSize(display.handle(),requested_width,requested_height);
            int w, h; double deadline = glfwGetTime()+2;
            do {
                glfwPollEvents(); glfwGetFramebufferSize(display.handle(),&w,&h);
                if(w == requested_width && h == requested_height) break;
                glfwWaitEventsTimeout(0.01);
            } while(glfwGetTime() < deadline);
            if(w != requested_width || h != requested_height) throw std::runtime_error("test window resize did not arrive");
            std::vector<unsigned char> expected(size_t(w)*h*4);
            for(int y = 0; y < h; y++) for(int x = 0; x < w; x++) {
                size_t i = 4*(size_t(y)*w+x);
                expected[i] = (x*17+y*31+pass*19)%256;
                expected[i+1] = (x*43+y*7)%256;
                expected[i+2] = (x*3+y*53)%256; expected[i+3] = 255;
            }
            if(pass == 1) {
                GLuint buffer; glGenBuffers(1,&buffer);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
                glBufferData(GL_PIXEL_UNPACK_BUFFER,GLsizeiptr(expected.size()),expected.data(),GL_STREAM_DRAW);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
                display.draw_buffer(buffer,w,h); glDeleteBuffers(1,&buffer);
            } else display.draw(expected,w,h);
            auto actual = display.read_back(w,h);
            for(size_t i = 0; i < expected.size(); i++) {
                checks++;
                if(std::abs(int(actual[i])-int(expected[i])) > 1) throw std::runtime_error("GL upload/quad/readback pixel mismatch");
            }
            glfwSwapBuffers(display.handle());
        }
        bool threw = false;
        try { display.draw({},19,13); } catch(const std::invalid_argument&) { threw = true; }
        if(!threw) throw std::runtime_error("invalid framebuffer storage accepted");
        std::cout << checks << " GL channel comparisons passed (within 1 byte), including texture update and resize\n";
    } catch(const display_unavailable& error) { std::cerr << error.what() << "\n"; return 77;
    } catch(const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
}
