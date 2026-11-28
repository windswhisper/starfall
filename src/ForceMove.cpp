#include "ForceMove.h"
#include "../modules/ActSp/HighPlatforms.h"
#include <OverlayTypeClass.h>
#include <tuple>
#include "UnitMount.h"
#include "UnitHealth.h"
#include <AircraftTrackerClass.h>
#include <BuildingClass.h>
#include <HouseClass.h>
#include <InfantryClass.h>
#include <LocomotionClass.h>
#include <MapClass.h>
#include <RulesClass.h>
#include <TacticalClass.h>
#include <TargetClass.h>
#include <UnitClass.h>
#include <Unsorted.h>
#include <set>
#include <vector>

using std::vector;

// Keep locomotion and cell occupation on the ground; targeting uses the lift
// and a temporary registration in the engine's aerial search grid.
namespace ForceMove {
std::map<DWORD,Motion> motions;
const Motion* find(const AbstractClass* unit) {
    if(!unit || motions.empty()) return nullptr;
    auto it=motions.find(unit->UniqueID);
    return it==motions.end()?nullptr:&it->second;
}
int screenLift(const AbstractClass* unit) {
    const auto motion=find(unit);
    return motion?TacticalClass::AdjustForZ(motion->lift()):0;
}
int ejectionLift(const AbstractClass* unit) {
    const auto m=find(unit);
    return m && m->landing && m->landing->openHeight ? m->lift() : 0;
}
bool airborne(const AbstractClass* unit) {
    if(UnitMount::mounted(unit)) {
        const auto& link=UnitMount::links.at(unit->UniqueID);
        return link.parent && !link.parent->InLimbo && link.parent->IsOnMap && link.parent->IsInAir();
    }
    auto motion=find(unit);
    return motion && motion->airborne();
}
using AirGetter=bool(__thiscall*)(const AbstractClass*);
using CoordsGetter=CoordStruct*(__thiscall*)(const AbstractClass*,CoordStruct*);
template<class T> AirGetter originalAirGetter=nullptr;
template<class T,int Slot> CoordsGetter originalCoordsGetter=nullptr;
template<class T> bool __fastcall queryAir(const AbstractClass* unit,void*) {
    if(UnitMount::mounted(unit)) return airborne(unit);
    if(auto motion=find(unit)) return motion->airborne();
    return originalAirGetter<T>(unit);
}
const AbstractClass* coordinateQuery=nullptr;
template<class T,int Slot> CoordStruct* __fastcall queryCoords(const AbstractClass* unit,void*,CoordStruct* out) {
    // An extension may implement GetTargetCoords through GetCenterCoords.
    // Apply the offset once while preserving its existing coordinate offsets.
    const auto previous=coordinateQuery;coordinateQuery=unit;
    auto result=originalCoordsGetter<T,Slot>(unit,out);
    coordinateQuery=previous;
    if(previous!=unit) if(auto motion=find(unit)) result->Z+=motion->lift();
    return result;
}
template<class T,int Slot,class Getter> void installGetter(Getter& original,Getter replacement) {
    if(original) return;
    auto slot=reinterpret_cast<Getter*>(T::AbsVTable+Slot);
    DWORD protection;
    if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&protection)) {
        OutputDebugStringA("Starfall: could not install forced-move targeting query\n");return;
    }
    original=*slot;*slot=replacement;
    VirtualProtect(slot,sizeof(*slot),protection,&protection);
}
template<class T> void installTargeting() {
    installGetter<T,0x54>(originalAirGetter<T>,reinterpret_cast<AirGetter>(&queryAir<T>));
    installGetter<T,0x58>(originalCoordsGetter<T,0x58>,reinterpret_cast<CoordsGetter>(&queryCoords<T,0x58>));
    installGetter<T,0xA4>(originalCoordsGetter<T,0xA4>,reinterpret_cast<CoordsGetter>(&queryCoords<T,0xA4>));
}
void installTargeting() {
    installTargeting<UnitClass>();
    installTargeting<InfantryClass>();
}
bool nativeInAir(const TechnoClass* unit) {
    auto getter=unit->WhatAmI()==AbstractType::Unit?originalAirGetter<UnitClass>:originalAirGetter<InfantryClass>;
    return getter?getter(unit):unit->IsInAir();
}
DynamicVectorClass<TechnoClass*>& trackerVector(int index) {
    return AircraftTrackerClass::Instance.TrackerVectors[index/20][index%20];
}
int locateTracker(TechnoClass* unit) {
    for(int i=0;i<400;++i) if(trackerVector(i).FindItemIndex(unit)>=0) return i;
    return -1;
}
void removeTracker(Motion& motion) {
    if(motion.trackerIndex>=0 && motion.foot) {
        while(trackerVector(motion.trackerIndex).Remove(motion.foot)) {}
        while(AircraftTrackerClass::Instance.CurrentVector.Remove(motion.foot)) {}
    }
    motion.trackerIndex=-1;
}
void syncAirTracker(Motion& motion) {
    auto foot=motion.foot;
    if(!foot) return;
    const bool tracked=motion.airborne() || motion.nativeTracked;
    int index=tracked?AircraftTrackerClass::Instance.GetVectorIndex(foot->GetMapCoords()):-1;
    // Phobos also uses this native sentinel to exclude tracker entries from
    // the ground damage pass. Clear it when a borrowed registration ends.
    if(index<0 && motion.trackerIndex>=0) foot->SetLastFlightMapCoords(CellStruct::Empty);
    if(index!=motion.trackerIndex) removeTracker(motion);
    if(index>=0) {
        trackerVector(index).AddUnique(foot);
        foot->SetLastFlightMapCoords(foot->GetMapCoords());
        motion.trackerIndex=index;
    }
}
void forget(DWORD id) {
    ejectOrders.erase(id);descending.erase(id);
    if(auto it=motions.find(id);it!=motions.end()) {
        removeTracker(it->second);
        motions.erase(it);
    }
}
void clearMotions() {
    ejectOrders.clear();descending.clear();
    for(auto& [id,motion]:motions) if(!motion.nativeTracked) removeTracker(motion);
    motions.clear();
}
std::map<DWORD,SettlingTilt> settling;
bool tilts(const Motion& motion) {
    return motion.tilt && (motion.dx || motion.dy);
}
void releaseTilt(FootClass* foot,const Motion& motion) {
    if(tilts(motion) && foot->WhatAmI()==AbstractType::Unit && foot->GetTechnoType()->Voxel)
        settling[foot->UniqueID]={motion.dx,motion.dy,static_cast<unsigned>(Unsorted::CurrentFrame)};
}
Lean visualTilt(UnitClass* unit) {
    int dx,dy;
    double angle=maxTilt;
    auto active=motions.find(unit->UniqueID);
    if(active!=motions.end() && tilts(active->second)) {
        dx=active->second.dx;dy=active->second.dy;
    } else {
        auto it=settling.find(unit->UniqueID);
        if(it==settling.end()) return {};
        unsigned elapsed=static_cast<unsigned>(Unsorted::CurrentFrame)-it->second.start;
        if(elapsed>=settleFrames) return {};
        double t=static_cast<double>(elapsed)/settleFrames;
        angle*=1-t*t*(3-2*t);
        dx=it->second.dx;dy=it->second.dy;
    }
    // Model X points forward and model Y left. Map Y increases southward.
    double facing=unit->PrimaryFacing.Current().GetRadian<32>();
    double c=std::cos(facing),s=std::sin(facing),length=std::hypot(dx,dy);
    double forward=(c*dx-s*dy)/length,right=(s*dx+c*dy)/length;
    double sine=std::sin(angle);
    return {static_cast<float>(std::asin(sine*forward)),static_cast<float>(std::atan2(sine*right,std::cos(angle)))};
}
bool locked(const AbstractClass* unit) {
    return !motions.empty() && unit && motions.contains(unit->UniqueID);
}
bool unreserved(const AbstractClass* unit) {
    if(UnitMount::mounted(unit)) return true;
    if(!unit || motions.empty()) return false;
    auto it=motions.find(unit->UniqueID);
    return it!=motions.end() && it->second.ignoreCollision;
}
bool eligible(TechnoClass* unit) {
    if(UnitMount::mounted(unit) || !unit || !unit->IsAlive || unit->Health<=0 || !unit->IsOnMap || unit->InLimbo || unit->Transporter ||
       unit->Absorbed || unit->IsImmobilized || unit->IsBeingWarpedOut() ||
       unit->IsSinking || unit->IsCrashing) return false;
    if(unit->WhatAmI()!=AbstractType::Unit && unit->WhatAmI()!=AbstractType::Infantry) return false;
    if(nativeInAir(unit)) return false;
    auto foot=static_cast<FootClass*>(unit);
    return foot->Locomotor && foot->TubeIndex<0 && !foot->IsAttackedByLocomotor && !foot->IsDeploying;
}
bool terrainAllows(FootClass* foot,CellClass* cell) {
    auto type=foot->GetTechnoType();
    if(!cell || type->SpeedType<SpeedType::Foot || type->SpeedType>SpeedType::FloatBeach) return false;
    auto land=foot->OnBridge?LandType::Clear:cell->LandType;
    if(land<LandType::Clear || static_cast<size_t>(land)>=std::size(GroundType::Array) ||
       GroundType::Array[static_cast<int>(land)].Cost[static_cast<int>(type->SpeedType)]<=0) return false;
    return !cell->GetBuilding() && !cell->GetTerrain(foot->OnBridge) &&
        cell->IsClearToMove(type->SpeedType,true,true,-1,type->MovementZone,-1,foot->OnBridge);
}
bool underneath(FootClass* foot,ObjectClass* obj) {
    if(!obj || obj==foot || !obj->IsAlive || obj->Health<=0 || !obj->IsOnMap || obj->InLimbo ||
       obj->OnBridge!=foot->OnBridge || unreserved(obj)) return false;
    auto kind=obj->WhatAmI();
    return (kind==AbstractType::Infantry || kind==AbstractType::Unit || kind==AbstractType::Aircraft) &&
        !obj->IsInAir() && !static_cast<TechnoClass*>(obj)->Transporter;
}
bool clearForSolid(FootClass* foot) {
    auto cell=foot->GetCell();
    if(!terrainAllows(foot,cell)) return false;
    for(auto obj=foot->OnBridge?cell->AltObject:cell->FirstObject;obj;obj=obj->NextObject)
        if(underneath(foot,obj)) return false;
    return true;
}
bool start(TechnoClass* unit,const Spec& spec,const CoordStruct& center) {
    if(!eligible(unit)) return false;
    if(spec.blowWeight && !(unit->GetTechnoType()->Weight < *spec.blowWeight)) return false;
    if(!spec.affectsIronCurtain && unit->IsIronCurtained() && !unit->ForceShielded) return false;
    auto foot=static_cast<FootClass*>(unit);
    auto origin=foot->Location;
    Motion motion;
    motion.foot=foot;motion.x=origin.X;motion.y=origin.Y;
    motion.dx=spec.dx;motion.dy=spec.dy;
    if(spec.distance) {
        double dx=origin.X-center.X,dy=origin.Y-center.Y,len=std::hypot(dx,dy);
        if(len>0) {
            double scale=(spec.mode==Mode::Pull?-std::min<double>(len,spec.distance):spec.distance)/len;
            motion.dx=static_cast<int>(std::lround(dx*scale));
            motion.dy=static_cast<int>(std::lround(dy*scale));
        }
    }
    motion.lastFrame=Unsorted::CurrentFrame;
    motion.height=spec.height;motion.duration=spec.duration;motion.tilt=spec.tilt;motion.easing=spec.easing;
    motion.airborneHeight=spec.airborneHeight;
    if(!motion.dx && !motion.dy && !motion.height) return false;
    motion.ignoreCollision=spec.ignoreCollision;
    auto previous=motions.find(unit->UniqueID);
    bool wasUnreserved=unreserved(foot);
    if(previous!=motions.end()) {
        motion.startHeight=previous->second.lift();
        motion.trackerIndex=previous->second.trackerIndex;motion.nativeTracked=previous->second.nativeTracked;
        if(wasUnreserved && !spec.ignoreCollision && !clearForSolid(foot)) return false;
    } else {
        if(!interruptTravel(foot)) return false;
        motion.trackerIndex=locateTracker(foot);motion.nativeTracked=motion.trackerIndex>=0;
    }
    if(!wasUnreserved && spec.ignoreCollision) foot->UnmarkAllOccupationBits(foot->Location);
    if(tilts(motion)) settling.erase(unit->UniqueID);
    else if(auto old=motions.find(unit->UniqueID);old!=motions.end()) releaseTilt(foot,old->second);
    motions[unit->UniqueID]=motion;
    syncAirTracker(motions.at(unit->UniqueID));
    if(wasUnreserved && !spec.ignoreCollision) foot->MarkAllOccupationBits(foot->Location);
    return true;
}
// Sweep even one-frame moves in short steps to catch intervening cells.
bool moveTo(FootClass* foot,int x,int y,bool ignoreCollision,std::optional<int> jumpZ) {
    auto from=foot->Location,pos=from;
    auto cell=foot->GetCell();
    bool bridge=foot->OnBridge;
    auto type=foot->GetTechnoType();
    int dx=x-from.X,dy=y-from.Y;
    int steps=(std::max(std::abs(dx),std::abs(dy))+31)/32;
    for(int i=1;i<=steps;++i) {
        CoordStruct next{from.X+static_cast<int>(int64_t(dx)*i/steps),from.Y+static_cast<int>(int64_t(dy)*i/steps),pos.Z};
        auto p=CellClass::Coord2Cell(next);
        auto& map=MapClass::Instance;
        if(next.X<0 || next.Y<0 || next.X>=mapExtent || next.Y>=mapExtent || !map.IsWithinUsableArea(p,false)) break;
        auto dest=map.TryGetCellAt(p);
        if(!dest) break;
        if(dest!=cell) {
            bool deck=dest->ContainsBridge() && (bridge || cell->Level>=dest->Level+CellClass::BridgeLevels-1);
            auto speed=type->SpeedType;
            int level=dest->Level+(deck?CellClass::BridgeLevels:0);
            if(!ignoreCollision && (std::abs(level-cell->Level-(bridge?CellClass::BridgeLevels:0))>1 ||
               speed<SpeedType::Foot || speed>SpeedType::FloatBeach || dest->GetBuilding() ||
               (deck?dest->AltObject:dest->FirstObject) ||
               !dest->IsClearToMove(speed,false,false,-1,type->MovementZone,-1,deck) ||
               foot->IsCellOccupied(dest,FacingType::None,level,cell,false)!=Move::OK)) break;
            bridge=deck;cell=dest;
        }
        next.Z=map.GetCellFloorHeight(next);
        if(jumpZ) bridge=dest->ContainsBridge() && *jumpZ>=next.Z+CellClass::BridgeHeight;
        if(bridge) next.Z+=CellClass::BridgeHeight;
        pos=next;
    }
    if(pos!=from) {
        foot->Mark(MarkType::Up);
        foot->OnBridge=bridge;
        foot->SetLocation(pos);
        foot->Mark(MarkType::Down);
        syncCellRegistration(foot);
    }
    return pos.X==x && pos.Y==y;
}
void crush(TechnoClass* victim,TechnoClass* falling) {
    int damage=UnitHealth::read(victim).current;
    victim->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,falling,true,false,falling?falling->Owner:nullptr);
}
// A death weapon or a passive can remove either participant during damage.
// Re-resolve native handles before using them again.
bool land(FootClass* foot) {
    auto cell=foot->GetCell();
    TargetClass mover(foot);
    auto building=cell->GetBuilding();
    if(building && building->IsAlive && building->Health>0) {
        int damage=UnitHealth::read(foot).current;
        building->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,foot,false,false,foot->Owner);
        foot=mover.As_Foot();
        if(foot && foot->IsAlive && foot->Health>0) crush(foot,nullptr);
        return false;
    }
    vector<TargetClass> victims;
    for(auto obj=foot->OnBridge?cell->AltObject:cell->FirstObject;obj;obj=obj->NextObject)
        if(underneath(foot,obj)) victims.emplace_back(obj);
    for(auto& handle:victims) {
        auto victim=handle.As_Techno();
        if(underneath(foot,victim)) crush(victim,foot);
        foot=mover.As_Foot();
        if(!foot || !foot->IsAlive || foot->Health<=0 || foot->InLimbo) return false;
    }
    if(!clearForSolid(foot)) {crush(foot,nullptr);return false;}
    return true;
}
#include "VehicleJump.inc"
#include "PassengerEjection.inc"

