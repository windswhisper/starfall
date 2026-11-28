#include "EntityWalls.h"
#include "../../src/BuildingResearch.h"
#include "../../src/BuildingSpawn.h"
#include <BuildingClass.h>
#include <CCINIClass.h>
#include <DisplayClass.h>
#include <FileFormats/SHP.h>
#include <HouseClass.h>
#include <Surface.h>
#include <TacticalClass.h>
#include <TargetClass.h>
#include <algorithm>
#include <array>
#include <map>
#include <vector>

namespace EntityWalls {
namespace {
std::map<BuildingTypeClass*,WallRules> types;
std::map<BuildingTypeClass*,BlockerRules> blockers;
bool loaded=false, barriers=false;
// Same bit order as the 16 native overlay wall frames (map N, E, S, W).
constexpr CellStruct steps[]={{0,-1},{1,0},{0,1},{-1,0}};

void loadTypes() {
    auto ini=CCINIClass::INI_Rules;
    if(loaded || !ini || !BuildingTypeClass::Array.Count) return;
    for(auto type:BuildingTypeClass::Array) {
        const bool physical=ini->ReadBool(type->ID,"ProjectileBlocker.Enabled",false);
        if(physical) blockers.emplace(type,BlockerRules{
            std::max(1,ini->ReadInteger(type->ID,"ProjectileBlocker.Height",4)),
            ini->ReadBool(type->ID,"ProjectileBlocker.BlockAllied",false)});
        if(!ini->ReadBool(type->ID,"EntityWall.Enabled",false) || !BuildingSpawn::supported(type) ||
           type->Wall || type->GetFoundationWidth()!=1 || type->GetFoundationHeight(true)!=1) continue;
        char id[64]{}, group[64]{};
        ini->ReadString(type->ID,"EntityWall.SegmentType",type->ID,id);
        ini->ReadString(type->ID,"EntityWall.Group",id,group);
        auto segment=BuildingTypeClass::Find(id);
        auto& entry=types[type]={segment,group,std::clamp(ini->ReadInteger(type->ID,"EntityWall.MaxGap",4),0,32),
            ini->ReadBool(type->ID,"EntityWall.BlocksProjectiles",!physical),ini->ReadBool(type->ID,"EntityWall.HighWall",false),
            ini->ReadBool(type->ID,"EntityWall.ConnectionFrames",type==segment)};
        barriers|=entry.highWall || entry.blocksProjectiles;
    }
    loaded=true;
}

CellClass* tile(CellStruct cell) {
    if(static_cast<unsigned>(cell.X)>=512 || static_cast<unsigned>(cell.Y)>=512) return nullptr;
    return MapClass::Instance.TryGetCellAt(cell);
}

bool connects(BuildingClass* other,HouseClass* owner,const WallRules& spec) {
    if(!other || !other->IsAlive || other->InLimbo || other->Owner!=owner) return false;
    const auto adjacent=rules(other->Type);
    return adjacent && !_stricmp(adjacent->group.c_str(),spec.group.c_str());
}

struct Run { CellStruct direction; int distance; };
std::vector<Run> connections(CellStruct from,HouseClass* owner,const WallRules& spec,bool preview) {
    std::vector<Run> out;
    const auto origin=tile(from);
    const auto segment=rules(spec.segment);
    if(!origin || !segment || segment->segment!=spec.segment || _stricmp(segment->group.c_str(),spec.group.c_str())) return out;
    for(const auto step:steps) {
        auto at=from;
        for(int n=1;n<=spec.maxGap+1;++n) {
            at+=step;
            auto cell=tile(at);
            if(!cell || cell->Level!=origin->Level || cell->ContainsBridge() || cell->SlopeIndex ||
               (preview && cell->IsShrouded())) break;
            if(auto building=cell->GetBuilding()) {
                if(connects(building,owner,spec)) out.push_back({step,n});
                break;
            }
            if(n>spec.maxGap || !BuildingSpawn::available(spec.segment,owner,at)) break;
        }
    }
    return out;
}

}

void built(BuildingClass* endpoint) {
    const auto spec=rules(endpoint->Type);
    if(!spec || !endpoint->IsOnMap || endpoint->InLimbo) return;
    const auto from=endpoint->GetMapCoords();
    for(const auto& run:connections(from,endpoint->Owner,*spec,false)) {
        auto at=from;
        std::vector<TargetClass> added;
        for(int i=1;i<run.distance;++i) {
            at+=run.direction;
            if(!BuildingSpawn::createAt(spec->segment,endpoint->Owner,at)) {
                for(auto& ref:added) if(auto wall=ref.As_Building()) wall->UnInit();
                break;
            }
            added.emplace_back(tile(at)->GetBuilding());
        }
    }
}

namespace {
int connectionFrame(BuildingClass* building) {
    auto spec=rules(building->Type);
    if(!spec || !spec->connectionFrames || !building->IsOnMap || building->InLimbo) return -1;
    auto image=building->Type->GetImage();
    // Body and shadow banks each have 16 frames (or 32 with damaged art).
    if(!image || image->Frames<32 || building->GetImage()!=image) return -1;
    auto center=building->GetMapCoords();
    auto origin=tile(center);
    int frame=0;
    for(int i=0;origin && i<4;++i) {
        auto cell=tile(center+steps[i]);
        if(cell && cell->Level==origin->Level && connects(cell->GetBuilding(),building->Owner,*spec)) frame|=1<<i;
    }
    const auto health=building->GetHealthStatus();
    if(image->Frames>=96 && health==HealthState::Red) frame+=32;
    else if(image->Frames>=64 && health!=HealthState::Green) frame+=16;
    return frame;
}
}

void reset() { types.clear(); blockers.clear(); loaded=false; barriers=false; }
const WallRules* rules(BuildingTypeClass* type) {
    loadTypes();
    const auto found=types.find(type);
    return found==types.end()?nullptr:&found->second;
}
bool hasBarriers() { loadTypes(); return barriers; }
bool hasProjectileBlockers() { loadTypes(); return !blockers.empty(); }
const BlockerRules* blockerRules(BuildingTypeClass* type) {
    loadTypes();
    const auto found=blockers.find(type);
    return found==blockers.end()?nullptr:&found->second;
}

void redrawNeighbors(BuildingClass* wall) {
    if(!rules(wall->Type)) return;
    auto center=wall->GetMapCoords();
    for(auto step:steps) if(auto cell=tile(center+step)) {
        if(auto neighbor=cell->GetBuilding();neighbor && rules(neighbor->Type)) neighbor->MarkForRedraw();
    }
}

DEFINE_HOOK(0x445D8D,ActSp_EntityWallRemoved,6) {
    redrawNeighbors(R->ESI<BuildingClass*>());
    return 0;
}

DEFINE_HOOK(0x44919D,ActSp_EntityWallCaptured,5) {
    BuildingResearch::ownerChanged(R->ESI<BuildingClass*>());
    redrawNeighbors(R->ESI<BuildingClass*>());
    return 0;
}

void drawPreview() {
    auto& display=DisplayClass::Instance;
    if(!display.CurrentBuilding || !DSurface::Temp) return;
    auto type=abstract_cast<BuildingTypeClass*>(display.CurrentBuildingType);
    const auto spec=rules(type);
    auto owner=HouseClass::CurrentPlayer;
    const auto at=display.CurrentFoundation_CenterCell+display.CurrentFoundation_TopLeftOffset;
    auto cell=tile(at);
    if(!spec || !owner || !cell || cell->IsShrouded() || !BuildingSpawn::available(type,owner,at)) return;
    for(const auto& run:connections(at,owner,*spec,true)) {
        auto position=at;
        for(int n=1;n<run.distance;++n) {
            position+=run.direction;
            auto [p,visible]=TacticalClass::Instance->CoordsToClient(tile(position)->GetCoords());
            if(!visible) continue;
            const int w=Unsorted::CellWidthInPixels/2-1,h=Unsorted::CellHeightInPixels/2-1;
            Point2D corner[]={{p.X,p.Y-h},{p.X+w,p.Y},{p.X,p.Y+h},{p.X-w,p.Y}};
            for(int i=0;i<4;++i) DSurface::Temp->DrawLineEx(&DSurface::ViewBounds,&corner[i],&corner[(i+1)%4],RGBClass(80,240,140).ToInt());
            TacticalClass::Instance->RegisterDirtyArea({p.X-w,p.Y-h,2*w+1,2*h+1},false);
        }
    }
}

// Stock wall art includes ToOverlay. Suppress that conversion on opted-in
// building types while retaining the SHP, cameo, and theater-specific palette.
DEFINE_HOOK(0x46121F,ActSp_KeepEntityWallBuilding,6) {
    auto type=R->EBP<BuildingTypeClass*>();
    auto ini=CCINIClass::INI_Rules;
    if(R->EAX<void*>() && ini && ini->ReadBool(type->ID,"EntityWall.Enabled",false) && !type->Wall)
        R->EAX(0);
    return 0;
}

// BuildingClass::GetImage selects the buildup until the building is placed
// and out of Construction. Stock wall art has no buildup, so fall through
// to the normal body SHP even during those first draws.
DEFINE_HOOK(0x4513F2,ActSp_EntityWallBuildupImage,6) {
    auto wall=R->ECX<BuildingClass*>();
    auto type=wall->Type;
    if(wall->IsOnMap && !wall->InLimbo && !type->Buildup && rules(type) && !type->LoadBuildup())
        return 0x4513E4;
    return 0;
}

DEFINE_HOOK(0x43EF94,ActSp_EntityWallShapeFrame,6) {
    const auto frame=connectionFrame(R->ESI<BuildingClass*>());
    if(frame<0) return 0;
    R->EAX(frame);
    return 0x43F171;
}

// This function checks proximity only. Ordinary foundation, occupation and
// terrain checks still run for the paid endpoint.
DEFINE_HOOK(0x4A8F1F,ActSp_EntityWallProximity,6) {
    auto type=R->ESI<BuildingTypeClass*>();
    auto spec=rules(type);
    if(!spec) return 0;
    auto owner=HouseClass::Array.GetItemOrDefault(R->Stack<int>(0x30));
    auto at=*R->EDI<CellStruct*>();
    if(owner && !connections(at,owner,*spec,true).empty()) return 0x4A905C;
    return 0;
}
}
