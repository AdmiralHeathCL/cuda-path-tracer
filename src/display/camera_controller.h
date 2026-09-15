#ifndef CAMERACONTROLLERH
#define CAMERACONTROLLERH

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "core/camera.h"

// Window-independent input for a Y-up camera. Mouse Y grows down.
struct camera_input {
    float forward = 0, right = 0, up = 0;
    double mouse_dx = 0, mouse_dy = 0;
    bool fast = false, reset = false;
};

class camera_controller {
    public:
        camera_controller(vec3 from, vec3 target, float fov, float aspect,
                          float aperture, float focus, float time0, float time1,
                          float speed = 1, float sensitivity = 0.1f, bool orbit = false) :
            initial(make_camera(from,target,fov,aspect,aperture,focus,time0,time1)), current(initial),
            focus_distance(focus), aspect_ratio(aspect), move_speed(speed), mouse_sensitivity(sensitivity),
            horizontal_size(initial.horizontal.length()), vertical_size(initial.vertical.length()),
            orbit_mode(orbit), initial_orbit_center(target), orbit_center(target),
            initial_radius((from-target).length()), orbit_radius(initial_radius) {
            if(!std::isfinite(speed) || speed <= 0 || !std::isfinite(sensitivity) || sensitivity <= 0) {
                throw std::invalid_argument("camera speed and sensitivity must be finite and positive");
            }
            initial_yaw = std::atan2(-double(initial.w.x()),double(initial.w.z()))*180/M_PI;
            initial_pitch = std::asin(std::clamp(-double(initial.w.y()),-1.0,1.0))*180/M_PI;
            if(std::abs(initial_pitch) > 89) throw std::invalid_argument("camera pitch must be within +/-89 degrees");
            yaw = initial_yaw; pitch = initial_pitch;
        }

        const camera& frame() const { return current; }

        // Preserve the current pose and vertical FOV. R will restore the
        // starting pose using this new aspect ratio as well.
        bool resize(float aspect) {
            if(!std::isfinite(aspect) || aspect <= 0) throw std::invalid_argument("camera aspect must be finite and positive");
            if(aspect == aspect_ratio) return false;
            float width = vertical_size*aspect;
            auto project = [&](camera c) {
                c.horizontal = width*c.u;
                c.lower_left_corner = c.origin-0.5f*c.horizontal-0.5f*c.vertical-focus_distance*c.w;
                validate_frame(c);
                return c;
            };
            camera next = project(current), next_initial = project(initial);
            current = next; initial = next_initial;
            horizontal_size = width; aspect_ratio = aspect;
            return true;
        }

        // Returns true only when the camera projection changes. The caller then
        // resets its retained renderer before adding any samples for this frame.
        bool update(const camera_input& input, double seconds) {
            if(!std::isfinite(seconds) || seconds < 0 || !std::isfinite(input.mouse_dx) || !std::isfinite(input.mouse_dy)) {
                throw std::invalid_argument("camera input/time must be finite and time nonnegative");
            }
            for(float value : {input.forward,input.right,input.up}) {
                if(!std::isfinite(value) || std::abs(value) > 1) throw std::invalid_argument("camera movement axes must be in [-1,1]");
            }
            if(input.reset) return reset();
            if(orbit_mode) return update_orbit(input,seconds);
            double next_yaw = yaw + input.mouse_dx*mouse_sensitivity;
            double next_pitch = pitch - input.mouse_dy*mouse_sensitivity;
            if(!std::isfinite(next_yaw) || !std::isfinite(next_pitch)) throw std::invalid_argument("camera rotation exceeds range");
            next_yaw = std::remainder(next_yaw,360.0);
            next_pitch = std::clamp(next_pitch,-89.0,89.0);
            camera next = current;
            if(next_yaw != yaw || next_pitch != pitch) {
                double y = next_yaw*M_PI/180, p = next_pitch*M_PI/180;
                next.w = -vec3(std::sin(y)*std::cos(p),std::sin(p),-std::cos(y)*std::cos(p));
                next.u = vec3(std::cos(y),0,std::sin(y));
                next.v = cross(next.w,next.u);
                next.horizontal = horizontal_size*next.u;
                next.vertical = vertical_size*next.v;
                next.lower_left_corner = next.origin-0.5f*next.horizontal-0.5f*next.vertical-focus_distance*next.w;
            }
            vec3 movement = -input.forward*next.w + input.right*next.u + vec3(0,input.up,0);
            float length = movement.length();
            if(length > 1) movement /= length;
            double distance = seconds*move_speed*(input.fast ? 4.0 : 1.0);
            if(length > 0 && seconds > 0) {
                if(!std::isfinite(distance) || distance > MAXFLOAT) throw std::invalid_argument("camera movement exceeds range");
                vec3 offset = float(distance)*movement;
                next.origin += offset;
                next.lower_left_corner += offset;
            }
            validate_frame(next);
            bool changed = !same_frame(current,next);
            current = next; yaw = next_yaw; pitch = next_pitch;
            return changed;
        }