void resume(FootClass* foot) {
    foot->ClearMegaMissionData();
    foot->NavQueue.Count=0;
    foot->SetTarget(nullptr);
    foot->AbortMotion();
    foot->QueuedMission=Mission::None;
    foot->SuspendedMission=Mission::None;
    foot->ForceMission(Mission::Guard);
    foot->EnterIdleMode(false,true);
}
void tick() {
    auto frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    std::erase_if(settling,[frame](const auto& e){return frame-e.second.start>=settleFrames;});
    tickEjections(frame);
    if(motions.empty()) return;
    // A restored motion has no native pointer. Resolve all of them in one scan.
    if(std::any_of(motions.begin(),motions.end(),[](const auto& e){return !e.second.foot;}))
        for(auto foot:FootClass::Array) if(foot) {
            auto found=motions.find(foot->UniqueID);
            if(found!=motions.end() && !found->second.foot) {
                found->second.foot=foot;
                found->second.trackerIndex=locateTracker(foot);
                if(!foot->InLimbo) syncAirTracker(found->second);
            }
        }
    vector<DWORD> completed;
    for(auto it=motions.begin();it!=motions.end();) {
        auto& m=it->second;
        auto foot=m.foot;
        // A completely full map can gain free cells again. Rate-limit the
        // emergency search, including while restoring a failed native opening.
        if(m.landing && m.landing->openHeight && m.blocked && m.elapsed==m.duration-1 &&
           frame-m.lastFrame<15) {++it;continue;}
        if(m.landing && m.landing->openHeight && foot && foot->IsAlive && foot->InLimbo && !foot->Transporter) {
            completed.push_back(it->first);++it;continue;
        }
        if(!eligible(foot)) {
            bool ghost=m.ignoreCollision;
            if(!m.nativeTracked) {
                if(foot && m.trackerIndex>=0) foot->SetLastFlightMapCoords(CellStruct::Empty);
                removeTracker(m);
            }
            it=motions.erase(it);
            if(foot && foot->IsAlive && foot->Health>0 && foot->IsOnMap && !foot->InLimbo && !foot->Transporter) {
                if(ghost) foot->MarkAllOccupationBits(foot->Location);
                if(foot->CurrentMission==Mission::Sleep) foot->ForceMission(Mission::Guard);
            }
            continue;
        }
        auto now=static_cast<unsigned>(Unsorted::CurrentFrame);
        if(m.lastFrame==now) {++it;continue;}
        m.lastFrame=now;
        ++m.elapsed;
        if(!m.blocked) {
            auto pos=m.position();
            m.blocked=!moveTo(foot,pos.X,pos.Y,m.ignoreCollision,m.landing?std::optional<int>(m.jumpZ()):std::nullopt);
        }
        if(m.landing && m.landing->openHeight) playEjectionCrawl(foot);
        syncAirTracker(m);
        foot->MarkForRedraw();
        if(m.elapsed==m.duration || (!m.ignoreCollision && m.blocked && !m.height && !m.startHeight)) {
            completed.push_back(it->first);
        }
        ++it;
    }
    // Complete all movement before applying impacts. Several objects may
    // land this frame, and explosions may invalidate other queued landings.
    for(auto id:completed) {
        auto it=motions.find(id);
        if(it==motions.end()) continue;
        auto motion=it->second;
        auto foot=motion.foot;
        // Drive locomotors assume a stationary vehicle is at its cell center.
        // Keep the landing in the same cell so collision and bridge handling
        // still use the actual destination reached by the motion.
        if(foot->WhatAmI()==AbstractType::Unit) {
            auto pos=foot->Location;
            moveTo(foot,(pos.X/256)*256+128,(pos.Y/256)*256+128,motion.ignoreCollision);
        }
        if(motion.landing && motion.landing->openHeight) {
            finishEjection(id,motion);
            continue;
        }
        bool survived=!motion.ignoreCollision || (motion.landing?landJump(foot,motion):land(foot));
        if(motion.landing && !survived) continue;
        motions.erase(id);
        if(!survived) continue;
        if(motion.ignoreCollision) foot->MarkAllOccupationBits(foot->Location);
        if(motion.landing) playJumpEffect(motion.landing->effect,foot->Location,foot->Owner);
        releaseTilt(foot,motion);
        TargetClass handle(foot);
        foot->UpdatePosition(PCPType::During);
        if(auto live=handle.As_Foot();live && live->IsAlive && live->Health>0) resume(live);
    }
}
}

