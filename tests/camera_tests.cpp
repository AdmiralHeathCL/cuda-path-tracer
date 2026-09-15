#include <iostream>
#include <limits>
#include "display/camera_controller.h"
#include "flat/flat_export.h"
#include "support/portable_renderer.h"

int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    checks++;
    if(!ok) { failures++; if(failures < 20) std::cerr << "FAIL: " << message << "\n"; }
}
bool close(float a, float b) { return fabs(a-b) < 0.0001f; }
bool close(vec3 a, vec3 b) { return (a-b).length() < 0.0001f; }
bool equal(vec3 a, vec3 b) { return a.x() == b.x() && a.y() == b.y() && a.z() == b.z(); }
bool equal(const std::vector<vec3>& a, const std::vector<vec3>& b) {
    if(a.size() != b.size()) return false;
    for(size_t i = 0; i < a.size(); i++) if(!equal(a[i],b[i])) return false;
    return true;
}
camera_controller basic() { return camera_controller(vec3(0,0,5),vec3(0,0,0),40,1.5,0.2,5,0.2,0.8,2,1); }

void test_motion() {
    auto control = basic();
    camera initial = control.frame();
    camera_input input;
    check(!control.update(input,1) && !control.reset(),"idle input and initial reset do not invalidate samples");
    input.forward = 1;
    check(!control.update(input,0),"zero time does not move");
    check(control.update(input,0.5),"forward motion changes camera");
    check(close(control.frame().origin,vec3(0,0,4)),"W follows view direction in world units/second");
    check(close(control.frame().lower_left_corner-initial.lower_left_corner,vec3(0,0,-1)),"movement translates film plane with eye");
    input.forward = -1; control.update(input,0.5);
    check(close(control.frame().origin,initial.origin),"S reverses W");
    input = {}; input.right = 1; input.up = 1;
    control.reset(); control.update(input,0.5);
    check(close((control.frame().origin-initial.origin).length(),1),"diagonal movement cannot exceed speed");
    check(close(control.frame().origin,initial.origin+vec3(1,1,0)/sqrtf(2)),"right plus world-up direction");
    input = {}; input.right = -1; input.fast = true;
    control.reset(); control.update(input,0.5);
    check(close(control.frame().origin,vec3(-4,0,5)),"Shift multiplies movement by four");
    input = {}; input.up = -1; control.reset(); control.update(input,0.5);
    check(close(control.frame().origin,vec3(0,-1,5)),"Q moves world-down");
    auto whole = basic(), divided = basic(); input = {}; input.forward = 1;
    whole.update(input,1);
    for(int i = 0; i < 20; i++) divided.update(input,0.05);
    check(close(whole.frame().origin,divided.frame().origin),"movement is independent of frame subdivision");
}

void test_look() {
    auto control = basic(); camera initial = control.frame();
    camera_input input; input.mouse_dx = 90;
    check(control.update(input,0),"mouse input works without multiplying by frame time");
    check(close(-control.frame().w,vec3(1,0,0)),"mouse right turns toward +X");
    input = {}; input.forward = 1; control.update(input,0.5);
    check(close(control.frame().origin,vec3(1,0,5)),"movement follows rotated view");
    control.reset(); input = {}; input.mouse_dy = -30; control.update(input,0);
    check(close(-control.frame().w,vec3(0,0.5,-sqrtf(0.75))),"mouse up pitches upward");
    input = {}; input.up = 1; control.update(input,0.5);
    check(close(control.frame().origin,vec3(0,1,5)),"vertical movement stays world-up while pitched");
    input = {}; input.mouse_dy = -1000; control.update(input,0);
    check(close(-control.frame().w.y(),sin(89*M_PI/180)),"pitch clamps before pole singularity");
    check(!control.update(input,0),"outward input at pitch limit does not reset accumulation again");
    control.reset(); input = {}; input.mouse_dx = 360;
    check(!control.update(input,0),"whole yaw revolution preserves camera");
    for(int i = 0; i < 1000; i++) {
        input.mouse_dx = 7.3; input.mouse_dy = 0.08;
        control.update(input,0.016);
        const camera& c = control.frame();
        check(close(c.u.length(),1) && close(c.v.length(),1) && close(c.w.length(),1),"basis stays unit length");
        check(close(dot(c.u,c.v),0) && close(dot(c.u,c.w),0) && close(dot(c.v,c.w),0),"basis stays orthogonal");
        check(close(c.horizontal.length(),initial.horizontal.length()) && close(c.vertical.length(),initial.vertical.length()),"look preserves FOV/aspect");
        check(close(c.lower_left_corner+0.5f*c.horizontal+0.5f*c.vertical-c.origin,-5*c.w),"center ray points along camera forward");
        check(c.lens_radius == initial.lens_radius && c.time0 == initial.time0 && c.time1 == initial.time1,"lens and shutter remain unchanged");
    }
    input.reset = true; input.forward = 1;
    check(control.update(input,1),"R restores original camera and overrides movement/look");
    check(equal(control.frame().origin,initial.origin) && equal(control.frame().lower_left_corner,initial.lower_left_corner),"reset restores exact original projection");
}

