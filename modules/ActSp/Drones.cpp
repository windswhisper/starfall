#include <HouseClass.h>
#include <AircraftTrackerClass.h>
#include <BulletTypeClass.h>
#include <CCINIClass.h>
#include <InfantryTypeClass.h>
#include <MapClass.h>
#include <SpawnManagerClass.h>
#include <UnitTypeClass.h>
#include <Unsorted.h>
#include <WeaponTypeClass.h>
#include <array>
#include <charconv>
#include <map>
#include <string>
#include <string_view>
#include "GuardPoints.h"
#include "GroundGuards.h"
#include "BuildingDocks.h"

namespace {
enum class Mode { Native, Guard, Follow, Orbit };
struct Rules {
    Mode mode=Mode::Native;
    bool useBuildingDocks=false;
    CellStruct point{-1,-1};
    int radius=3;
    int period=180;
    int guardRange=5;
    GroundGuards::Config ground;
};
std::map<const TechnoTypeClass*,Rules> profiles;

std::string field(const char* type,const char* key,const char* fallback="") {
    char value[96]{};
    CCINIClass::INI_Rules->ReadString(type,key,fallback,value,sizeof(value));
    return value[0]?value:fallback;
}
bool integer(std::string_view text,int min,int max,int& out) {
    const auto first=text.find_first_not_of(" \t");
    if(first==text.npos) return false;
    text=text.substr(first,text.find_last_not_of(" \t")-first+1);
    const auto end=text.data()+text.size();
    const auto result=std::from_chars(text.data(),end,out);
    return result.ec==std::errc{} && result.ptr==end && out>=min && out<=max;
}
bool pair(const std::string& text,int min,int max,CellStruct& out) {
    const auto comma=text.find(',');
    int x,y;
    if(comma==text.npos || !integer(std::string_view(text).substr(0,comma),min,max,x) ||
       !integer(std::string_view(text).substr(comma+1),min,max,y)) return false;
    out={static_cast<short>(x),static_cast<short>(y)};
    return true;
}
Rules readRules(const char* type) {
    Rules rules;
    rules.useBuildingDocks=CCINIClass::INI_Rules->ReadBool(type,"Drone.UseBuildingDocks",false);
    auto mode=field(type,"Drone.Mode");
    if(mode.empty() || !_stricmp(mode.c_str(),"Native")) return rules;
    if(!_stricmp(mode.c_str(),"Guard")) {
        auto cell=field(type,"Drone.GuardCell");
        if(cell.empty() || pair(cell,0,511,rules.point)) rules.mode=Mode::Guard;
    }
    else if(!_stricmp(mode.c_str(),"Follow") && pair(field(type,"Drone.Offset","2,0"),-32,32,rules.point))
        rules.mode=Mode::Follow;
    else if(!_stricmp(mode.c_str(),"Orbit") && integer(field(type,"Drone.Radius","3"),1,32,rules.radius) &&
            integer(field(type,"Drone.Period","180"),30,3600,rules.period))
        rules.mode=Mode::Orbit;
    if(rules.mode==Mode::Native || !integer(field(type,"Drone.GuardRange","5"),1,32,rules.guardRange)) {
        rules.mode=Mode::Native;
        OutputDebugStringA(("ActSp: invalid drone configuration for ["+std::string(type)+"]\n").c_str());
    }
    if(rules.mode==Mode::Guard) {
        auto id=field(type,"Drone.GroundUnit");
        if(!id.empty()) {
            TechnoTypeClass* unit=UnitTypeClass::Find(id.c_str());
            if(!unit) unit=InfantryTypeClass::Find(id.c_str());
            if(unit && unit->Speed>0 && !unit->Naval && !unit->JumpJet && !unit->BalloonHover &&
               unit->SpeedType!=SpeedType::Winged && unit->MovementZone!=MovementZone::Fly &&
               integer(field(type,"Drone.GroundRespawnDelay","300"),0,1000000,rules.ground.delay)) {
                rules.ground.type=unit;
                rules.ground.initialPoint=rules.point;
                rules.ground.radius=rules.guardRange;
                rules.ground.useBuildingDocks=rules.useBuildingDocks && unit->WhatAmI()==AbstractType::UnitType;
            } else OutputDebugStringA(("ActSp: invalid ground guard for ["+std::string(type)+"]\n").c_str());
        }
    }
    return rules;
}
const Rules& rulesFor(TechnoClass* owner) {
    auto type=owner->GetTechnoType();
    auto [it,added]=profiles.try_emplace(type);
    if(added) it->second=readRules(type->ID);
    return it->second;
}

BuildingClass* dockHost(SpawnManagerClass* manager,AircraftClass* child) {
    auto host=abstract_cast<BuildingClass*>(manager->Owner);
    if(!host || !host->IsAlive || host->Health<=0 || host->InLimbo || !host->IsOnMap || !CCINIClass::INI_Rules ||
       !child || !child->IsAlive || child->Health<=0 || child->IsCrashing || child->SpawnOwner!=host || child->Owner!=host->Owner ||
       child->Type->MissileSpawn || host->Type->NumberOfDocks<=0 || child->Type->Dock.FindItemIndex(host->Type)<0 ||
       !rulesFor(host).useBuildingDocks) return nullptr;
    return host;
}

bool reserveDock(BuildingClass* host,AircraftClass* child) {
    return BuildingDocks::reserve(host,child,host->Type->NumberOfDocks);
}

bool parkNewSpawn(BuildingClass* host,AircraftClass* child) {
    if(host->CurrentMission==Mission::Construction || host->CurrentMission==Mission::Selling ||
       !host->ActuallyPlacedOnMap || !reserveDock(host,child)) return false;
    auto berth=host->GetDestination(child);
    if(!child->Unlimbo(berth,host->PrimaryFacing.Current().GetDir())) {
        child->SendCommand(RadioCommand::NotifyUnlink,host);
        return false;
    }
    child->ForceMission(Mission::Sleep);
    return true;
}

// A full airport must not send its own spawn to another airfield. Keep its
// current position until a real berth is available, then let Enter land it.
bool guideToDock(BuildingClass* host,AircraftClass* child) {
    if(!reserveDock(host,child)) {
        if(child->CurrentMission!=Mission::Sleep || child->Destination) {
            child->SetDestination(nullptr,true);
            child->Locomotor->Stop_Moving();
            child->ForceMission(Mission::Sleep);
        }
        return false;
    }
    const auto delta=child->GetCoords()-host->GetDestination(child);
    if(std::abs(delta.X)<=12 && std::abs(delta.Y)<=12 && std::abs(delta.Z)<=1) {
        child->ForceMission(Mission::Sleep);
        return true;
    }
    if(child->CurrentMission!=Mission::Enter || child->Destination!=host) {
        child->SetDestination(host,true);
        child->QueueMission(Mission::Enter,true);
    }
    return false;
}

const Rules* profile(SpawnManagerClass* manager,int slot) {
    auto owner=manager->Owner;
    const auto node=manager->SpawnedNodes[slot];
    auto child=node->Unit;
    if(!owner || !owner->Owner || node->IsSpawnMissile || !child || !child->Type || child->Type->MissileSpawn || child->SpawnOwner!=owner ||
       child->Owner!=owner->Owner || !owner->IsAlive || !owner->IsOnMap || owner->InLimbo || owner->Transporter)
        return nullptr;
    if(!CCINIClass::INI_Rules) return nullptr;
    const auto& rules=rulesFor(owner);
    if(rules.mode==Mode::Native || owner->Deactivated || owner->IsUnderEMP() ||
       owner->IsBeingWarpedOut() || owner->CurrentMission==Mission::Selling ||
       (owner->WhatAmI()==AbstractType::Building && !owner->IsPowerOnline())) return nullptr;
    return &rules;
}
CellClass* usable(int x,int y) {
    if(x<0 || y<0 || x>=512 || y>=512) return nullptr;
    CellStruct at{static_cast<short>(x),static_cast<short>(y)};
    auto& map=MapClass::Instance;
    return map.IsWithinUsableArea(at,false)?map.TryGetCellAt(at):nullptr;
}
CellClass* station(const Rules& rules,SpawnManagerClass* manager,int slot) {
    if(rules.mode==Mode::Guard) {
        const auto at=GuardPoints::station(manager->Owner->UniqueID,rules.point);
        return usable(at.X,at.Y);
    }
    const auto center=manager->Owner->GetMapCoords();
    int dx=rules.point.X,dy=rules.point.Y;
    if(rules.mode==Mode::Orbit) {
        // Fixed-point circle. Phase uses only saved game time and the native slot index.
        constexpr std::array<Point2D,16> circle={{{256,0},{237,98},{181,181},{98,237},
            {0,256},{-98,237},{-181,181},{-237,98},{-256,0},{-237,-98},{-181,-181},
            {-98,-237},{0,-256},{98,-237},{181,-181},{237,-98}}};
        const int frame=static_cast<unsigned>(Unsorted::CurrentFrame)%rules.period;
        const int phase=(16*frame/rules.period + 16*slot/manager->SpawnedNodes.Count)%16;
        auto cells=[&](int value){return (value*rules.radius+(value<0?-128:128))/256;};
        dx=cells(circle[phase].X);
        dy=cells(circle[phase].Y);
    }
    if(auto cell=usable(center.X+dx,center.Y+dy)) return cell;
    return usable(center.X,center.Y);
}
}
TechnoClass* GuardPoints::findTarget(TechnoClass* child,CellClass* point,int radius) {
    const auto center=point->MapCoords;
    TechnoClass* best=nullptr;
    int bestDistance=radius*radius+1;
    auto consider=[&](TechnoClass* candidate) {
        if(!candidate || !candidate->IsAlive || !candidate->IsOnMap || candidate->InLimbo ||
           candidate->Health<=0 || !candidate->Owner || child->Owner->IsAlliedWith(candidate->Owner)) return;
        const auto at=candidate->GetMapCoords();
        const int dx=at.X-center.X,dy=at.Y-center.Y,dist=dx*dx+dy*dy;
        if(dist>radius*radius || dist>bestDistance ||
           (dist==bestDistance && best && candidate->UniqueID>=best->UniqueID)) return;
        auto type=candidate->GetTechnoType();
        if(!type->LegalTarget || type->Invisible || (candidate->Disguised && !candidate->IsClearlyVisibleTo(child->Owner))) return;
        if(candidate->CloakState!=CloakState::Uncloaked) {
            const int h=child->Owner->ArrayIndex;
            auto cell=candidate->GetCell();
            if(h<0 || h>=24 || !cell || !cell->Sensors_InclHouse(h)) return;
        }
        const int slot=child->SelectWeapon(candidate);
        auto mount=slot>=0?child->GetWeapon(slot):nullptr;
        auto w=mount?mount->WeaponType:nullptr;
        if(!w || !w->Projectile || !(candidate->IsInAir()?w->Projectile->AA:w->Projectile->AG) ||
           !w->Warhead || !MapClass::GetTotalDamage(10000,w->Warhead,type->Armor,0)) return;
        const auto error=child->GetFireError(candidate,slot,true);
        if(error!=FireError::OK && error!=FireError::REARM && error!=FireError::FACING &&
           error!=FireError::ROTATING && error!=FireError::MOVING) return;
        best=candidate;
        bestDistance=dist;
    };
    for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x) {
        if(x*x+y*y>radius*radius) continue;
        auto cell=usable(center.X+x,center.Y+y);
        if(!cell) continue;
        for(auto head:{cell->FirstObject,cell->AltObject}) for(auto obj=head;obj;obj=obj->NextObject)
            if((obj->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None) consider(static_cast<TechnoClass*>(obj));
    }
    bool scansAir=false;
    for(int i=0;i<2;++i) {
        auto m=child->GetWeapon(i);
        if(m && m->WeaponType && m->WeaponType->Projectile && m->WeaponType->Projectile->AA) scansAir=true;
    }
    if(scansAir) {
        auto& tracker=AircraftTrackerClass::Instance;
        tracker.FillCurrentVector(point,radius);
        while(auto air=tracker.Get()) consider(air);
    }
    return best;
}
namespace {
bool patrol(AircraftClass* child,CellClass* point,int guardRange) {
    if(child->Ammo==0 || child->Health<=0 || child->InLimbo || child->IsCrashing ||
       child->IsUnderEMP() || child->IsBeingWarpedOut()) return false;
    if(auto threat=GuardPoints::findTarget(child,point,guardRange)) {
        if(child->Target!=threat || child->CurrentMission!=Mission::Attack) {
            child->SetTarget(threat);
            child->QueueMission(Mission::Attack,true);
        }
        return true;
    }
    if(child->Target) child->SetTarget(nullptr);
    if(child->GetMapCoords()==point->MapCoords && !child->Destination && child->CurrentMission==Mission::Guard) return true;
    if(child->Destination!=point || child->CurrentMission!=Mission::Move) {
        child->SetDestination(point,true);
        child->QueueMission(Mission::Move,true);
    }
    return true;
}
}

