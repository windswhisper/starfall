#include <HouseClass.h>
#include <EventClass.h>
#include <MapClass.h>
#include <map>
#include <set>
#include "ActSp.h"
#include "GuardPoints.h"
#include "GroundGuards.h"
#include "BetterGarrison.h"
#include "HighPlatforms.h"
#include "Piercing.h"
#include "FireOrigin.h"
#include "BuildNodes.h"
#include "EntityWalls.h"

namespace {
std::map<DWORD,CellStruct> overrides;
constexpr wchar_t streamName[]=L"ActSp.GuardPoints";
constexpr DWORD formatVersion=5;

bool playable(CellStruct cell) {
    return cell.X>=0 && cell.X<512 && cell.Y>=0 && cell.Y<512 &&
           MapClass::Instance.IsWithinUsableArea(cell,false) && MapClass::Instance.TryGetCellAt(cell);
}
bool needsRallyCommand(BuildingClass* building) {
    return building->Owner && building->Owner->IsControlledByCurrentPlayer() &&
           !building->IsUnitFactory() && GuardPoints::eligible(building);
}

struct SavedPoint { DWORD owner; CellStruct cell; };
static_assert(sizeof(SavedPoint)==8);
struct SavedGround { DWORD post; int child; DWORD readyFrame; int house; };
static_assert(sizeof(SavedGround)==16);
struct SavedObjectOrder { DWORD post; int house; int target; };
static_assert(sizeof(SavedObjectOrder)==12);
bool writePoints(IStream* stream) {
    auto write=[stream](const auto& value) {
        ULONG size=0;
        return SUCCEEDED(stream->Write(&value,sizeof(value),&size)) && size==sizeof(value);
    };
    const DWORD count=static_cast<DWORD>(overrides.size());
    if(!write(formatVersion) || !write(count)) return false;
    for(const auto& [owner,cell]:overrides)
        if(!write(SavedPoint{owner,cell})) return false;
    const DWORD guards=static_cast<DWORD>(GroundGuards::slots.size());
    if(!write(guards)) return false;
    for(const auto& [post,slot]:GroundGuards::slots)
        if(!write(SavedGround{post,slot.child,slot.readyFrame,slot.house}) || !write(slot.dock)) return false;
    const DWORD orders=static_cast<DWORD>(BetterGarrison::orders.size());
    if(!write(orders)) return false;
    for(const auto& [post,order]:BetterGarrison::orders)
        if(!write(SavedObjectOrder{post,order.house,order.target}) || !write(static_cast<DWORD>(order.kind))) return false;
    return SUCCEEDED(stream->Commit(STGC_DEFAULT));
}
bool readPoints(IStream* stream) {
    auto read=[stream](auto& value) {
        ULONG size=0;
        return stream->Read(&value,sizeof(value),&size)==S_OK && size==sizeof(value);
    };
    DWORD version=0,count=0;
    STATSTG info{};
    if(!read(version) || version<1 || version>formatVersion || !read(count) ||
       FAILED(stream->Stat(&info,STATFLAG_NONAME))) return false;
    auto prefix=2*sizeof(DWORD)+static_cast<ULONGLONG>(count)*sizeof(SavedPoint);
    if(info.cbSize.QuadPart<prefix || (version==1 && info.cbSize.QuadPart!=prefix)) return false;
    std::map<DWORD,CellStruct> restored;
    for(DWORD i=0;i<count;++i) {
        SavedPoint entry{};
        if(!read(entry) || !entry.owner || !playable(entry.cell) ||
           !restored.emplace(entry.owner,entry.cell).second) return false;
    }
    std::map<DWORD,GroundGuards::Slot> slots;
    if(version>=2) {
        DWORD guards=0;
        if(!read(guards)) return false;
        const auto entrySize=sizeof(SavedGround)+(version>=5?sizeof(GroundGuards::DockState):0);
        prefix+=sizeof(DWORD)+static_cast<ULONGLONG>(guards)*entrySize;
        if(info.cbSize.QuadPart<prefix || (version==2 && info.cbSize.QuadPart!=prefix)) return false;
        std::set<int> children;
        for(DWORD i=0;i<guards;++i) {
            SavedGround entry{};
            auto dock=GroundGuards::DockState::None;
            if(!read(entry) || (version>=5 && !read(dock)) || dock>GroundGuards::DockState::Parked ||
               !entry.post || entry.house<0 || entry.house>=HouseClass::Array.Count ||
               (entry.child && !children.insert(entry.child).second) ||
               !slots.emplace(entry.post,GroundGuards::Slot{entry.child,entry.readyFrame,entry.house,dock}).second) return false;
        }
    }
    std::map<DWORD,BetterGarrison::Order> orders;
    if(version>=3) {
        DWORD count=0;
        const auto orderSize=sizeof(SavedObjectOrder)+(version>=4?sizeof(DWORD):0);
        if(!read(count) || info.cbSize.QuadPart!=prefix+sizeof(DWORD)+static_cast<ULONGLONG>(count)*orderSize) return false;
        for(DWORD i=0;i<count;++i) {
            SavedObjectOrder entry{};
            DWORD kind=static_cast<DWORD>(AbstractType::Abstract);
            if(!read(entry) || (version>=4 && !read(kind)) || !entry.post ||
               entry.house<0 || entry.house>=HouseClass::Array.Count) return false;
            if(kind==static_cast<DWORD>(AbstractType::Cell)) {
                if(entry.target<0 || entry.target>=512000 || entry.target%1000>=512 ||
                   !playable({static_cast<short>(entry.target%1000),static_cast<short>(entry.target/1000)})) return false;
            } else if(kind!=static_cast<DWORD>(AbstractType::Abstract) || !entry.target) return false;
            if(!orders.emplace(entry.post,BetterGarrison::Order{entry.house,entry.target,static_cast<AbstractType>(kind)}).second) return false;
        }
    }
    overrides.swap(restored);
    GroundGuards::slots.swap(slots);
    BetterGarrison::orders.swap(orders);
    return true;
}
}