void test_orbit() {
    vec3 center(7,-2,3);
    camera_controller control(center+vec3(0,0,5),center,40,1.5,0.2,5,0.2,0.8,2,1,true);
    camera initial = control.frame();
    check(!control.update({},1),"idle orbit leaves original projection exact");
    camera_input input; input.mouse_dx = 90;
    check(control.update(input,0),"orbit works without frame time");
    check(close(control.frame().origin,center+vec3(-5,0,0)),"quarter orbit moves eye around translated model center");
    check(close(control.frame().origin-5*control.frame().w,center),"orbit center stays on center ray");
    for(int i = 0; i < 1000; i++) {
        input.mouse_dx = 7.3; input.mouse_dy = 0.08;
        control.update(input,0.016);
        check(close((control.frame().origin-center).length(),5),"repeated orbit preserves radius");
        check(close(unit_vector(center-control.frame().origin),-control.frame().w),"repeated orbit keeps model centered");
        const auto& c = control.frame();
        check(close(c.u.length(),1) && close(c.v.length(),1) && close(c.w.length(),1) &&
              close(dot(c.u,c.v),0) && close(dot(c.u,c.w),0) && close(dot(c.v,c.w),0),"mixed orbit keeps an orthonormal camera basis");
    }
    control.reset(); input = {}; input.forward = 1; control.update(input,0.5);
    check(close(control.frame().origin,center+vec3(0,0,4)),"orbit W dollies toward model");
    input.forward = -1; control.update(input,0.5);
    check(close(control.frame().origin,initial.origin),"orbit S reverses dolly");
    input = {}; input.right = 1; control.update(input,0.5);
    check(close(control.frame().origin,initial.origin+vec3(1,0,0)) && equal(control.frame().w,initial.w),"D pans horizontally without turning");
    input = {}; input.up = 1; control.update(input,0.5);
    check(close(control.frame().origin,initial.origin+vec3(1,1,0)) && equal(control.frame().v,initial.v),"E pans vertically without turning");
    vec3 panned_center = center+vec3(1,1,0);
    input = {}; input.mouse_dx = 90; control.update(input,0);
    check(close(control.frame().origin-5*control.frame().w,panned_center),"right drag orbits panned view center without snapping back to model");
    input = {}; input.forward = 1; control.update(input,100);
    check(close((control.frame().origin-panned_center).length(),0.05),"dolly stops before crossing current view center");
    check(!control.update(input,100),"dolly at near limit stops resetting samples");
    control.reset(); input = {}; input.mouse_dy = 90; control.update(input,0);
    check(close(control.frame().w,vec3(0,1,0)),"vertical orbit reaches pole");
    input.mouse_dy = 45; check(control.update(input,0),"vertical orbit continues past pole without sticking");
    check(control.frame().w.z() < 0 && control.frame().v.y() < 0,"orbit can pass above and behind current view");
    auto tilted = control.frame(); input = {}; input.up = 1; control.update(input,0.5);
    check(close(control.frame().origin,tilted.origin+tilted.v) && equal(control.frame().w,tilted.w),"vertical pan follows screen-up after rotation");
    check(!control.update({},0.5),"idle rotated view preserves samples");
    control.reset(); input = {}; input.mouse_dy = 30;
    for(int i = 0; i < 12; i++) control.update(input,0);
    check(close(control.frame().origin,initial.origin) && close(control.frame().v,initial.v),"full vertical orbit returns to original view without a pole clamp");
    auto before = control.frame(); control.resize(2);
    check(close(control.frame().origin,before.origin) && close(control.frame().w,before.w),"orbit resize preserves pose");
    control.reset(); check(equal(control.frame().origin,initial.origin),"orbit reset restores original eye");
    input = {}; input.mouse_dx = 90; control.update(input,0);
    check(close((control.frame().origin-center).length(),5),"reset restores orbit radius after dolly");
    check(close(control.frame().origin-5*control.frame().w,center),"reset restores original pivot after panning");
    auto valid = control;
    input = {}; input.right = 1;
    bool threw = false;
    try { control.update(input,std::numeric_limits<double>::max()); } catch(const std::invalid_argument&) { threw = true; }
    check(threw && equal(control.frame().origin,valid.frame().origin),"invalid pan leaves camera unchanged");
    input = {}; input.mouse_dx = 20;
    control.update(input,0); valid.update(input,0);
    check(equal(control.frame().origin,valid.frame().origin),"invalid pan also leaves orbit pivot unchanged");
}

