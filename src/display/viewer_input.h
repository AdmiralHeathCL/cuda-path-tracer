#ifndef VIEWERINPUTH
#define VIEWERINPUTH

#include <array>
#include <GLFW/glfw3.h>
#include "display/camera_controller.h"

// Callback state only: no rendering, allocations or exceptions in event handlers.
class viewer_input {
    public:
        void key(int code, int action) {
            if(code < 0 || code > GLFW_KEY_LAST || !focused) return;
            if(action == GLFW_RELEASE) { keys[size_t(code)] = false; return; }
            if(action != GLFW_PRESS) return;
            keys[size_t(code)] = true;
            if(code == GLFW_KEY_ESCAPE) {
                if(captured) capture(false);
                else close_requested = true;
            }
            if(code == GLFW_KEY_R) reset_pending = true;
            if(code >= GLFW_KEY_1 && code <= GLFW_KEY_6) mode_pending = code-GLFW_KEY_1;
        }
        void mouse_button(int button, int action) {
            if(button != GLFW_MOUSE_BUTTON_RIGHT) return;
            if(action == GLFW_RELEASE || (action == GLFW_PRESS && focused)) {
                captured = action == GLFW_PRESS;
                // Preserve movement already received while held, even when
                // a complete press/drag/release arrives between render frames.
            }
        }
        void focus(bool value) {
            focused = value;
            keys.fill(false); reset_pending = false; mode_pending = -1;
            capture(false);
        }
        void capture(bool value) {
            captured = value && focused;
            reset_cursor();
        }
        void reset_cursor() {
            have_cursor = false; dx = 0; dy = 0;
        }
        void cursor(double x, double y) {
            if(!focused || !std::isfinite(x) || !std::isfinite(y)) { have_cursor = false; return; }
            if(captured && have_cursor) {
                double next_dx = dx+x-last_x, next_dy = dy+y-last_y;
                if(std::isfinite(next_dx) && std::isfinite(next_dy)) { dx = next_dx; dy = next_dy; }
            }
            // Track normal cursor events even between drags. Button press uses
            // this event-ordered position, not a poll of a later physical position.
            last_x = x; last_y = y; have_cursor = true;
        }
        camera_input take() {
            camera_input result;
            if(focused) {
                result.forward = float(keys[GLFW_KEY_W])-float(keys[GLFW_KEY_S]);
                result.right = float(keys[GLFW_KEY_D])-float(keys[GLFW_KEY_A]);
                result.up = float(keys[GLFW_KEY_E])-float(keys[GLFW_KEY_Q]);
                result.fast = keys[GLFW_KEY_LEFT_SHIFT] || keys[GLFW_KEY_RIGHT_SHIFT];
                result.reset = reset_pending;
                result.mouse_dx = dx; result.mouse_dy = dy;
            }
            dx = 0; dy = 0; reset_pending = false;
            return result;
        }
        int take_mode() { int result = mode_pending; mode_pending = -1; return result; }
        bool has_focus() const { return focused; }
        bool mouse_captured() const { return captured; }
        bool should_close() const { return close_requested; }

    private:
        std::array<bool,GLFW_KEY_LAST+1> keys{};
        bool focused = true, captured = false, have_cursor = false;
        bool reset_pending = false, close_requested = false;
        int mode_pending = -1;
        double last_x = 0, last_y = 0, dx = 0, dy = 0;
};

#endif
