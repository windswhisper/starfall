#include <BuildingClass.h>
#include "../../src/BuildingResearch.h"
#include <BulletClass.h>
#include <BulletTypeClass.h>
#include <CCINIClass.h>
#include <ConvertClass.h>
#include <DisplayClass.h>
#include <EventClass.h>
#include <FootClass.h>
#include <HouseClass.h>
#include <IsometricTileTypeClass.h>
#include <MapClass.h>
#include <OverlayTypeClass.h>
#include <RulesClass.h>
#include <Surface.h>
#include <TacticalClass.h>
#include <TargetClass.h>
#include <TerrainClass.h>
#include <WeaponTypeClass.h>
#include <WWKeyboardClass.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>
#include "../../src/FootMovement.h"
#include "../../src/ForceMove.h"
#include "HighPlatforms.h"
#include "EntityWalls.h"

namespace HighPlatforms {
namespace {
constexpr CellStruct legacyDeck{4,4};
constexpr int lift=4;
constexpr int maxLevel=14;
constexpr unsigned mapSide=512;
constexpr wchar_t streamName[]=L"ActSp.HighPlatforms";
constexpr DWORD streamVersion=5;

struct Geometry {
    CellStruct deck{};
    bool nativeRim=false;
    bool allowWater=false;
    bool allowLand=true;

    explicit operator bool() const { return deck.X!=0; }
};

struct SeaTile { int index; char subTile; };

struct Platform {
    CellStruct anchor;
    int level;
    Geometry geometry;
    std::vector<SeaTile> seaFloor;

