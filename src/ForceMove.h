#pragma once
#include "ForceMoveSpec.h"
#include "FootMovement.h"
#include <map>
#include <vector>
#include <cmath>

class UnitClass;

namespace ForceMove {
constexpr int mapExtent=512*256;
struct Landing {
    int startZ,endZ,level;
    DWORD deck;
    bool bridge,crush;
    JumpEffect effect;
    int openHeight=0; // Positive for a passenger awaiting native parachute deployment.
    double ejectSpeed=EjectSpec::defaultSpeed;
};
struct Motion {
    std::optional<Landing> landing;
    FootClass* foot=nullptr; // Rebound by UniqueID after load.
    int x=0,y=0,dx=0,dy=0,height=0,startHeight=0,duration=0,elapsed=0;
    int airborneHeight=256,trackerIndex=-1;
    bool nativeTracked=false;
    unsigned lastFrame=0;
    bool blocked=false,tilt=true,ignoreCollision=true;
    Easing easing=Easing::Linear;
    Point2D position() const {
        if(easing==Easing::Linear)
            return {x+static_cast<int>(int64_t(dx)*elapsed/duration),y+static_cast<int>(int64_t(dy)*elapsed/duration)};
        double t=static_cast<double>(elapsed)/duration;
        double progress=easing==Easing::EaseIn?t*t:easing==Easing::EaseOut?t*(2-t):t*t*(3-2*t);
        return {x+static_cast<int>(std::lround(dx*progress)),y+static_cast<int>(std::lround(dy*progress))};
    }
    int jumpZ() const {
        const double t=double(elapsed)/duration;
        return static_cast<int>(std::lround((1-t)*landing->startZ+t*landing->endZ+4*t*(1-t)*height));
    }
    int lift() const {
        double t=static_cast<double>(elapsed)/duration;
        if(landing && foot) return std::max(0,jumpZ()-foot->Location.Z);
        return static_cast<int>(std::lround((1-t)*startHeight+4*t*(1-t)*height));
    }
    bool airborne() const {return airborneHeight>0 && lift()>=airborneHeight;}
};
constexpr unsigned settleFrames=12;
constexpr double maxTilt=Math::Pi/4;
struct SettlingTilt {
    int dx,dy;
    unsigned start;
};
struct Lean {float forward,side;};

extern std::map<DWORD,Motion> motions;
extern std::map<DWORD,SettlingTilt> settling;
void installTargeting();
void clearMotions();
void forget(DWORD id);
bool locked(const AbstractClass* unit);
int screenLift(const AbstractClass* unit);
int ejectionLift(const AbstractClass* unit);
using FootMovement::stopOrders;
using FootMovement::interruptTravel;
bool start(TechnoClass* unit,const Spec& spec,const CoordStruct& center);
using FootMovement::syncCellRegistration;
std::optional<CellStruct> jumpSlot(TechnoClass* unit,CellStruct click,const JumpSpec& spec,const std::vector<CellStruct>& claimed={});
void playJumpEffect(const JumpEffect& effect,const CoordStruct& at,HouseClass* owner);
bool jump(TechnoClass* unit,CellStruct click,const JumpSpec& spec);
struct EjectOrder {
    CellStruct target;
    EjectSpec spec;
    DWORD nextFrame;
    std::vector<DWORD> passengers; // Reverse boarding-list order, next passenger at back.
};
extern std::map<DWORD,EjectOrder> ejectOrders;
extern std::map<DWORD,CellStruct> descending;
bool canEject(TechnoClass* carrier,CellStruct target,const EjectSpec& spec);
bool eject(TechnoClass* carrier,CellStruct target,const EjectSpec& spec);
bool ejectUnit(TechnoClass* unit,CellStruct target,const EjectSpec& spec);
void tick();
Lean visualTilt(UnitClass* unit);
bool terrainAllows(FootClass* foot,CellClass* cell);
bool moveTo(FootClass* foot,int x,int y,bool ignoreCollision,std::optional<int> jumpZ={});
void syncAirTracker(Motion& motion);
int locateTracker(TechnoClass* unit);
void removeTracker(Motion& motion);
DynamicVectorClass<TechnoClass*>& trackerVector(int index);
}
