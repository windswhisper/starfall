#pragma once
#include <GeneralDefinitions.h>
#include <GeneralStructures.h>
#include <string>
#include "ForceMoveSpec.h"
#include <vector>
class TechnoClass;
class HouseClass;

namespace ActiveSkills {
enum class Kind { None, Unit, Point, Area, Direction };
struct Spec {
    std::string id,icon;
    std::string indicatorTexture,arrowTexture;
    double arrowWidth=0.75;
    int arrowHead=25,arrowTail=37,indicatorOpacity=90;
    std::wstring name;
    Kind kind=Kind::None;
    int range=0,radius=0,distance=0;
    std::optional<ForceMove::JumpSpec> jump;
    std::optional<ForceMove::EjectSpec> eject;
    bool all=false,interruptOrders=true;
    MouseCursorType orderCursor=MouseCursorType::Default;
    std::vector<std::string> houses;
};
inline std::vector<Spec> specs;
inline unsigned generation=0;
std::vector<int> held(TechnoClass* unit);
int remaining(TechnoClass* unit,const Spec& skill);
bool readyCaster(TechnoClass* unit,HouseClass* house);
bool playable(CellStruct point);
bool orderArea(unsigned long unit,CellStruct& center,int& radius,MouseCursorType& style);
bool aim(const Spec& skill,TechnoClass* caster,TechnoClass* target,CellStruct cursor,CellStruct& end);
bool execute(int sender,int skill,unsigned long caster,unsigned long target,CellStruct cursor);
}