    CellStruct start() const {
        if(geometry.nativeRim) return {static_cast<short>(anchor.X+1),static_cast<short>(anchor.Y+1)};
        // Legacy platforms used a single occupied cell at the front corner.
        return {static_cast<short>(anchor.X-legacyDeck.X),static_cast<short>(anchor.Y-legacyDeck.Y+1)};
    }
    bool contains(CellStruct cell) const {
        const auto top=start();
        return cell.X>=top.X && cell.X<top.X+geometry.deck.X && cell.Y>=top.Y && cell.Y<top.Y+geometry.deck.Y;
    }
};
constexpr CellStruct foundationEnd{0x7FFF,0x7FFF};

std::map<const BuildingTypeClass*,Geometry> enabledTypes;
std::map<const TechnoTypeClass*,bool> entryTypes;
std::map<DWORD,Platform> platforms;
struct TreeShadow {
    DWORD terrainId;
    int frame;
    int depth;
};
std::map<CellClass*,TreeShadow> foregroundShadows;
struct EntryOrder {
    DWORD platformId;
    CellStruct approach;
};
std::map<DWORD,EntryOrder> entries;
std::set<DWORD> falling;
std::array<BYTE,mapSide*mapSide> rimCells{};
bool refreshSavedCover=false;

bool inMapArray(CellStruct at) {
    return static_cast<unsigned>(at.X)<mapSide && static_cast<unsigned>(at.Y)<mapSide;
}

unsigned cellIndex(CellStruct at) {
    return static_cast<unsigned>(at.Y)*mapSide+static_cast<unsigned>(at.X);
}

bool isRim(CellStruct at) {
    return inMapArray(at) && rimCells[cellIndex(at)]!=0;
}

void markRim(const Platform& platform,bool raised) {
    const auto top=platform.start();
    const auto [width,height]=platform.geometry.deck;
    for(int y=-1;y<=height;++y) for(int x=-1;x<=width;++x) {
        if(x>=0 && x<width && y>=0 && y<height) continue;
        const CellStruct at{static_cast<short>(top.X+x),static_cast<short>(top.Y+y)};
        if(!inMapArray(at)) continue;
        auto& count=rimCells[cellIndex(at)];
        if(raised) ++count;
        else --count;
    }
}

CellClass* playable(CellStruct at) {
    auto& map=MapClass::Instance;
    return inMapArray(at) && map.IsWithinUsableArea(at,false)
        ? map.TryGetCellAt(at):nullptr;
}

bool validDeckSize(CellStruct deck) {
    return deck.X>0 && deck.Y>0 && deck.X<=static_cast<int>(mapSide)-2 && deck.Y<=static_cast<int>(mapSide)-2;
}

Geometry readLayout(const BuildingTypeClass* type) {
    const auto foundation=type->GetFoundationData(true);
    if(!foundation) return {};
    if(*foundation==CellStruct{0,0} && foundation[1]==foundationEnd) return {legacyDeck,false};
    const int width=type->GetFoundationWidth(),height=type->GetFoundationHeight(true);
    if(width<3 || height<3 || width>static_cast<int>(mapSide) || height>static_cast<int>(mapSide)) return {};
    const int rimLength=2*(width+height)-4;
    std::set<int> cells;
    for(int i=0;i<rimLength;++i) {
        const auto [x,y]=foundation[i];
        if(x<0 || y<0 || x>=width || y>=height ||
           (x>0 && x<width-1 && y>0 && y<height-1) || !cells.insert(y*width+x).second) return {};
    }
    if(foundation[rimLength]!=foundationEnd) return {};
    return {{static_cast<short>(width-2),static_cast<short>(height-2)},true};
}

Geometry layout(const BuildingTypeClass* type) {
    if(!CCINIClass::INI_Rules) return {};
    auto [it,added]=enabledTypes.try_emplace(type);
    if(added && CCINIClass::INI_Rules->ReadBool(type->ID,"HighPlatform.Enabled",false)) {
        it->second=readLayout(type);
        it->second.allowLand=CCINIClass::INI_Rules->ReadBool(type->ID,"HighPlatform.AllowLand",true);
        it->second.allowWater=CCINIClass::INI_Rules->ReadBool(type->ID,"HighPlatform.AllowWater",false);
    }
    return it->second;
}

const Platform* deckAt(CellStruct at) {
    for(const auto& [id,platform]:platforms) if(platform.contains(at)) return &platform;
    return nullptr;
}

int platformDrawOrder(CellStruct anchor) {
    // Draw the platform just before the first point on its raised deck.
    return (anchor.X+anchor.Y+2)*Unsorted::LeptonsPerCell-1;
}

int groundDrawOrder(const FootClass* unit,int order) {
    const auto location=unit->GetCoords();
    const int tile=Unsorted::LeptonsPerCell;
    const int horizontal=location.X-location.Y;
    for(const auto& [id,platform]:platforms) {
        if(!platform.geometry.nativeRim || location.Z>=(platform.level+lift)*Unsorted::LevelHeight) continue;
        const int backX=platform.anchor.X*tile;
        const int backY=platform.anchor.Y*tile;
        if(location.X>=backX && location.Y>=backY) continue;
        const auto [width,height]=platform.geometry.deck;
        const int frontX=backX+(width+2)*tile;
        const int frontY=backY+(height+2)*tile;
        // Allow for sprite overhang where a rear unit stands beside the projected footprint.
        if(horizontal<backX-frontY-tile || horizontal>frontX-backY+tile) continue;
        order=std::min(order,platformDrawOrder(platform.anchor)-1);
    }
    return order;
}

int foregroundSceneryDepthOffset(CellStruct cell) {
    constexpr int frontRows=3;
    for(const auto& [id,platform]:platforms) {
        if(!platform.geometry.nativeRim) continue;
        const int x=cell.X-platform.anchor.X;
        const int y=cell.Y-platform.anchor.Y;
        const int width=platform.geometry.deck.X+2;
        const int height=platform.geometry.deck.Y+2;
        if(x<0 || y<0 || (x<width && y<height)) continue;
        // Only adjust scenery on the near side whose sprite can overlap the platform.
        if(x>=width+frontRows || y>=height+frontRows || x-y>width+1 || y-x>height+1) continue;
        return TacticalClass::AdjustForZ((lift-1)*Unsorted::LevelHeight);
    }
    return 0;
}

bool firesUpCliff(const TechnoClass* attacker,AbstractClass* target) {
    if(platforms.empty() || !attacker || !target) return false;
    const auto source=attacker->Transporter?attacker->Transporter:attacker;
    if(source->IsInAir() || target->IsInAir()) return false;

    CellStruct destination;
    if(auto cell=abstract_cast<CellClass*>(target)) destination=cell->MapCoords;
    else if(auto building=abstract_cast<BuildingClass*>(target)) destination=building->GetMapCoords();
    else if(auto unit=abstract_cast<FootClass*>(target)) destination=CellClass::Coord2Cell(unit->GetTargetCoords());
    else return false;

    const auto deck=deckAt(destination);
    if(!deck) return false;
    const auto origin=source->GetCoords();
    return !deck->contains(CellClass::Coord2Cell(origin)) &&
        origin.Z<(deck->level+lift)*Unsorted::LevelHeight;
}

bool blockedByCliff(const WeaponTypeClass* weapon) {
    return weapon && weapon->Projectile && weapon->Projectile->SubjectToCliffs;
}

bool mayRaise(CellStruct anchor,Geometry geometry,const BuildingClass* support=nullptr) {
    auto base=playable(anchor);
    if(!base || base->Level<0 || base->Level>maxLevel-lift || base->SlopeIndex ||
       base->ContainsBridge() || base->ContainsBridgeEx() || deckAt(anchor)) return false;
    const bool sea=base->LandType==LandType::Water;
    if(!(sea ? geometry.allowWater : geometry.allowLand)) return false;
    const Platform proposed{anchor,base->Level,geometry};
    const auto top=proposed.start();
    for(int y=-1;y<=geometry.deck.Y;++y) for(int x=-1;x<=geometry.deck.X;++x) {
        const CellStruct at{static_cast<short>(top.X+x),static_cast<short>(top.Y+y)};
        auto cell=playable(at);
        if(!cell || isRim(at) || cell->Level!=base->Level || cell->SlopeIndex || cell->ContainsBridge() || cell->ContainsBridgeEx()) return false;
        auto occupant=cell->FirstObject;
        if(occupant && occupant==support) occupant=occupant->NextObject;
        const auto land=static_cast<unsigned>(cell->LandType);
        if(occupant || cell->AltObject || cell->Jumpjet ||
            cell->OverlayTypeIndex>=0 || cell->TubeIndex>=0 || cell->Rubble || (cell->OccupationFlags&0x3F) ||
            (cell->LandType==LandType::Water)!=sea || cell->LandType==LandType::Beach ||
            (!sea && (land>=std::size(GroundType::Array) || !GroundType::Array[land].Buildable))) return false;
    }
    return true;
}

bool allowed(const BuildingTypeClass* type,CellStruct anchor) {
    const auto geometry=layout(type);
    if(isRim(anchor) || (geometry && !mayRaise(anchor,geometry))) return false;
    if(platforms.empty()) return true;
    const auto origin=deckAt(anchor);
    auto foundation=type->GetFoundationData(true);
    for(;foundation && *foundation!=foundationEnd;++foundation) {
        const CellStruct at{static_cast<short>(anchor.X+foundation->X),static_cast<short>(anchor.Y+foundation->Y)};
        if(isRim(at) || deckAt(at)!=origin) return false;
    }
    return true;
}

void changeLevel(const Platform& platform,int level) {
    auto& map=MapClass::Instance;
    const auto top=platform.start();
    const auto [width,height]=platform.geometry.deck;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const CellStruct at{static_cast<short>(top.X+x),static_cast<short>(top.Y+y)};
        if(auto cell=playable(at)) {
            cell->Level=static_cast<char>(level);
            if(!platform.seaFloor.empty()) {
                const auto& original=platform.seaFloor[y*width+x];
                const bool raised=level!=platform.level;
                // 0xFFFF is the native flat clear tile. Height selects a subtile within an isometric tile.
                cell->IsoTileTypeIndex=raised?0xFFFF:original.index;
                cell->Height=raised?0:original.subTile;
            }
        }
    }
    for(int y=-1;y<=height;++y) for(int x=-1;x<=width;++x) {
        const CellStruct at{static_cast<short>(top.X+x),static_cast<short>(top.Y+y)};
        if(auto cell=playable(at)) {
            cell->RecalcAttributes(-1);
            map.ResetZones(at);
            map.RecalculateZones(at);
            map.RecalculateSubZones(at);
            cell->MarkForRedraw();
        }
    }
    foregroundShadows.clear();
    map.MarkNeedsRedraw(2);
}

TargetClass objectReference(DWORD id) {
    TargetClass reference;
    reference.m_ID=id;
    reference.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
    return reference;
}

const Platform* findPlatform(const BuildingClass* building) {
    if(!building || !building->IsAlive || building->Health<=0 || building->InLimbo || !building->IsOnMap ||
       building->CurrentMission==Mission::Selling) return nullptr;
    const auto it=platforms.find(building->UniqueID);
    return it!=platforms.end() && it->second.anchor==building->GetMapCoords()?&it->second:nullptr;
}

FootClass* groundUnit(TechnoClass* unit) {
    if(!unit || (unit->WhatAmI()!=AbstractType::Infantry && unit->WhatAmI()!=AbstractType::Unit)) return nullptr;
    const auto speed=unit->GetTechnoType()->SpeedType;
    return speed>=SpeedType::Foot && speed!=SpeedType::Winged && speed!=SpeedType::Float && speed!=SpeedType::FloatBeach
        ?static_cast<FootClass*>(unit):nullptr;
}

bool canTransfer(const FootClass* unit) {
    return unit && unit->IsAlive && unit->Health>0 && unit->IsOnMap && !unit->InLimbo && !unit->Transporter &&
        !unit->Absorbed && !unit->IsImmobilized && !unit->IsBeingWarpedOut() && !unit->IsInAir() &&
        !unit->IsFallingDown && !unit->IsSinking && !unit->IsCrashing && !unit->Deactivated && !unit->IsUnderEMP() && !unit->OnBridge &&
        unit->Locomotor && unit->TubeIndex<0 && !unit->IsAttackedByLocomotor && !unit->IsDeploying &&
        !unit->ParasiteEatingMe;
}

bool entryEnabled(const TechnoTypeClass* type) {
    if(!CCINIClass::INI_Rules) return false;
    auto [it,added]=entryTypes.try_emplace(type,true);
    if(added) it->second=CCINIClass::INI_Rules->ReadBool(type->ID,"HighPlatform.Enter",true);
    return it->second;
}

bool canEnter(FootClass* unit, BuildingClass* support, const Platform* platform) {
    return platform && canTransfer(unit) && unit->Owner && unit->Owner==support->Owner &&
        !deckAt(unit->GetMapCoords()) && entryEnabled(support->Type) && entryEnabled(unit->GetTechnoType());
}

bool canExit(const FootClass* unit,const BuildingClass* support,const Platform& platform) {
    return unit && unit->Owner && unit->Owner==support->Owner && platform.contains(unit->GetMapCoords()) && canTransfer(unit);
}

bool hasOccupants(const BuildingClass* support,const Platform& platform) {
    for(auto foot:FootClass::Array)
        if(auto unit=groundUnit(foot); canExit(unit,support,platform)) return true;
    return false;
}

CellClass* transferCell(FootClass* unit,const Platform& platform,CellStruct from,bool onDeck,bool approach=false) {
    const auto* type=unit->GetTechnoType();
    const auto top=platform.start();
    const int first=onDeck?0:-2;
    const auto [width,height]=platform.geometry.deck;
    const int lastX=onDeck?width-1:width+1;
    const int lastY=onDeck?height-1:height+1;
    CellClass* best=nullptr;
    int bestDistance=INT_MAX;
    for(int y=first;y<=lastY;++y) for(int x=first;x<=lastX;++x) {
        if(!onDeck && x!=first && x!=lastX && y!=first && y!=lastY) continue;
        const CellStruct at{static_cast<short>(top.X+x),static_cast<short>(top.Y+y)};
        const int dx=at.X-from.X,dy=at.Y-from.Y;
        const int distance=dx*dx+dy*dy;
        if(distance>=bestDistance || isRim(at) || deckAt(at)!=(onDeck?&platform:nullptr)) continue;
        auto cell=playable(at);
        if(!cell || cell->Level!=platform.level+(onDeck?lift:0) || cell->SlopeIndex ||
           cell->ContainsBridge() || cell->ContainsBridgeEx() || cell->GetBuilding() || cell->Jumpjet) continue;
        const bool current=approach && at==from;
        if(!current && (cell->GetContent() ||
           !cell->IsClearToMove(type->SpeedType,false,false,-1,type->MovementZone,-1,false))) continue;
        if(approach && !current) {
            if(!unit->IsInSameZoneAs(cell)) continue;
            if(std::any_of(entries.begin(),entries.end(),[&](const auto& entry) {
                return entry.first!=unit->UniqueID && entry.second.approach==at;
            })) continue;
        }
        best=cell;
        bestDistance=distance;
    }
    return best;
}

void approachPlatform(FootClass* unit,CellClass* cell) {
    FootMovement::stopOrders(unit);
    if(unit->GetMapCoords()!=cell->MapCoords) {
        unit->SetDestination(cell,true);
        unit->QueueMission(Mission::Move,true);
        unit->NextMission();
    }
}

bool transferUnit(FootClass* unit,CellClass* cell) {
    auto destination=cell->GetCoords();
    const bool infantry=unit->WhatAmI()==AbstractType::Infantry;
    bool moving=false;
    if(infantry) {
        // Infantry reserves a native position within the cell, separately from the object list.
        auto subposition=cell->FindInfantrySubposition(destination,false,false,false);
        if(subposition==CoordStruct::Empty || CellClass::Coord2Cell(subposition)!=cell->MapCoords) return false;
        destination.X=subposition.X;
        destination.Y=subposition.Y;
        moving=unit->Locomotor->Is_Moving() || unit->Locomotor->Is_Moving_Now();
    }
    if(!FootMovement::interruptTravel(unit)) return false;
    if(infantry && !moving) {
        // The walk locomotor already releases a moving infantry unit's reservation.
        // Mark(Up) removes a stationary infantry unit from the cell list only.
        const auto& origin=unit->Location;
        const auto subposition=reinterpret_cast<int(__fastcall*)(const CoordStruct*)>(0x4810A0)(&origin);
        if(unit->GetCell()->OccupationFlags&(1u<<subposition)) unit->UnmarkAllOccupationBits(origin);
    }
    unit->MarkForRedraw();
    unit->Mark(MarkType::Up);
    unit->SetLocation(destination);
    unit->Mark(MarkType::Down);
    if(infantry) unit->MarkAllOccupationBits(destination);
    FootMovement::syncCellRegistration(unit);
    unit->SightTimer.Start(0);
    unit->ForceMission(Mission::Guard);
    unit->MarkForRedraw();
    return true;
}

void orderEntry(FootClass* unit,BuildingClass* support,const Platform& platform) {
    if(auto approach=transferCell(unit,platform,unit->GetMapCoords(),false,true)) {
        entries[unit->UniqueID]={support->UniqueID,approach->MapCoords};
        approachPlatform(unit,approach);
    }
}

void orderExit(BuildingClass* support,const Platform& platform) {
    for(auto foot:FootClass::Array) {
        auto unit=groundUnit(foot);
        if(!canExit(unit,support,platform)) continue;
        if(auto landing=transferCell(unit,platform,unit->GetMapCoords(),false)) transferUnit(unit,landing);
    }
}

void tickEntries() {
    for(auto it=entries.begin();it!=entries.end();) {
        auto unit=groundUnit(objectReference(it->first).As_Techno());
        auto support=objectReference(it->second.platformId).As_Building();
        const auto platform=findPlatform(support);
        auto& approach=it->second.approach;
        if(!canEnter(unit,support,platform) ||
           (unit->CurrentMission!=Mission::Move && unit->CurrentMission!=Mission::Guard && unit->CurrentMission!=Mission::Sleep) ||
           (unit->Destination && unit->Destination!=playable(approach))) {
            it=entries.erase(it);
            continue;
        }
        const auto at=unit->GetMapCoords();
        if(at==approach) {
            // The walk locomotor can cross the cell boundary before settling in its reserved spot.
            if(unit->WhatAmI()==AbstractType::Infantry && unit->Locomotor->Is_Moving_Now()) {
                ++it;
                continue;
            }
            if(auto landing=transferCell(unit,*platform,at,true); landing && transferUnit(unit,landing)) {
                it=entries.erase(it);
                continue;
            }
            if(unit->CurrentMission!=Mission::Sleep) FootMovement::stopOrders(unit);
        } else if(!unit->Destination && (static_cast<DWORD>(Unsorted::CurrentFrame)+it->first)%15==0) {
            if(auto next=transferCell(unit,*platform,at,false,true)) {
                approach=next->MapCoords;
                approachPlatform(unit,next);
            }
        }
        ++it;
    }
}

void refreshSavedPlatformCover() {
    auto& map=MapClass::Instance;
    std::vector<std::pair<BuildingClass*,CellStruct>> supports;
    for(const auto& [id,platform]:platforms) {
        if(!platform.geometry.nativeRim) continue;
        auto building=objectReference(id).As_Building();
        auto cell=playable(platform.anchor);
        if(building && building->IsAlive && building->IsOnMap && !building->InLimbo &&
           building->Type->CanHideThings && cell && cell->GetBuilding()==building)
            supports.emplace_back(building,platform.anchor);
    }
    // Let the engine remove each saved building's own cover contributions.
    // Types are shared, so change the flag only after removing all old registrations.
    for(auto& [building,at]:supports) map.RemoveContentAt(&at,building);
    for(auto type:BuildingTypeClass::Array)
        if(type && type->CanHideThings && layout(type).nativeRim) type->CanHideThings=false;
    for(auto& [building,at]:supports) map.AddContentAt(&at,building);
    if(!supports.empty()) map.MarkNeedsRedraw(2);
}

void destroy(TechnoClass* object) {
    int damage=object->Health;
    object->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,nullptr,true,false,nullptr);
}