// Keep in-transit objects in the cell list for targeting, without setting or
// clearing the native occupation bits belonging to a stationary object below.
// Aerial scans normally skip the ground rendering layer even for entries in
// AircraftTracker. Forced lift retains that layer for locomotion and drawing.
DEFINE_HOOK(0x6F925C,SF_ForceMoveAirScan,8) {
    return ForceMove::airborne(R->EBP<AbstractClass*>())?0x6F926D:0;
}

// Native aerial splash reads Location directly, rather than GetTargetCoords.
// Use a local coordinate copy for both distance checks; retain Phobos' height
// and cylinder filters. The ground pass must not damage the same unit twice.
DEFINE_HOOK(0x48941C,SF_ForceMoveAirDamageCoords,7) {
    auto unit=R->EBX<TechnoClass*>();
    if(ForceMove::airborne(unit)) {
        R->Stack<CoordStruct>(0xB0,unit->GetTargetCoords());
        R->ESI(R->ESP()+0xB0);
    }
    return 0;
}
DEFINE_HOOK(0x489702,SF_ForceMoveGroundDamage,6) {
    return ForceMove::airborne(R->ESI<AbstractClass*>())?0x4899B3:0;
}

DEFINE_HOOK_AGAIN(0x5217C0,SF_ForceMoveOccupation,8)
DEFINE_HOOK_AGAIN(0x521850,SF_ForceMoveOccupation,7)
DEFINE_HOOK_AGAIN(0x744210,SF_ForceMoveOccupation,5)
DEFINE_HOOK(0x7441B0,SF_ForceMoveOccupation,5) {
    return ForceMove::unreserved(R->ECX<AbstractClass*>())?0x74420B:0;
}