        bool reset() {
            bool changed = !same_frame(current,initial);
            current = initial; yaw = initial_yaw; pitch = initial_pitch; orbit_radius = initial_radius;
            orbit_center = initial_orbit_center;
            return changed;
        }

    private:
        bool update_orbit(const camera_input& input, double seconds) {
            double distance = seconds*move_speed*(input.fast ? 4.0 : 1.0);
            if(!std::isfinite(distance) || distance > MAXFLOAT) throw std::invalid_argument("camera movement exceeds range");
            double radius = orbit_radius;
            double axes = std::max(1.0,std::sqrt(double(input.forward)*input.forward +
                double(input.right)*input.right + double(input.up)*input.up));
            distance /= axes;
            // Independent angles make diagonal drags deterministic regardless
            // of how the window system groups horizontal and vertical events.
            double next_yaw = yaw+input.mouse_dx*mouse_sensitivity;
            double next_pitch = pitch-input.mouse_dy*mouse_sensitivity;
            if(!std::isfinite(next_yaw) || !std::isfinite(next_pitch)) throw std::invalid_argument("camera rotation exceeds range");
            next_yaw = std::remainder(next_yaw,360.0);
            next_pitch = std::remainder(next_pitch,360.0);
            double next_radius = std::max(double(initial_radius)*0.01,radius-input.forward*distance);
            if(next_radius > MAXFLOAT) throw std::invalid_argument("camera orbit exceeds range");
            if(next_yaw == yaw && next_pitch == pitch && next_radius == radius &&
               (distance == 0 || (input.right == 0 && input.up == 0))) return false;
            camera next = current;
            if(next_yaw != yaw || next_pitch != pitch) {
                double y = next_yaw*M_PI/180, p = next_pitch*M_PI/180;
                next.w = -vec3(std::sin(y)*std::cos(p),std::sin(p),-std::cos(y)*std::cos(p));
                next.u = vec3(std::cos(y),0,std::sin(y));
                next.v = cross(next.w,next.u);
            }
            vec3 next_center = orbit_center+float(distance)*(input.right*next.u+input.up*next.v);
            finite_vector(next_center);
            next.origin = next_center+float(next_radius)*next.w;
            next.horizontal = horizontal_size*next.u;
            next.vertical = vertical_size*next.v;
            next.lower_left_corner = next.origin-0.5f*next.horizontal-0.5f*next.vertical-focus_distance*next.w;
            validate_frame(next);
            bool changed = !same_frame(current,next);
            current = next; yaw = next_yaw; pitch = next_pitch; orbit_radius = next_radius;
            orbit_center = next_center;
            return changed;
        }
        static bool same_vector(vec3 a, vec3 b) {
            return a.x() == b.x() && a.y() == b.y() && a.z() == b.z();
        }
        static bool same_frame(const camera& a, const camera& b) {
            return same_vector(a.origin,b.origin) && same_vector(a.lower_left_corner,b.lower_left_corner) &&
                   same_vector(a.horizontal,b.horizontal) && same_vector(a.vertical,b.vertical) &&
                   same_vector(a.u,b.u) && same_vector(a.v,b.v) && same_vector(a.w,b.w);
        }
        static void finite_vector(vec3 v) {
            for(int axis = 0; axis < 3; axis++) {
                if(!std::isfinite(v[axis])) throw std::invalid_argument("camera contains nonfinite values");
            }
        }
        static void validate_frame(const camera& c) {
            for(vec3 v : {c.origin,c.lower_left_corner,c.horizontal,c.vertical,c.u,c.v,c.w}) finite_vector(v);
            for(float size : {c.horizontal.length(),c.vertical.length()}) {
                if(!std::isfinite(size) || size <= 0) throw std::invalid_argument("camera projection exceeds range");
            }
        }
        static camera make_camera(vec3 from, vec3 target, float fov, float aspect,
                                  float aperture, float focus, float time0, float time1) {
            finite_vector(from); finite_vector(target);
            for(float value : {fov,aspect,aperture,focus,time0,time1}) {
                if(!std::isfinite(value)) throw std::invalid_argument("camera settings must be finite");
            }
            float distance = (target-from).length();
            if(fov <= 0 || fov >= 179 || aspect <= 0 || aperture < 0 || focus <= 0 || time1 < time0 ||
               !std::isfinite(distance) || distance <= 0) throw std::invalid_argument("invalid camera projection/settings");
            camera result(from,target,vec3(0,1,0),fov,aspect,aperture,focus,time0,time1);
            validate_frame(result);
            return result;
        }

        camera initial, current;
        float focus_distance, aspect_ratio, move_speed, mouse_sensitivity, horizontal_size, vertical_size;
        double initial_yaw, initial_pitch, yaw, pitch;
        bool orbit_mode;
        vec3 initial_orbit_center, orbit_center;
        float initial_radius;
        double orbit_radius;
};

#endif