void GuardPoints::clearProfiles() { profiles.clear(); }
const GroundGuards::Config* GroundGuards::config(BuildingClass* building) {
    if(!CCINIClass::INI_Rules || building->SpawnManager) return nullptr;
    const auto& ground=rulesFor(building).ground;
    return ground.type?&ground:nullptr;
}
bool GuardPoints::eligible(TechnoClass* owner) {
    if(!owner || !owner->Owner || !owner->IsAlive || !owner->IsOnMap || owner->InLimbo ||
       !CCINIClass::INI_Rules) return false;
    auto manager=owner->SpawnManager;
    if(!manager) return owner->WhatAmI()==AbstractType::Building && GroundGuards::config(static_cast<BuildingClass*>(owner));
    return manager->Owner==owner && manager->SpawnCount>0 && manager->SpawnType && !manager->SpawnType->MissileSpawn &&
           rulesFor(owner).mode==Mode::Guard;
}

// Only relax the absent-target gate. Native launch timing, placement and node setup follow.
DEFINE_HOOK(0x6B72A2, ActSp_IdleLaunch, 5) {
    auto manager=R->ESI<SpawnManagerClass*>();
    auto child=R->EDI<AircraftClass*>();
    if(auto host=dockHost(manager,child);host && child->InLimbo && !parkNewSpawn(host,child))
        return 0x6B795A;
    if(manager->Target || manager->NewTarget) return 0;
    const int slot=R->EBX<int>();
    auto rules=profile(manager,slot);
    if(!rules || !station(*rules,manager,slot)) return 0;
    // Vanilla launch uses the carrier's primary weapon for the spawn origin.
    auto mount=manager->Owner->GetWeapon(0);
    return mount && mount->WeaponType && mount->WeaponType->Spawner ? 0x6B72AD : 0;
}