CellStruct GuardPoints::station(DWORD ownerId,CellStruct fallback) {
    auto found=overrides.find(ownerId);
    return found==overrides.end()?fallback:found->second;
}
bool GuardPoints::set(TechnoClass* owner,CellStruct cell) {
    if(!eligible(owner) || !playable(cell)) return false;
    overrides[owner->UniqueID]=cell;
    return true;
}

// BuildingClass::MouseOverCell has already decided on attack/force-fire and other special actions.
DEFINE_HOOK(0x44776D,ActSp_GuardRallyAction,5) {
    auto action=R->EBX<Action>();
    if(action!=Action::None && action!=Action::Move && action!=Action::NoMove) return 0;
    auto building=R->ESI<BuildingClass*>();
    auto cell=R->Stack<const CellStruct*>(0x1C);
    if(cell && needsRallyCommand(building) && playable(*cell)) R->EBX(Action::Move);
    return 0;
}
// Non-factory outposts use the same synchronized Archive event as native factory rally points.
DEFINE_HOOK(0x44373D,ActSp_ClickGuardRally,6) {
    auto building=R->ESI<BuildingClass*>();
    auto cell=R->Stack<const CellStruct*>(0x8C);
    if(R->EAX<Action>()!=Action::Move || !needsRallyCommand(building)) return 0;
    if(cell && playable(*cell)) {
        EventClass order(building->Owner->ArrayIndex,EventType::Archive,TargetClass(building),TargetClass(*cell));
        EventClass::OutList.Add(order);
    }
    return 0x44384E;
}
// Only the execution of the network event changes the guard point, never local mouse input.
DEFINE_HOOK(0x4C6DD3,ActSp_ApplyGuardRally,8) {
    auto owner=R->EDI<TechnoClass*>();
    auto& target=R->ESI<EventClass*>()->Archive.Whom2;
    if(owner->WhatAmI()==AbstractType::Building && owner->CurrentMission!=Mission::Selling &&
       target.m_RTTI==static_cast<unsigned char>(AbstractType::Cell)) {
        if(auto cell=target.As_Cell()) GuardPoints::set(owner,cell->MapCoords);
    }
    return 0;
}

void ActSp::reset() {
    Piercing::reset();
    FireOrigin::reset();
    BuildNodes::reset();
    EntityWalls::reset();
    GuardPoints::clearProfiles();
    overrides.clear();
    GroundGuards::slots.clear();
    BetterGarrison::reset();
    HighPlatforms::reset();
}

void ActSp::forget(DWORD id) {
    overrides.erase(id);
    GroundGuards::forget(id);
    BetterGarrison::orders.erase(id);
    HighPlatforms::forget(id);
}

bool ActSp::save(IStorage* storage) {
    IStream* stream=nullptr;
    if(FAILED(storage->CreateStream(streamName,STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,0,&stream)))
        return false;
    const bool ok=writePoints(stream);
    stream->Release();
    return ok && HighPlatforms::save(storage) && Piercing::save(storage);
}

bool ActSp::load(IStorage* storage) {
    BuildNodes::reset();
    EntityWalls::reset();
    GuardPoints::clearProfiles();
    BetterGarrison::reset();
    if(!storage || !HighPlatforms::load(storage) || !Piercing::load(storage)) return false;
    IStream* stream=nullptr;
    HRESULT status=storage->OpenStream(streamName,nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&stream);
    if(status==STG_E_FILENOTFOUND) {
        overrides.clear();
        GroundGuards::slots.clear();
        return true;
    }
    const bool ok=SUCCEEDED(status) && readPoints(stream);
    if(stream) stream->Release();
    return ok;
}