void collapse(const Platform& platform) {
    std::set<DWORD> buildings;
    const auto top=platform.start();
    const auto [width,height]=platform.geometry.deck;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const CellStruct at{static_cast<short>(top.X+x),static_cast<short>(top.Y+y)};
        if(auto cell=playable(at)) if(auto building=cell->GetBuilding()) buildings.insert(building->UniqueID);
    }
    for(auto id:buildings) {
        auto building=objectReference(id).As_Building();
        if(building && building->IsAlive && building->Health>0) destroy(building);
    }

    // Collect after destroying the buildings so any ejected occupants on the deck also fall.
    std::vector<TargetClass> occupants;
    for(auto unit:FootClass::Array) {
        if(unit && unit->IsAlive && unit->Health>0 && unit->IsOnMap && !unit->InLimbo && !unit->IsInAir() &&
           platform.contains(unit->GetMapCoords())) occupants.emplace_back(unit);
    }
    markRim(platform,false);
    changeLevel(platform,platform.level);
    for(auto reference:occupants) if(auto unit=reference.As_Foot();unit && unit->IsAlive && unit->Health>0) {
        falling.insert(unit->UniqueID);
        unit->DropAsBomb();
    }
}

void placed(BuildingClass* building) {
    const auto geometry=layout(building->Type);
    if(!geometry || platforms.contains(building->UniqueID)) return;
    const auto anchor=building->GetMapCoords();
    if(!mayRaise(anchor,geometry,building)) return;
    Platform platform{anchor,playable(anchor)->Level,geometry};
    if(playable(anchor)->LandType==LandType::Water) {
        const auto top=platform.start();
        const auto [width,height]=geometry.deck;
        platform.seaFloor.reserve(width*height);
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            const auto cell=playable({short(top.X+x),short(top.Y+y)});
            platform.seaFloor.push_back({cell->IsoTileTypeIndex,cell->Height});
        }
    }
    auto it=platforms.emplace(building->UniqueID,std::move(platform)).first;
    markRim(it->second,true);
    changeLevel(it->second,it->second.level+lift);
}