void test_orbit_axes() {
    vec3 center(3,-2,7);
    vec3 from = center+5*unit_vector(vec3(0.6,0.35,1));
    auto make = [&]() { return camera_controller(from,center,40,1.5,0,5,0,1,2,1,true); };
    auto horizontal = make(); camera start = horizontal.frame();
    camera_input input; input.mouse_dx = 30; horizontal.update(input,0);
    const auto& h = horizontal.frame();
    check(close(h.origin.y(),start.origin.y()) && close(h.w.y(),start.w.y()),"horizontal drag at tilted starting view preserves orbit height and pitch");
    check(close(h.u.y(),0),"horizontal orbit does not introduce camera roll");
    auto vertical = make(); input = {}; input.mouse_dy = 30; vertical.update(input,0);
    check(close(vertical.frame().u,start.u),"vertical drag preserves horizontal orbit direction");
    check(!close(vertical.frame().origin.y(),start.origin.y()),"vertical drag changes orbit height");
    auto combined = make(), separate = make(), subdivided = make();
    input = {}; input.mouse_dx = 30; input.mouse_dy = -20; combined.update(input,0);
    input.mouse_dy = 0; separate.update(input,0);
    input.mouse_dx = 0; input.mouse_dy = -20; separate.update(input,0);
    check(close(combined.frame().origin,separate.frame().origin) && close(combined.frame().v,separate.frame().v),"diagonal drag equals independent horizontal and vertical orbit");
    input.mouse_dx = 3; input.mouse_dy = -2;
    for(int i = 0; i < 10; i++) subdivided.update(input,0.1);
    check(close(combined.frame().origin,subdivided.frame().origin) && close(combined.frame().v,subdivided.frame().v),"drag does not depend on render frame grouping or duration");
    input.mouse_dx = -30; input.mouse_dy = 20; combined.update(input,0);
    check(close(combined.frame().origin,start.origin) && close(combined.frame().v,start.v),"reversing both drag axes restores view without residual roll");
    camera_controller fine(vec3(0,0,5),vec3(0,0,0),40,1.5,0,5,0,1,2,0.01f,true);
    input = {}; input.mouse_dx = 100; fine.update(input,0);
    check(close(fine.frame().w.x(),-sin(M_PI/180)) && close(fine.frame().w.y(),0),"100 cursor units at viewer sensitivity rotates horizontally by one degree");
}

void test_rejected_input() {
    auto control = basic();
    auto reject = [&](camera_input input, double seconds) {
        camera before = control.frame(); bool threw = false;
        try { control.update(input,seconds); } catch(const std::invalid_argument&) { threw = true; }
        check(threw,"invalid input throws");
        check(equal(before.origin,control.frame().origin) && equal(before.lower_left_corner,control.frame().lower_left_corner),"invalid update leaves projection intact");
    };
    reject({},-1); reject({},std::numeric_limits<double>::infinity());
    camera_input input; input.right = 2; reject(input,1);
    input = {}; input.mouse_dx = std::numeric_limits<double>::quiet_NaN(); reject(input,1);
    input = {}; input.forward = 1; reject(input,std::numeric_limits<double>::max());
    for(int test = 0; test < 6; test++) {
        bool threw = false;
        try {
            camera_controller invalid(vec3(0,0,0),test == 0 ? vec3(0,0,0) : test == 1 ? vec3(0,1,0) : vec3(0,0,-1),
                test == 2 ? 180 : 40,test == 3 ? 0 : 1,0,test == 4 ? 0 : 1,0,1,test == 5 ? 0 : 1);
        } catch(const std::invalid_argument&) { threw = true; }
        check(threw,"invalid initial camera settings rejected");
    }
}

void test_accumulation() {
    srand48(7);
    obj_scene source(RAY_TRACER_ASSET_DIR "/models/blender_sphere/sphere.obj",19.0f/13);
    auto scene = flatten_scene(&source);
    camera original(source.lookfrom,source.lookat,vec3(0,1,0),40,19.0f/13,0,source.focus_dist,0,1);
    camera_controller control(source.lookfrom,source.lookat,40,19.0f/13,0,source.focus_dist,0,1);
    check(equal(original.lower_left_corner,control.frame().lower_left_corner),"initial controller camera exactly reuses original projection");
    portable_renderer renderer(scene,flatten_camera(control.frame()),19,13,7);
    renderer.add_samples(3);
    auto advance = [&](const camera_input& input) {
        if(control.update(input,0.5)) renderer.reset(flatten_camera(control.frame()));
    };
    advance({});
    check(renderer.sample_count() == 3,"idle frame preserves accumulated samples");
    renderer.add_samples(4); auto initial = renderer.image();
    camera_input input; input.right = 1; input.mouse_dx = 10; advance(input);
    check(renderer.sample_count() == 0,"camera change clears sample count before next render");
    for(vec3 pixel : renderer.image()) check(equal(pixel,vec3(0,0,0)),"camera change clears all old radiance");
    renderer.add_samples(7);
    portable_renderer fresh(scene,flatten_camera(control.frame()),19,13,7);
    fresh.add_samples(7);
    check(equal(renderer.image(),fresh.image()),"moved frame exactly matches a fresh render, with no ghosting");
    check(!equal(renderer.image(),initial),"movement produces a different scene view");
    input = {}; input.reset = true; advance(input); renderer.add_samples(7);
    check(equal(renderer.image(),initial),"R restores original seeded image exactly");
}

int main() {
    test_motion(); test_look(); test_orbit(); test_orbit_axes(); test_rejected_input(); test_accumulation();
    std::cout << checks << " camera checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
