#include <iostream>
#include "display/viewer_input.h"
#include "display/viewer_options.h"

int checks = 0, failures = 0;
void check(bool value, const char *message) {
    checks++;
    if(!value) { failures++; std::cerr << "FAIL: " << message << "\n"; }
}
viewer_options options(std::vector<std::string> values) {
    values.insert(values.begin(),"viewer"); std::vector<char*> args;
    for(auto& value : values) args.push_back(value.data());
    return parse_viewer_options(int(args.size()),args.data());
}
int main() {
    viewer_input input;
    input.key(GLFW_KEY_W,GLFW_PRESS); input.key(GLFW_KEY_S,GLFW_PRESS);
    check(input.take().forward == 0,"opposing movement cancels");
    input.key(GLFW_KEY_S,GLFW_RELEASE); input.key(GLFW_KEY_D,GLFW_PRESS); input.key(GLFW_KEY_E,GLFW_PRESS);
    input.key(GLFW_KEY_RIGHT_SHIFT,GLFW_PRESS);
    auto motion = input.take();
    check(motion.forward == 1 && motion.right == 1 && motion.up == 1 && motion.fast,"held movement and either Shift key");
    input.key(GLFW_KEY_R,GLFW_PRESS);
    check(input.take().reset && !input.take().reset,"R is consumed once");
    input.key(GLFW_KEY_R,GLFW_REPEAT); check(!input.take().reset,"key repeat does not retrigger reset");
    input.key(GLFW_KEY_TAB,GLFW_PRESS); check(!input.mouse_captured(),"Tab no longer captures mouse");
    input.mouse_button(GLFW_MOUSE_BUTTON_LEFT,GLFW_PRESS);
    input.mouse_button(GLFW_MOUSE_BUTTON_MIDDLE,GLFW_PRESS);
    check(!input.mouse_captured(),"other mouse buttons do not rotate");
    input.cursor(100,100); check(input.take().mouse_dx == 0,"free mouse does not rotate");
    input.mouse_button(GLFW_MOUSE_BUTTON_RIGHT,GLFW_PRESS); check(input.mouse_captured(),"right press captures mouse");
    input.cursor(100,100); input.cursor(104,97); input.cursor(109,99);
    motion = input.take(); check(motion.mouse_dx == 9 && motion.mouse_dy == -1,"cursor deltas accumulate after baseline");
    motion = input.take(); check(motion.mouse_dx == 0 && motion.mouse_dy == 0,"mouse deltas consumed once");
    input.cursor(120,120); input.mouse_button(GLFW_MOUSE_BUTTON_RIGHT,GLFW_RELEASE);
    motion = input.take(); check(!input.mouse_captured() && motion.mouse_dx == 11 && motion.mouse_dy == 21,"right release preserves final held movement for one frame");
    input.cursor(500,500);
    motion = input.take(); check(motion.mouse_dx == 0 && motion.mouse_dy == 0,"movement after release does not rotate");
    input.cursor(900,800); input.mouse_button(GLFW_MOUSE_BUTTON_RIGHT,GLFW_PRESS); input.cursor(900,800);
    motion = input.take(); check(motion.mouse_dx == 0 && motion.mouse_dy == 0,"new drag starts without a cursor jump");
    input.cursor(920,820); input.reset_cursor(); input.cursor(-500,400);
    motion = input.take(); check(motion.mouse_dx == 0 && motion.mouse_dy == 0,"cursor reentry discards old coordinates");
    input.focus(false);
    check(!input.mouse_captured() && !input.has_focus(),"focus loss releases capture");
    input.mouse_button(GLFW_MOUSE_BUTTON_RIGHT,GLFW_PRESS);
    check(!input.mouse_captured(),"unfocused right press is ignored");
    input.key(GLFW_KEY_W,GLFW_PRESS); input.cursor(999,999); input.focus(true);
    motion = input.take(); check(motion.forward == 0 && motion.right == 0 && motion.up == 0 && !motion.fast && motion.mouse_dx == 0,"focus clears held keys and stale motion");
    for(int mode = 0; mode < 6; mode++) {
        input.key(GLFW_KEY_1+mode,GLFW_PRESS);
        check(input.take_mode() == mode && input.take_mode() == -1,"number key maps and consumes debug selection");
    }
    input.key(-1,GLFW_PRESS); input.key(GLFW_KEY_LAST+1,GLFW_PRESS);
    input.mouse_button(GLFW_MOUSE_BUTTON_RIGHT,GLFW_PRESS); input.key(GLFW_KEY_ESCAPE,GLFW_PRESS);
    check(!input.mouse_captured() && !input.should_close(),"first Escape releases mouse");
    input.key(GLFW_KEY_ESCAPE,GLFW_PRESS); check(input.should_close(),"Escape with free cursor closes");
    viewer_input quick;
    quick.cursor(10,20); quick.mouse_button(GLFW_MOUSE_BUTTON_RIGHT,GLFW_PRESS);
    quick.cursor(15,20); quick.cursor(20,30); quick.mouse_button(GLFW_MOUSE_BUTTON_RIGHT,GLFW_RELEASE);
    quick.cursor(100,200); quick.mouse_button(GLFW_MOUSE_BUTTON_RIGHT,GLFW_PRESS);
    quick.cursor(103,198); quick.mouse_button(GLFW_MOUSE_BUTTON_RIGHT,GLFW_RELEASE);
    motion = quick.take();
    check(motion.mouse_dx == 13 && motion.mouse_dy == 8,"two complete drags between frames preserve only movement while held");
    motion = quick.take(); check(motion.mouse_dx == 0 && motion.mouse_dy == 0,"completed drag motion is never applied twice");
    auto defaults = options({});
    check(defaults.render.backend == "cuda" && defaults.render.width == 640 && defaults.render.batch_samples == 1,"viewer defaults select CUDA with MIS");
    auto config = options({"--backend","cuda","--width","19","--height","13","--batch-samples","3","--frames","2","--hidden","--debug","material-id","--obj-scale","-1","2","3"});
    check(config.hidden && config.frames == 2 && config.render.backend == "cuda" && config.render.debug == DEBUG_MATERIAL && config.render.obj_scale.x() == -1,"viewer and forwarded render options");
    check(options({"--help"}).render.help,"help does not need a display");
    for(auto bad : std::vector<std::vector<std::string>>{{"--hidden"},{"--frames","0"},{"--width","0"},{"--backend","reference"},{"--samples","4"},{"--screenshot"},{"--obj-scale","1","2"}}) {
        bool threw = false;
        try { options(bad); } catch(const std::invalid_argument&) { threw = true; }
        check(threw,"invalid viewer options rejected");
    }
    std::cout << checks << " viewer input/options checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