BuildingClass* wallAt(Point2D point) {
    // This runs after the engine's screen-space lookup of units and other render-layer objects.
    // Resolve the face to the same building object that owns the ground foundation.
    BuildingClass* result=nullptr;
    int nearestBase=0;
    for(const auto& [id,platform]:platforms) {
        const auto top=platform.start();
        const auto [width,height]=platform.geometry.deck;
        const auto middle=playable({static_cast<short>(top.X+width/2),static_cast<short>(top.Y+height/2)});
        if(!middle || middle->IsShrouded() || middle->IsFogged()) continue;
        const int tile=Unsorted::LeptonsPerCell;
        const CoordStruct corner{(top.X+width)*tile,(top.Y+height)*tile,(platform.level+lift)*Unsorted::LevelHeight};
        const auto projected=TacticalClass::Instance->CoordsToClient(corner).first;
        // The two visible faces share the front corner and span different widths on a rectangle.
        const int dx=point.X-projected.X;
        const int halfWidth=Unsorted::CellWidthInPixels/2;
        if(dx<=-width*halfWidth || dx>=height*halfWidth) continue;
        const int front=projected.Y-std::abs(dx)*Unsorted::CellHeightInPixels/Unsorted::CellWidthInPixels;
        const int base=front+TacticalClass::AdjustForZ(corner.Z)-TacticalClass::AdjustForZ(corner.Z-lift*Unsorted::LevelHeight);
        if(point.Y<=front || point.Y>=base || (result && base<=nearestBase)) continue;
        auto support=objectReference(id).As_Building();
        if(!support || !support->IsAlive || support->Health<=0 || !support->IsOnMap || support->InLimbo ||
           (support->Owner!=HouseClass::CurrentPlayer && support->CloakState!=CloakState::Uncloaked)) continue;
        result=support;
        nearestBase=base;
    }
    return result;
}

template<class T> bool write(IStream* stream,const T& value) {
    ULONG written=0;
    return SUCCEEDED(stream->Write(&value,sizeof(value),&written)) && written==sizeof(value);
}

template<class T> bool read(IStream* stream,T& value) {
    ULONG count=0;
    return stream->Read(&value,sizeof(value),&count)==S_OK && count==sizeof(value);
}

bool saveState(IStream* stream) {
    if(!write(stream,streamVersion) || !write(stream,static_cast<DWORD>(platforms.size()))) return false;
    for(const auto& [id,platform]:platforms) {
        if(!write(stream,id) || !write(stream,platform.anchor) || !write(stream,platform.level) ||
           !write(stream,static_cast<BYTE>(platform.geometry.nativeRim)) || !write(stream,platform.geometry.deck) ||
           !write(stream,static_cast<DWORD>(platform.seaFloor.size()))) return false;
        for(const auto& tile:platform.seaFloor)
            if(!write(stream,tile.index) || !write(stream,tile.subTile)) return false;
    }
    if(!write(stream,static_cast<DWORD>(falling.size()))) return false;
    for(auto id:falling) if(!write(stream,id)) return false;
    if(!write(stream,static_cast<DWORD>(entries.size()))) return false;
    for(const auto& [id,order]:entries)
        if(!write(stream,id) || !write(stream,order.platformId) || !write(stream,order.approach)) return false;
    return SUCCEEDED(stream->Commit(STGC_DEFAULT));
}