// AirportBound aircraft also ask for a home between missions. A busy pad
// still belongs to their carrier; reporting no airfield can crash the plane.
DEFINE_HOOK(0x41BBD3,ActSp_FindSpawnHomeAirport,6) {
    auto child=R->ESI<AircraftClass*>();
    auto owner=child->SpawnOwner;
    if(owner && owner->SpawnManager) {
        if(auto host=dockHost(owner->SpawnManager,child)) {
            child->DockNowHeadingTo=host;
            R->EAX(host);
            return 0x41BC22;
        }
    }
    return 0;
}

// EnterIdleMode requests a berth immediately and crashes AirportBound
// aircraft if the radio link is refused. The spawn manager retries this one.
DEFINE_HOOK_AGAIN(0x417B72,ActSp_WaitForSpawnDock,6)
DEFINE_HOOK(0x4179E7,ActSp_WaitForSpawnDock,6) {
    auto child=R->ESI<AircraftClass*>();
    auto owner=child->SpawnOwner;
    if(owner && owner->SpawnManager && dockHost(owner->SpawnManager,child)) {
        R->EDI(static_cast<int>(Mission::Sleep));
        return 0x417B44;
    }
    return 0;
}

// The native launch timer and target selection still apply. An aircraft
// already on a pad can start its mission without being placed a second time.
DEFINE_HOOK(0x6B73FC,ActSp_LaunchFromBuildingDock,5) {
    auto manager=R->ESI<SpawnManagerClass*>();
    auto child=R->EDI<AircraftClass*>();
    auto host=dockHost(manager,child);
    if(!host || child->InLimbo || !child->IsOnMap) return 0;
    BuildingDocks::release(host,child);
    manager->SpawnedNodes[R->EBX<int>()]->Status=SpawnNodeStatus::Preparing;
    return 0x6B757A;
}

