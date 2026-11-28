#pragma once
#include <TechnoTypeClass.h>
#include <map>

namespace GroundGuards {
enum class DockState : DWORD { None, Patrol, Returning, Parked };
// The child uses the engine's saved TargetClass ID, never a process address.
struct Slot {
    int child=0;
    DWORD readyFrame=0;
    int house=-1;
    DockState dock=DockState::None;
};
extern std::map<DWORD,Slot> slots;
struct Config {
    TechnoTypeClass* type=nullptr;
    CellStruct initialPoint{-1,-1};
    int radius=5;
    int delay=300;
    bool useBuildingDocks=false;
};
const Config* config(BuildingClass* building);
void forget(DWORD postId);
}