bool loadState(IStream* stream) {
    STATSTG info{};
    DWORD version=0,count=0;
    if(FAILED(stream->Stat(&info,STATFLAG_NONAME)) || !read(stream,version) || version<1 || version>streamVersion ||
       !read(stream,count)) return false;
    const auto recordSize=sizeof(DWORD)+sizeof(CellStruct)+sizeof(int)+(version>=2?sizeof(BYTE):0)+(version>=4?sizeof(CellStruct):0);
    auto prefix=3*sizeof(DWORD)+static_cast<ULONGLONG>(count)*(recordSize+(version>=5?sizeof(DWORD):0));
    if(info.cbSize.QuadPart<prefix) return false;
    std::map<DWORD,Platform> restored;
    std::set<int> occupied;
    for(DWORD i=0;i<count;++i) {
        DWORD id=0;
        Platform platform{};
        BYTE nativeRim=0;
        auto deck=legacyDeck;
        if(!read(stream,id) || !id || !read(stream,platform.anchor) || !playable(platform.anchor) ||
           !read(stream,platform.level) || platform.level<0 || platform.level>maxLevel-lift ||
           (version>=2 && (!read(stream,nativeRim) || nativeRim>1)) ||
           (version>=4 && (!read(stream,deck) || !validDeckSize(deck) || (!nativeRim && deck!=legacyDeck)))) return false;
        platform.geometry={deck,nativeRim!=0};
        if(version>=5) {
            DWORD tiles=0;
            if(!read(stream,tiles) || (tiles && tiles!=static_cast<DWORD>(deck.X*deck.Y))) return false;
            prefix+=static_cast<ULONGLONG>(tiles)*(sizeof(int)+sizeof(char));
            if(prefix>info.cbSize.QuadPart) return false;
            platform.seaFloor.resize(tiles);
            for(auto& tile:platform.seaFloor) {
                if(!read(stream,tile.index) || !read(stream,tile.subTile) ||
                   tile.index<0 || tile.index>=IsometricTileTypeClass::Array.Count) return false;
            }
        }
        const auto top=platform.start();
        const int margin=version>=4?1:0;
        for(int y=-margin;y<deck.Y+margin;++y) for(int x=-margin;x<deck.X+margin;++x) {
            const CellStruct at{static_cast<short>(top.X+x),static_cast<short>(top.Y+y)};
            if(!playable(at) || !occupied.insert(cellIndex(at)).second) return false;
        }
        if(!restored.emplace(id,std::move(platform)).second) return false;
    }
    std::set<DWORD> restoredFalling;
    if(!read(stream,count)) return false;
    const auto afterFalling=prefix+static_cast<ULONGLONG>(count)*sizeof(DWORD);
    if(version>=3 ? info.cbSize.QuadPart<afterFalling+sizeof(DWORD) : info.cbSize.QuadPart!=afterFalling) return false;
    for(DWORD i=0;i<count;++i) {
        DWORD id=0;
        if(!read(stream,id) || !id || !restoredFalling.insert(id).second) return false;
    }
    std::map<DWORD,EntryOrder> restoredEntries;
    if(version>=3) {
        constexpr auto entrySize=2*sizeof(DWORD)+sizeof(CellStruct);
        if(!read(stream,count) || info.cbSize.QuadPart!=afterFalling+sizeof(DWORD)+static_cast<ULONGLONG>(count)*entrySize) return false;
        for(DWORD i=0;i<count;++i) {
            DWORD id=0;
            EntryOrder order{};
            if(!read(stream,id) || !id || !read(stream,order.platformId) || !order.platformId ||
               !read(stream,order.approach) || !playable(order.approach) ||
               !restoredEntries.emplace(id,order).second) return false;
        }
    }
    platforms.swap(restored);
    falling.swap(restoredFalling);
    entries.swap(restoredEntries);
    for(const auto& [id,platform]:platforms) markRim(platform,true);
    for(const auto& [id,platform]:platforms) {
        if(platform.seaFloor.empty()) continue;
        // The engine normally restores the deck in its own map stream. Keep its saved pathing intact.
        const auto top=platform.start();
        const auto [width,height]=platform.geometry.deck;
        const int raised=platform.level+lift;
        bool needsRepair=false;
        for(int y=0;y<height && !needsRepair;++y) for(int x=0;x<width;++x) {
            auto cell=playable({short(top.X+x),short(top.Y+y)});
            if(cell->Level!=raised || cell->Tile_Is_Water()) { needsRepair=true; break; }
        }
        if(needsRepair) changeLevel(platform,raised);
    }
    return true;
}
}

bool landingRim(CellStruct at) {return isRim(at);}
DWORD landingDeck(CellStruct at) {
    for(const auto& [id,platform]:platforms) if(platform.contains(at)) return id;
    return 0;
}

CellClass* arcingWallCell(const AbstractClass* target,const BulletTypeClass* projectile,const CoordStruct& source) {
    if(platforms.empty() || !target || !projectile || !projectile->Arcing) return nullptr;
    const auto it=platforms.find(target->UniqueID);
    if(it==platforms.end()) return nullptr;
    const auto& platform=it->second;
    if(!platform.geometry.nativeRim) return playable(platform.anchor);

    const auto from=CellClass::Coord2Cell(source);
    const auto left=platform.anchor.X, back=platform.anchor.Y;
    const short right=left+platform.geometry.deck.X+1, front=back+platform.geometry.deck.Y+1;
    CellStruct wall{std::clamp(from.X,left,right),std::clamp(from.Y,back,front)};
    if(platform.contains(wall)) {
        const int distance=std::min({wall.X-left,right-wall.X,wall.Y-back,front-wall.Y});
        if(wall.X-left==distance) wall.X=left;
        else if(right-wall.X==distance) wall.X=right;
        else if(wall.Y-back==distance) wall.Y=back;
        else wall.Y=front;
    }
    return playable(wall);
}

int deckDepthOffset(const TechnoClass* object) {
    if(platforms.empty() || !object->IsOnMap || object->InLimbo || object->IsInAir()) return 0;
    const auto coords=object->GetCoords();
    const auto platform=deckAt(CellClass::Coord2Cell(coords));
    if(!platform || !platform->geometry.nativeRim || coords.Z<(platform->level+lift)*Unsorted::LevelHeight) return 0;
    // The ground gradient advances one depth row per screen row. Account
    // for the roof moving up the screen as well as closer in depth.
    return 2*TacticalClass::AdjustForZ(lift*Unsorted::LevelHeight);
}