// Run before the stock/Phobos distance-to-carrier check. Airport berths
// may lie several cells from the carrier's origin and at a different height.
DEFINE_HOOK(0x6B7793,ActSp_ReturnToBuildingDock,7) {
    auto manager=R->ESI<SpawnManagerClass*>();
    auto child=R->EDI<AircraftClass*>();
    auto host=dockHost(manager,child);
    if(!host) return 0;
    return guideToDock(host,child) ? 0x6B7809 : 0x6B795A;
}

// Just before the native return order for a preparing/attacking aircraft.
// The ordinary absent-target patrol only applies to aircraft with ammunition.
DEFINE_HOOK(0x6B7663, ActSp_NoTargetPatrol, 5) {
    auto manager=R->ESI<SpawnManagerClass*>();
    auto child=R->EDI<AircraftClass*>();
    const int slot=R->EBX<int>();
    if(!manager->Target && !manager->NewTarget) {
        if(auto rules=profile(manager,slot)) {
            auto point=station(*rules,manager,slot);
            if(point && patrol(child,point,rules->guardRange)) {
                manager->SpawnedNodes[slot]->Status=SpawnNodeStatus::Preparing;
                return 0x6B795A;
            }
        }
    }
    if(auto host=dockHost(manager,child)) {
        child->SetTarget(nullptr);
        manager->SpawnedNodes[slot]->Status=SpawnNodeStatus::Returning;
        return guideToDock(host,child) ? 0x6B7809 : 0x6B795A;
    }
    return 0;
}
