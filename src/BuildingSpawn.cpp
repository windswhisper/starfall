#include "BuildingSpawn.h"
#include <BuildingClass.h>
#include <HouseClass.h>
#include <MapClass.h>
#include <algorithm>
#include <vector>

// Shared by INI results, Lua actions and superweapon sequences.
namespace BuildingSpawn {
bool supported(const BuildingTypeClass* type) {
    // Upgrades require a host; wall/pavement types replace themselves with map data.
    return type && !type->PowersUpBuilding[0] && !type->ToOverlay && !type->ToTile;
}
bool available(BuildingTypeClass* type,HouseClass* owner,CellStruct anchor) {
    auto& map=MapClass::Instance;
    auto within=[&](CellStruct cell) {
        return cell.X>=0 && cell.Y>=0 && cell.X<512 && cell.Y<512 && map.IsWithinUsableArea(cell,false);
    };
    if(!within(anchor)) return false;
    auto origin=map.TryGetCellAt(anchor);
    auto foundation=type->GetFoundationData(true);
    constexpr CellStruct end{0x7FFF,0x7FFF};
    if(!origin || !foundation || *foundation==end) return false;
    for(auto offset=foundation;*offset!=end;++offset) {
        const int x=anchor.X+offset->X,y=anchor.Y+offset->Y;
        if(x<0 || y<0 || x>=512 || y>=512) return false;
        CellStruct position{static_cast<short>(x),static_cast<short>(y)};
        if(!within(position)) return false;
        auto cell=map.TryGetCellAt(position);
        // Check the entire foundation, including the bib. Do not let extensions'
        // build-on / displacement rules destroy or move existing occupants.
        if(!cell || cell->FirstObject || cell->AltObject || cell->Jumpjet ||
           cell->ContainsBridge() || cell->ContainsBridgeEx() ||
           cell->Level!=origin->Level || cell->SlopeIndex || (cell->OccupationFlags&0x3F)) return false;
    }
    return type->CanPlaceHere(&anchor,owner);
}
bool createAt(BuildingTypeClass* type,HouseClass* owner,CellStruct anchor) {
    if(!supported(type) || !owner || !available(type,owner,anchor)) return false;
    auto building=static_cast<BuildingClass*>(type->CreateObject(owner));
    if(!building) return false;
    building->QueueMission(Mission::Construction,false);
    building->NextMission();
    if(!building->Unlimbo(MapClass::Instance.GetCellAt(anchor)->GetCoords(),DirType::North)) {
        building->UnInit();
        return false;
    }
    building->BeginMode(BStateType::Idle);
    building->QueueMission(Mission::Guard,false);
    building->NextMission();
    building->Place(false);
    owner->RecheckTechTree=true;
    return true;
}
bool create(BuildingTypeClass* type,HouseClass* owner,const CoordStruct& location,int radius) {
    if(!supported(type) || !owner || owner->Defeated || owner->IsObserver() || radius<0 || radius>32) return false;
    auto& map=MapClass::Instance;
    const auto center=CellClass::Coord2Cell(location);
    if(center.X<0 || center.Y<0 || center.X>=512 || center.Y>=512 || !map.IsWithinUsableArea(center,false)) return false;
    std::vector<CellStruct> offsets;
    for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x)
        if(x*x+y*y<=radius*radius) offsets.push_back({static_cast<short>(x),static_cast<short>(y)});
    // Nearest anchor first; ties use Y, then X. No local UI state or RNG is used
    // to choose a site, so peers resolve simultaneous results identically.
    std::sort(offsets.begin(),offsets.end(),[](CellStruct a,CellStruct b) {
        const int da=a.X*a.X+a.Y*a.Y,db=b.X*b.X+b.Y*b.Y;
        return da!=db?da<db:a.Y!=b.Y?a.Y<b.Y:a.X<b.X;
    });
    for(auto offset:offsets) {
        CellStruct anchor{static_cast<short>(center.X+offset.X),static_cast<short>(center.Y+offset.Y)};
        if(createAt(type,owner,anchor)) return true;
    }
    return false;
}
}