bool drawsPlatformBody(TechnoClass* object) {
    const auto building=abstract_cast<BuildingClass*>(object);
    return building && layout(building->Type).nativeRim;
}

void tick() {
    if(refreshSavedCover) {
        refreshSavedCover=false;
        refreshSavedPlatformCover();
    }
    for(auto it=platforms.begin();it!=platforms.end();) {
        auto support=objectReference(it->first).As_Building();
        if(support && support->IsAlive && support->Health>0 && support->IsOnMap && !support->InLimbo &&
           support->GetMapCoords()==it->second.anchor && layout(support->Type)) {
            ++it;
            continue;
        }
        const auto platform=std::move(it->second);
        it=platforms.erase(it);
        collapse(platform);
    }
    tickEntries();
}

void reset() {
    enabledTypes.clear();
    entryTypes.clear();
    platforms.clear();
    foregroundShadows.clear();
    entries.clear();
    falling.clear();
    rimCells.fill(0);
    refreshSavedCover=false;
}
void forgetEntry(DWORD id) { entries.erase(id); }
void forget(DWORD id) {
    falling.erase(id);
    forgetEntry(id);
}

bool save(IStorage* storage) {
    IStream* stream=nullptr;
    if(FAILED(storage->CreateStream(streamName,STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,0,&stream))) return false;
    const bool ok=saveState(stream);
    stream->Release();
    return ok;
}

bool load(IStorage* storage) {
    reset();
    IStream* stream=nullptr;
    const HRESULT status=storage->OpenStream(streamName,nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&stream);
    if(status==STG_E_FILENOTFOUND) return true;
    if(FAILED(status)) return false;
    const bool ok=loadState(stream);
    stream->Release();
    refreshSavedCover=ok;
    return ok;
}
}

using namespace HighPlatforms;

namespace {
bool normalPointer(bool ignoreForce) {
    const auto keyboard=WWKeyboardClass::Instance;
    return ignoreForce || (!keyboard->IsForceFireKeyPressed() && !keyboard->IsForceMoveKeyPressed() &&
        !keyboard->IsForceSelectKeyPressed());
}

bool sendTransfer(TechnoClass* sender,Mission mission,BuildingClass* support) {
    if(!sender->Owner || !sender->Owner->IsControlledByCurrentPlayer()) return false;
    const TargetClass target(support);
    // Enter and Unload use the same stock network packet as ordinary unit orders.
    const EventClass order(sender->Owner->ArrayIndex,TargetClass(sender),mission,target,target,TargetClass{});
    return EventClass::OutList.Add(order);
}

bool canDeployPlatform(BuildingClass* support) {
    const auto platform=findPlatform(support);
    return platform && support->Owner && support->Owner->IsControlledByCurrentPlayer() && hasOccupants(support,*platform);
}

Action enterPointer(REGISTERS* registers) {
    if(platforms.empty() || !normalPointer(registers->Stack<bool>(8))) return Action::None;
    auto support=abstract_cast<BuildingClass*>(registers->Stack<ObjectClass*>(4));
    auto unit=groundUnit(registers->ECX<TechnoClass*>());
    const auto platform=findPlatform(support);
    if(!platform || !canTransfer(unit) || !unit->Owner || unit->Owner!=support->Owner ||
       deckAt(unit->GetMapCoords())) return Action::None;
    return entryEnabled(support->Type) && entryEnabled(unit->GetTechnoType())?Action::Enter:Action::NoEnter;
}

bool clickEnter(REGISTERS* registers) {
    const auto action=registers->Stack<Action>(4);
    if(action!=Action::Enter && action!=Action::NoEnter) return false;
    auto support=abstract_cast<BuildingClass*>(registers->Stack<ObjectClass*>(8));
    const auto platform=findPlatform(support);
    if(!platform) return false;
    auto unit=groundUnit(registers->ECX<TechnoClass*>());
    registers->EAX(action==Action::Enter && canEnter(unit,support,platform) &&
        sendTransfer(unit,Mission::Enter,support)?1u:0u);
    return true;
}
}

DEFINE_HOOK(0x51E3B0,ActSp_InfantryPlatformEnterPointer,7) {
    const auto action=enterPointer(R);
    if(action==Action::None) return 0;
    R->EAX(action);
    return 0x51E45F;
}

DEFINE_HOOK(0x73FD50,ActSp_VehiclePlatformEnterPointer,7) {
    const auto action=enterPointer(R);
    if(action==Action::None) return 0;
    R->EAX(action);
    return 0x73FD78;
}

DEFINE_HOOK(0x51F190,ActSp_InfantryClickPlatformEnter,5) {
    return clickEnter(R)?0x51F1F7:0;
}

DEFINE_HOOK(0x738890,ActSp_VehicleClickPlatformEnter,5) {
    return clickEnter(R)?0x73890A:0;
}

DEFINE_HOOK(0x447210,ActSp_PlatformDeployPointer,6) {
    auto support=R->ECX<BuildingClass*>();
    if(support!=R->Stack<ObjectClass*>(4) || !normalPointer(R->Stack<bool>(8)) || !canDeployPlatform(support)) return 0;
    R->EAX(Action::Self_Deploy);
    return 0x447230;
}

DEFINE_HOOK(0x443450,ActSp_ClickPlatformDeploy,9) {
    if(R->EAX<Action>()!=Action::Self_Deploy) return 0;
    auto support=R->EBX<BuildingClass*>();
    if(!findPlatform(support)) return 0;
    R->EAX(canDeployPlatform(support) && sendTransfer(support,Mission::Unload,support)?1u:0u);
    return 0x443444;
}

DEFINE_HOOK(0x730AF0,ActSp_PlatformDeployKey,8) {
    if(platforms.empty()) return 0;
    for(auto selected:ObjectClass::CurrentObjects) {
        auto support=abstract_cast<BuildingClass*>(selected);
        if(canDeployPlatform(support)) sendTransfer(support,Mission::Unload,support);
    }
    return 0;
}

DEFINE_HOOK(0x4C7217,ActSp_ApplyPlatformTransfer,8) {
    if(platforms.empty() && entries.empty()) return 0;
    auto sender=R->EDI<TechnoClass*>();
    auto event=R->ESI<EventClass*>();
    if(!sender->Owner || event->HouseIndex!=sender->Owner->ArrayIndex || sender->InLimbo) return 0;
    const auto& order=event->MegaMission;
    const auto mission=static_cast<Mission>(order.Mission);
    if(event->Type==EventType::MegaMission && order.Target.m_RTTI==static_cast<BYTE>(AbstractType::Abstract) &&
       order.Target.m_ID==order.Destination.m_ID && order.Target.m_RTTI==order.Destination.m_RTTI) {
        auto support=event->MegaMission.Target.As_Building();
        if(mission==Mission::Enter) {
            forgetEntry(sender->UniqueID);
            auto unit=groundUnit(sender);
            if(const auto platform=findPlatform(support); canEnter(unit,support,platform)) orderEntry(unit,support,*platform);
            return 0x4C8109;
        }
        if(mission==Mission::Unload && order.Target.m_ID==sender->UniqueID) {
            if(const auto platform=findPlatform(support)) {
                orderExit(support,*platform);
                return 0x4C8109;
            }
        }
    }
    forgetEntry(sender->UniqueID);
    return 0;
}

