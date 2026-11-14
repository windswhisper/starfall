#pragma once
#include <GeneralStructures.h>
#include <string>
#include <vector>
class TechnoClass;
class HouseClass;

namespace ActiveSkills {
enum class Kind { None, Unit, Point, Area, Direction };
struct Spec {
    std::string id,icon;
    std::wstring name;
    Kind kind=Kind::None;
    int range=0,radius=0,distance=0;
    bool all=false,interruptOrders=true;
    std::vector<std::string> houses;
};
inline std::vector<Spec> specs;
inline unsigned generation=0;
std::vector<int> held(TechnoClass* unit);
int remaining(TechnoClass* unit,const Spec& skill);
bool readyCaster(TechnoClass* unit,HouseClass* house);
bool aim(const Spec& skill,TechnoClass* caster,TechnoClass* target,CellStruct cursor,CellStruct& end);
bool execute(int sender,int skill,unsigned long caster,unsigned long target,CellStruct cursor);
}
