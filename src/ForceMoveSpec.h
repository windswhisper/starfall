#pragma once
#include <cmath>
#include <optional>
#include <string>

namespace ForceMove {
enum class Easing { Linear, EaseOut, EaseIn, EaseInOut };
enum class Mode { Offset, Push, Pull, Launch };
struct JumpEffect {
    std::string anim,sound;
};
struct EjectSpec {
    int range=8,openHeight=3,interval=9;
    static constexpr double defaultSpeed=1; // Horizontal cells per simulation second.
    double horizontalSpeed=defaultSpeed;
    static bool validSpeed(double speed) {return std::isfinite(speed) && speed>=1.0/256.0;}
    bool valid() const {return range>=1 && range<=128 && openHeight>=1 && openHeight<=128 &&
        validSpeed(horizontalSpeed) && interval>=1 && interval<=3600;}
};
struct JumpSpec {
    int range=6,height=3,duration=15; // Distances in cells, duration in frames.
    JumpEffect takeoff,land;
};
struct Spec {
    Mode mode=Mode::Offset;
    Easing easing=Easing::Linear;
    int dx=0,dy=0,distance=0,height=0,duration=0;
    int airborneHeight=256;
    std::optional<double> blowWeight;
    bool impact=false,tilt=true,ignoreCollision=true,affectsIronCurtain=false;
};
}