// Relax only the final terrain test after the engine's ordinary cell checks.
DEFINE_HOOK(0x47CA33,ActSp_PlatformWaterFoundation,6) {
    auto cell=R->EDI<CellClass*>();
    if(cell->LandType==LandType::Water) {
        auto type=R->Stack<BuildingTypeClass*>(0x20);
        if(type) {
            const auto geometry=layout(type);
            if(geometry && geometry.allowWater) return 0x47CA70;
        }
    }
    return 0;
}

DEFINE_HOOK(0x464AC0,ActSp_CheckPlatformPlacement,6) {
    const auto cell=R->Stack<const CellStruct*>(4);
    if(cell && !allowed(R->ECX<BuildingTypeClass*>(),*cell)) {
        R->EAX(0);
        return 0x464ACC;
    }
    return 0;
}

DEFINE_HOOK(0x4A8EB0,ActSp_CheckPlatformPreview,5) {
    auto type=abstract_cast<BuildingTypeClass*>(R->Stack<ObjectTypeClass*>(4));
    const auto cell=R->Stack<const CellStruct*>(0x10);
    if(type && cell && *cell!=CellStruct::Empty && !allowed(type,*cell)) {
        R->EAX(0);
        return 0x4A9063;
    }
    return 0;
}

DEFINE_HOOK(0x4834A0,ActSp_CheckPlatformPathCell,5) {
    const auto cell=R->ECX<const CellClass*>();
    if(R->Stack<SpeedType>(4)!=SpeedType::Winged && isRim(cell->MapCoords)) {
        R->EAX(0);
        return 0x4834DE;
    }
    return 0;
}

bool cannotEnterPlatformRim(REGISTERS* registers) {
    const auto cell=registers->Stack<const CellClass*>(4);
    if(!cell || !isRim(cell->MapCoords)) return false;
    const auto unit=registers->ECX<const FootClass*>();
    return !unit->IsOnMap || !unit->IsInAir();
}

DEFINE_HOOK(0x51BF90,ActSp_CheckPlatformInfantryCell,5) {
    if(cannotEnterPlatformRim(R)) {
        R->EAX(static_cast<int>(Move::No));
        return 0x51C036;
    }
    return 0;
}

DEFINE_HOOK(0x73F0A0,ActSp_CheckPlatformVehicleCell,6) {
    if(cannotEnterPlatformRim(R)) {
        R->EAX(static_cast<int>(Move::No));
        return 0x73F18E;
    }
    return 0;
}

DEFINE_HOOK(0x440580,ActSp_CheckPlatformUnlimbo,5) {
    const auto coords=R->Stack<const CoordStruct*>(4);
    if(coords && !allowed(R->ECX<BuildingClass*>()->Type,CellClass::Coord2Cell(*coords))) {
        R->EAX(0);
        return 0x44086E;
    }
    return 0;
}

DEFINE_HOOK(0x440D01,ActSp_RaisePlatform,6) {
    BuildingResearch::trackBuilding(R->ESI<BuildingClass*>());
    placed(R->ESI<BuildingClass*>());
    EntityWalls::redrawNeighbors(R->ESI<BuildingClass*>());
    return 0;
}

DEFINE_HOOK(0x449410,ActSp_PlatformRenderOrder,8) {
    auto building=R->ECX<BuildingClass*>();
    if(building->Type && layout(building->Type).nativeRim) {
        R->EAX(platformDrawOrder(building->GetMapCoords()));
        return 0x44943D;
    }
    return 0;
}

// The native sum of the render X and Y coordinates is in ECX just before returning it.
DEFINE_HOOK(0x5F6BFE,ActSp_PlatformGroundRenderOrder,7) {
    if(const int height=ForceMove::ejectionLift(R->ESI<ObjectClass*>())) {
        R->ECX(R->ECX<int>()+height);
        return 0;
    }
    if(platforms.empty()) return 0;
    auto unit=abstract_cast<FootClass*>(R->ESI<ObjectClass*>());
    if(unit && unit->IsOnMap && !unit->InLimbo && !unit->IsInAir()) R->ECX(groundDrawOrder(unit,R->ECX<int>()));
    return 0;
}

DEFINE_HOOK(0x5195E2,ActSp_PlatformInfantryDrawDepth,6) {
    // Infantry supplies its own Z adjustment before calling DrawObject.
    const auto infantry=R->EBP<const TechnoClass*>();
    // The launch lifts the sprite on screen while its engine location stays
    // grounded. Account for both the screen displacement and elevated depth.
    const int offset=deckDepthOffset(infantry)+2*TacticalClass::AdjustForZ(ForceMove::ejectionLift(infantry));
    if(offset) R->Stack(0x18,R->Stack<int>(0x18)-offset);
    return 0;
}

DEFINE_HOOK(0x71C2F0,ActSp_PlatformTerrainDepth,5) {
    if(platforms.empty()) return 0;
    const auto cell=R->Stack<CellClass*>(0x38);
    const auto offset=foregroundSceneryDepthOffset(cell->MapCoords);
    if(!offset) return 0;
    const auto tree=cell->GetTerrain(false);
    if(!tree || tree->Type->SpawnsTiberium) {
        foregroundShadows.erase(cell);
        return 0;
    }
    // The upright sprite's depth is on the stack. EDI still holds the native ground depth.
    R->Stack(0,R->Stack<int>(0)-offset);
    if(tree->Type->IsAnimated || tree->IsCrumbling) foregroundShadows.erase(cell);
    else foregroundShadows.insert_or_assign(cell,TreeShadow{tree->UniqueID,R->EBX<int>(),R->EDI<int>()-3-offset});
    return 0;
}

// Static tree shadows are baked into the terrain before buildings are drawn.
// Keep the chosen SHP frame and composite these foreground shadows after the objects.
DEFINE_HOOK(0x71C309,ActSp_DeferPlatformTreeShadow,5) {
    return !foregroundShadows.empty() && foregroundShadows.contains(R->Stack<CellClass*>(0x18)) ? 0x71C353 : 0;
}