// DrawIt takes a screen point by reference. Keep the native position grounded.
DEFINE_HOOK_AGAIN(0x518F90,SF_ForceMoveDraw,7)
DEFINE_HOOK(0x73CEC0,SF_ForceMoveDraw,5) {
    if(const int lift=ForceMove::screenLift(R->ECX<TechnoClass*>())) {
        static Point2D location;
        location=*R->Stack<Point2D*>(4);
        location.Y-=lift;
        R->Stack(4,&location);
    }
    return 0;
}
// DrawAsVXL has finished the body. Its separate shadow origin is shared by
// vanilla and Phobos, before bridge handling and locomotor shadow offsets.
DEFINE_HOOK(0x73C216,SF_ForceMoveVoxelShadow,8) {
    R->EDX(R->EDX<int>()+ForceMove::screenLift(R->EBP<UnitClass*>()));
    return 0;
}
// A grounded voxel can bake its shadow into the body surface. Draw it on the
// scene instead for any visible lift, even below the air-targeting threshold.
DEFINE_HOOK(0x73C41B,SF_ForceMoveVoxelShadowSurface,5) {
    return ForceMove::screenLift(R->EBP<UnitClass*>())?0x73C427:0;
}
// Airborne infantry already drew their native drop shadow in DrawIt.
DEFINE_HOOK_AGAIN(0x70653E,SF_ForceMoveSpriteShadow,10)
DEFINE_HOOK(0x706469,SF_ForceMoveSpriteShadow,10) {
    auto unit=R->ESI<TechnoClass*>();
    return ForceMove::screenLift(unit) && unit->WhatAmI()==AbstractType::Infantry?0x706602:0;
}
// The native airborne infantry shadow uses height above the bridge or ground.
// ForceMove keeps physical height grounded, so open this draw path explicitly
// without changing the height used for its depth test.
DEFINE_HOOK(0x5194E7,SF_ForceMoveInfantryShadowVisible,8) {
    auto unit=R->EBP<InfantryClass*>();
    if(ForceMove::screenLift(unit) && Game::bDrawShadow && !unit->Type->NoShadow &&
       unit->CloakState==CloakState::Uncloaked)
        return 0x5194EF;
    return 0;
}
DEFINE_HOOK(0x51953B,SF_ForceMoveInfantryAirShadow,6) {
    R->EAX(R->EAX<int>()+ForceMove::screenLift(R->EBP<InfantryClass*>()));
    return 0;
}
// UnitClass::DrawIt is about to call DrawAsVXL with the projected point,
// clipping rectangle, brightness and zero tint. Scope the extra rotations to
// this draw so forced lean never trips gameplay checks for a tipping unit.
DEFINE_HOOK(0x73D32B,SF_ForceMoveVoxelTilt,6) {
    if(ForceMove::motions.empty() && ForceMove::settling.empty()) return 0;
    auto unit=R->ESI<UnitClass*>();
    auto lean=ForceMove::visualTilt(unit);
    if(!lean.forward && !lean.side) return 0;
    const float forward=unit->AngleRotatedForwards,side=unit->AngleRotatedSideways;
    unit->AngleRotatedForwards=forward+lean.forward;
    unit->AngleRotatedSideways=side+lean.side;
    unit->DrawAsVXL(R->Stack<Point2D>(0x1C),*R->EBX<RectangleStruct*>(),R->EBP<int>(),0);
    unit->AngleRotatedForwards=forward;
    unit->AngleRotatedSideways=side;
    return 0x73D395;
}
