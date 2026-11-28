#pragma once
#include <GeneralStructures.h>
#include <string>
#include <map>
#include <vector>
class TechnoClass;
class HouseClass;
namespace CommandBarOrders {
enum EnterKind : unsigned {Transport=1, Garrison=2, TankBunker=4};
struct Entry {
    std::string id,button,image;
    bool explicitImage;
    int radius;
    unsigned enterKinds;
    std::wstring tooltip;
};
inline std::vector<Entry> entries;
// Local UI layout. The synchronized order index always comes from entries.
inline std::map<std::string,int> listedSlots;
inline constexpr unsigned eventTag=0x534F0000u;
bool eligible(TechnoClass* unit,HouseClass* owner,unsigned enterKinds);
bool execute(int sender,unsigned index,int object,CellStruct center);
}