DEFINE_HOOK(0x6D4664,ActSp_DrawPlatformTreeShadows,5) {
    if(foregroundShadows.empty() || !Game::bDrawShadow) return 0;
    for(auto it=foregroundShadows.begin();it!=foregroundShadows.end();) {
        const auto cell=it->first;
        const auto shadow=it->second;
        const auto tree=cell->GetTerrain(false);
        if(!tree || tree->UniqueID!=shadow.terrainId || tree->IsCrumbling) {
            it=foregroundShadows.erase(it);
            continue;
        }
        ++it;
        auto [pt,visible]=TacticalClass::Instance->CoordsToClient(tree->GetRenderCoords());
        if(!visible || cell->IsShrouded() || cell->IsFogged()) continue;
        const auto image=tree->GetImage();
        const int frame=image ? image->Frames/2+shadow.frame : -1;
        if(!image || frame<0 || frame>=image->Frames || !cell->LightConvert) continue;
        constexpr auto flags=BlitterFlags::Darken|BlitterFlags::Centered|BlitterFlags::bf_400|BlitterFlags::Alpha|BlitterFlags::ZRead;
        DSurface::Temp->DrawSHP(cell->LightConvert,image,frame,&pt,&DSurface::ViewBounds,flags,
            0,shadow.depth,ZGradient::Ground,1000,0,nullptr,0,0,0);
    }
    return 0;
}

DEFINE_HOOK(0x47F722,ActSp_PlatformWallDepth,6) {
    // The overlay drawer subtracts EDI from the wall sprite's depth.
    if(!platforms.empty()) {
        const auto cell=R->ESI<const CellClass*>();
        const auto overlay=OverlayTypeClass::Array.GetItemOrDefault(cell->OverlayTypeIndex);
        if(overlay && overlay->Wall) R->EDI(R->EDI<int>()+foregroundSceneryDepthOffset(cell->MapCoords));
    }
    return 0;
}

DEFINE_HOOK(0x6DA4B8,ActSp_FindPlatformWall,6) {
    if(!platforms.empty() && !DisplayClass::Instance.CurrentBuilding) {
        if(auto building=wallAt(*R->ESI<const Point2D*>())) {
            R->EAX(building);
            return 0x6DA501;
        }
    }
    return 0;
}

// InRange has sampled the floor under the building center, which is the raised deck.
DEFINE_HOOK(0x6F7379,ActSp_ArcingWallRange,6) {
    const auto weapon=R->Base<WeaponTypeClass*>(0x10);
    if(auto cell=arcingWallCell(R->Base<AbstractClass*>(0xC),weapon->Projectile,*R->Base<const CoordStruct*>(8))) {
        const auto aim=cell->GetCoords();
        R->Stack<CoordStruct>(0x20,aim);
        R->EAX(aim.Z);
    }
    return 0;
}

// Feed the wall position to the native launch calculation before it applies weapon scatter.
DEFINE_HOOK(0x6FE634,ActSp_ArcingWallAim,6) {
    if(auto cell=arcingWallCell(R->Base<AbstractClass*>(8),R->Stack<BulletTypeClass*>(0x68),R->Stack<CoordStruct>(0x44)))
        *R->EAX<CoordStruct*>()=cell->GetCoords();
    return 0;
}

// The ring has no elevated terrain of its own. Let a shell that has physically
// reached the target wall explode there before the next step takes it onto the deck.
DEFINE_HOOK(0x468BE2,ActSp_ArcingWallContact,6) {
    const auto bullet=R->ESI<BulletClass*>();
    const auto cell=R->EAX<CellClass*>();
    if(platforms.empty() || !bullet->Type->Arcing || !isRim(cell->MapCoords) ||
        !bullet->Target || cell->GetBuilding()!=bullet->Target) return 0;
    const auto it=platforms.find(bullet->Target->UniqueID);
    if(it!=platforms.end() && bullet->Location.Z<=(it->second.level+lift)*Unsorted::LevelHeight)
        return 0x468C76;
    return 0;
}

// A confirmed ground hit normally snaps to the fixed building offset. Use the
// wall selected at launch, even if the firing unit has since moved or died.
DEFINE_HOOK(0x468F8B,ActSp_ArcingWallImpact,6) {
    const auto bullet=R->ESI<BulletClass*>();
    if(auto cell=arcingWallCell(bullet->Target,bullet->Type,bullet->SourceCoords)) {
        const auto contact=MapClass::Instance.TryGetCellAt(bullet->Location);
        const bool hitWall=contact && isRim(contact->MapCoords) && contact->GetBuilding()==bullet->Target;
        R->Stack<CoordStruct>(0x0C,hitWall?bullet->Location:cell->GetCoords());
        return 0x468FF4;
    }
    return 0;
}

DEFINE_HOOK(0x6FC339,ActSp_CheckPlatformCliffAttack,6) {
    if(EntityWalls::blocksAttack(R->ESI<TechnoClass*>(),R->Stack<AbstractClass*>(0x24),R->EDI<WeaponTypeClass*>())) return 0x6FC86A;
    if(blockedByCliff(R->EDI<WeaponTypeClass*>()) &&
       firesUpCliff(R->ESI<TechnoClass*>(),R->Stack<AbstractClass*>(0x24))) return 0x6FC86A;
    return 0;
}

DEFINE_HOOK(0x6FDDC0,ActSp_CheckPlatformCliffFire,6) {
    if(EntityWalls::blocksAttack(R->ESI<TechnoClass*>(),R->EDI<AbstractClass*>(),R->EBX<WeaponTypeClass*>())) return 0x6FDE03;
    if(blockedByCliff(R->EBX<WeaponTypeClass*>()) &&
       firesUpCliff(R->ESI<TechnoClass*>(),R->EDI<AbstractClass*>())) return 0x6FDE03;
    return 0;
}

DEFINE_HOOK(0x6F86FE,ActSp_CheckPlatformCliffThreat,6) {
    const auto attacker=R->EDI<TechnoClass*>();
    const auto target=R->ESI<ObjectClass*>();
    const bool cliff=firesUpCliff(attacker,target);
    if(cliff || EntityWalls::hasBarriers()) {
        const int slot=attacker->SelectWeapon(target);
        const auto mount=slot>=0?attacker->GetWeapon(slot):nullptr;
        if(mount && ((cliff && blockedByCliff(mount->WeaponType)) || EntityWalls::blocksAttack(attacker,target,mount->WeaponType))) return 0x6F894F;
    }
    return 0;
}

// Retain native falling motion and override damage only when a platform victim reaches the ground.
DEFINE_HOOK(0x5F4021,ActSp_PlatformFallLanding,6) {
    auto object=R->ESI<ObjectClass*>();
    if(!falling.erase(object->UniqueID)) return 0;
    object->FallRate=0;
    object->IsABomb=false;
    if(auto unit=abstract_cast<FootClass*>(object);unit && unit->IsAlive && unit->Health>0) destroy(unit);
    return 0x5F405B;
}
