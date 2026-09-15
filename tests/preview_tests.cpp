#include "support/portable_renderer.h"
#include <iostream>
#include <limits>
#include "display/preview_session.h"

int checks = 0, failures = 0;
void check(bool ok, const char *message) {
    checks++;
    if(!ok) { failures++; if(failures < 20) std::cerr << "FAIL: " << message << "\n"; }
}
bool close(vec3 a, vec3 b) { return (a-b).length() < 0.0001f; }
bool close(float a, float b) { return fabs(a-b) < 0.0001f; }

void test_pixels() {
    float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
    auto bytes = display_pixels({vec3(0,0.25,1),vec3(-1,nan,inf),vec3(0.01,0.04,0.09),vec3(1,0,0)});
    check(bytes == std::vector<unsigned char>({0,127,255,255,0,0,255,255,25,51,76,255,255,0,0,255}),
          "RGBA order, alpha, gamma, saturation, invalid channels and row order");
    check(display_pixels({}).empty(),"empty pixel conversion");
}

void test_resize_camera() {
    camera_controller control(vec3(0,0,5),vec3(0,0,0),40,1.5,0.2,5,0.2,0.8);
    camera_input move; move.right = 1; move.mouse_dx = 250; move.mouse_dy = -100;
    control.update(move,0.5);
    camera before = control.frame();
    check(control.resize(2),"aspect change reprojects camera");
    const camera& c = control.frame();
    check(close(c.origin,before.origin) && close(c.w,before.w) && close(c.u,before.u),"resize preserves moved pose");
    check(close(c.vertical,before.vertical) && close(c.horizontal.length()/c.vertical.length(),2),"resize preserves vertical FOV and changes horizontal extent");
    check(c.lens_radius == before.lens_radius && c.time0 == before.time0 && c.time1 == before.time1,"resize preserves aperture and shutter");
    check(!control.resize(2),"same aspect is a no-op");
    control.reset();
    check(close(control.frame().origin,vec3(0,0,5)) && close(control.frame().horizontal.length()/control.frame().vertical.length(),2),"reset restores original pose at current aspect");
    for(float bad : {0.0f,-1.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::max()}) {
        camera before_error = control.frame(); bool threw = false;
        try { control.resize(bad); } catch(const std::invalid_argument&) { threw = true; }
        check(threw && close(control.frame().horizontal,before_error.horizontal),"invalid aspect leaves camera intact");
    }
}

void test_frames() {
    srand48(7);
    obj_scene source(RAY_TRACER_ASSET_DIR "/models/blender_sphere/sphere.obj",19.0f/13);
    auto scene = flatten_scene(&source);
    camera_controller camera(source.lookfrom,source.lookat,40,19.0f/13,0,source.focus_dist,0,1,1,0.1f,true);
    preview_session<portable_renderer> session(scene,camera,19,13,7,2);
    auto compare_fresh = [&]() {
        portable_renderer fresh(scene,flatten_camera(session.camera_frame()),session.image_width(),session.image_height(),7,false,session.debug_mode());
        fresh.add_samples(session.sample_count());
        check(session.rgba() == display_pixels(fresh.image()),"display equals fresh render of same camera/dimensions/mode/sample count");
    };
    check(session.sample_count() == 0 && session.rgba().size() == 19*13*4,"new session starts empty at requested resolution");
    for(size_t i = 0; i < session.rgba().size(); i++) check(session.rgba()[i] == (i%4 == 3 ? 255 : 0),"initial framebuffer is opaque black");
    session.render_frame({},0); session.render_frame({},0.1);
    check(session.sample_count() == 4 && session.frame_milliseconds() > 0,"idle frames accumulate and report host frame duration");
    compare_fresh();
    camera_input input; input.right = 1; input.mouse_dx = 50;
    session.render_frame(input,0.5);
    check(session.sample_count() == 2,"camera change starts a fresh accumulation"); compare_fresh();
    input = {}; input.mouse_dx = 75; input.mouse_dy = 1200;
    session.render_frame(input,0);
    check(session.sample_count() == 2,"orbit after panning clears old view samples"); compare_fresh();
    auto pixels = session.rgba(); vec3 eye = session.camera_frame().origin;
    session.set_focused(false);
    check(session.paused() && !session.render_frame(input,1000),"unfocused frames do not render");
    check(session.rgba() == pixels && session.sample_count() == 2 && close(session.camera_frame().origin,eye),"focus loss preserves image, sample count and pose");
    session.set_focused(true); session.render_frame(input,1000);
    check(close(session.camera_frame().origin,eye) && session.sample_count() == 4,"first resumed frame discards stale input/time"); compare_fresh();
    session.render_frame(input,0.5);
    check(!close(session.camera_frame().origin,eye) && session.sample_count() == 2,"fresh input after resume can move camera");
    check(!session.resize(0,0) && session.paused(),"zero-size framebuffer suspends rendering");
    check(!session.render_frame(input,100) && session.sample_count() == 2,"minimized frame does not advance samples");
    check(!session.resize(19,13) && !session.paused(),"same-size restoration retains framebuffer");
    session.render_frame(input,100);
    check(session.sample_count() == 4,"minimize/restore discards stale motion without clearing unchanged view"); compare_fresh();
    eye = session.camera_frame().origin;
    srand48(341); double expected_random = drand48(); srand48(341);
    check(session.resize(23,17),"new dimensions recreate accumulation");
    check(drand48() == expected_random,"resize does not invoke randomized BVH construction");
    check(session.sample_count() == 0 && session.rgba().size() == 23*17*4 && close(session.camera_frame().origin,eye),"resize clears samples, changes buffer, preserves pose");
    session.render_frame({},0.1); compare_fresh();
    check(session.resize(46,34) && session.sample_count() == 0,"same-aspect resolution change still clears samples");
    session.render_frame({},0); compare_fresh();
    for(auto mode : {DEBUG_NORMALS,DEBUG_TRIANGLE,DEBUG_MATERIAL,DEBUG_BVH,DEBUG_INTERSECTIONS,DEBUG_SHADED}) {
        check(session.set_debug(mode) && session.sample_count() == 0,"debug mode change invalidates accumulation");
        check(!session.set_debug(mode),"repeated debug mode is a no-op");
        session.render_frame({},0); compare_fresh();
    }
    input = {}; input.reset = true; session.render_frame(input,0);
    check(close(session.camera_frame().origin,source.lookfrom),"reset restores original pose after resize"); compare_fresh();
    pixels = session.rgba();
    for(int bad = 0; bad < 5; bad++) {
        bool threw = false;
        try {
            if(bad == 0) session.resize(-1,1);
            if(bad == 1) session.resize(INT32_MAX,2);
            if(bad == 2) session.set_debug(render_debug(-1));
            if(bad == 3) session.set_batch_samples(0);
            if(bad == 4) session.render_frame({},-1);
        } catch(const std::invalid_argument&) { threw = true; }
        check(threw && session.rgba() == pixels,"invalid frame settings leave visible image intact");
    }
    session.set_batch_samples(3); int count = session.sample_count(); session.render_frame({},0);
    check(session.sample_count() == count+3,"batch size changes without resetting view"); compare_fresh();
    preview_session<portable_renderer> brute(scene,camera,19,13,7,4,true);
    brute.render_frame({},0);
    portable_renderer accelerated(scene,flatten_camera(camera.frame()),19,13,7);
    accelerated.add_samples(4);
    check(brute.rgba() == display_pixels(accelerated.image()),"brute preview agrees with BVH for identical sampling");
    scene.nodes.clear();
    brute.render_frame({},0);
    check(brute.sample_count() == 8,"session owns scene independently of source storage");
}

int main() {
    test_pixels(); test_resize_camera(); test_frames();
    std::cout << checks << " preview checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
