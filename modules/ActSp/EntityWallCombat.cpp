#include "EntityWalls.h"
#include "Piercing.h"
#include "../../src/ProjectileShield.h"
#include <BuildingClass.h>
#include <BulletClass.h>
#include <HouseClass.h>
#include <InfantryClass.h>
#include <MapClass.h>
#include <TargetClass.h>
#include <RulesClass.h>
#include <WarheadTypeClass.h>
#include <WeaponTypeClass.h>
#include <algorithm>
#include <cstdint>

namespace EntityWalls {
namespace {
constexpr int tile=Unsorted::LeptonsPerCell;
struct Fraction {
    int64_t n,d;
    bool operator<(const Fraction& other) const { return n*other.d<other.n*d; }
};
struct Impact {
    BuildingClass* building=nullptr;
    CoordStruct point{};
    bool physical=false;
    double fraction=1;
};

bool valid(CellStruct at) {
    return static_cast<unsigned>(at.X)<512 && static_cast<unsigned>(at.Y)<512;
}

// Visit cells in flight order. Entry/exit fractions belong to the whole step,
// so a vertical shot or a descending arc can hit the roof inside a single cell.
template<typename Visitor>
Impact trace(const CoordStruct& from,const CoordStruct& to,Visitor visit) {
    if(from.X<0 || from.Y<0 || from.X>=512*tile || from.Y>=512*tile) return {};
    auto at=CellClass::Coord2Cell(from);
    const auto endCell=CellClass::Coord2Cell(to);
    const int64_t dx=int64_t(to.X)-from.X,dy=int64_t(to.Y)-from.Y;
    const int sx=(dx>0)-(dx<0),sy=(dy>0)-(dy<0);
    const Fraction end{1,1},beyond{2,1};
    Fraction x=sx?Fraction{sx>0?(at.X+1)*tile-from.X:from.X-at.X*tile,std::abs(dx)}:beyond;
    Fraction y=sy?Fraction{sy>0?(at.Y+1)*tile-from.Y:from.Y-at.Y*tile,std::abs(dy)}:beyond;
    Fraction enter{0,1};
    while(valid(at)) {
        const auto leave=std::min({x,y,end});
        auto cell=MapClass::Instance.TryGetCellAt(at);
        auto building=cell && cell->FirstObject?cell->GetBuilding():nullptr;
        if(building && building->IsAlive && building->Health>0 && !building->InLimbo) {
            auto hit=visit(building,cell,enter,leave);
            if(hit.building) return hit;
        }
        // Include a positive-direction boundary endpoint in its destination cell.
        const bool endpoint=!(leave<end);
        if(endpoint && (at==endCell || (end<x && end<y))) break;
        const bool crossX=!(y<x) && (!endpoint || at.X!=endCell.X);
        const bool crossY=!(x<y) && (!endpoint || at.Y!=endCell.Y);
        if(!crossX && !crossY) break;
        if(crossX) { at.X+=sx; x.n+=tile; }
        if(crossY) { at.Y+=sy; y.n+=tile; }
        enter=leave;
    }
    return {};
}

struct WallShot {
    TechnoClass* firer;
    AbstractClass* target;
    CellStruct start,end;
    bool cliffs,walls;
};
bool wallBlocks(BuildingClass* wall,CellClass* cell,const WallShot& shot) {
    if(cell->MapCoords==shot.start || wall==shot.target || wall==shot.firer || cell==shot.target) return false;
    auto spec=rules(wall->Type);
    if(!spec) return false;
    if(spec->highWall && shot.cliffs) return true;
    if(!spec->blocksProjectiles || !shot.walls || !valid(shot.start) || !valid(shot.end)) return false;
    auto& map=MapClass::Instance;
    return map.GetCellAt(shot.start)->Level<=map.GetCellAt(shot.end)->Level &&
        !(RulesClass::Instance->AlliedWallTransparency && wall->Owner && shot.firer && wall->Owner->IsAlliedWith(shot.firer->Owner));
}

Impact contact(BuildingClass* building,CellStruct cell,const BlockerRules& spec,
               const CoordStruct& from,const CoordStruct& to,Fraction enter,Fraction leave) {
    const int64_t dz=int64_t(to.Z)-from.Z;
    const int64_t low=building->Location.Z;
    const int64_t high=std::min<int64_t>(INT_MAX,low+int64_t(spec.height)*Unsorted::LevelHeight);
    const auto zAtEntry=dz*enter.n;
    if(zAtEntry<(low-from.Z)*enter.d) {
        if(dz<=0) return {};
        enter={low-from.Z,dz};
    } else if(zAtEntry>(high-from.Z)*enter.d) {
        if(dz>=0) return {};
        enter={int64_t(from.Z)-high,-dz};
    }
    if(leave<enter) return {};
    auto lerp=[enter](int a,int b) { return int(a+(int64_t(b)-a)*enter.n/enter.d); };
    // The east/south face belongs to the next map cell at the exact boundary.
    // Put damage one lepton inside the struck foundation in that case.
    return {building,{std::clamp(lerp(from.X,to.X),cell.X*tile,(cell.X+1)*tile-1),
                      std::clamp(lerp(from.Y,to.Y),cell.Y*tile,(cell.Y+1)*tile-1),
                      lerp(from.Z,to.Z)},true,double(enter.n)/enter.d};
}
bool launchesFrom(BuildingClass* building,TechnoClass* firer) {
    if(!firer) return false;
    if(firer==building || firer->Transporter==building || firer->BunkerLinkedItem==building) return true;
    return firer->WhatAmI()==AbstractType::Infantry &&
        building->Occupants.FindItemIndex(static_cast<InfantryClass*>(firer))>=0;
}
bool physicalRound(BulletClass* bullet) {
    const auto w=bullet->WeaponType;
    return !bullet->Type->Inviso && !(w && (w->IsLaser || w->DiskLaser || w->IsElectricBolt || w->IsRadBeam || w->IsRailgun || w->IsSonic));
}
Impact firstImpact(BulletClass* bullet,const CoordStruct& from,const CoordStruct& to,bool includeLegacy,bool piercing=false) {
    const bool physical=hasProjectileBlockers() && physicalRound(bullet);
    const bool legacy=includeLegacy && hasBarriers() && (bullet->Type->SubjectToWalls || bullet->Type->SubjectToCliffs);
    if(!physical && !legacy) return {};
    const WallShot shot{bullet->Owner,piercing?nullptr:bullet->Target,CellClass::Coord2Cell(bullet->SourceCoords),CellClass::Coord2Cell(bullet->TargetCoords),
        bullet->Type->SubjectToCliffs,bullet->Type->SubjectToWalls};
    auto house=physical?firingHouse(bullet):nullptr;
    return trace(from,to,[&](BuildingClass* b,CellClass* cell,Fraction enter,Fraction leave)->Impact {
        if(auto spec=blockerRules(b->Type)) {
            if(physical && !launchesFrom(b,bullet->Owner) &&
               (spec->blockAllied || !house || !b->Owner || !b->Owner->IsAlliedWith(house)))
                return contact(b,cell->MapCoords,*spec,from,to,enter,leave);
        } else if(legacy && wallBlocks(b,cell,shot)) {
            auto point=b->GetCoords();
            if(piercing) {
                auto atContact=[enter](int a,int z){ return int(a+(int64_t(z)-a)*enter.n/enter.d); };
                point={atContact(from.X,to.X),atContact(from.Y,to.Y),atContact(from.Z,to.Z)};
            }
            return {b,point,false,double(enter.n)/enter.d};
        }
        return {};
    });
}

void applyHit(BulletClass* bullet,const Impact& hit) {
    bullet->SetTarget(hit.building);
    bullet->TargetCoords=hit.point;
    bullet->SetLocation(hit.point);
    bullet->Velocity=BulletVelocity::Empty;
    bullet->Speed=0;
}

// Only live during the synchronous native explosion. Nested explosions of
// other bullets keep their ordinary target snapping and warhead behavior.
BulletClass* surfaceImpact=nullptr;
}

bool piercingStep(BulletClass* bullet,const CoordStruct& next,const ScriptImpact& hit) {
    TargetClass ref;
    ref.m_ID=hit.id;
    ref.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
    Piercing::sweep(bullet,next,hit.fraction,hit.id?ref.As_Techno():nullptr);
    if(!hit.id) return false;
    DWORD directTarget=0;
    // A direct hit can destroy other objects through death effects.
    if(auto live=ref.As_Techno()) {
        directTarget=live->UniqueID;
        ProjectileShield::Hit impact{live,hit.point,hit.fraction,hit.multiplier*Piercing::interceptionFactor(bullet,directTarget)};
        ProjectileShield::intercept(bullet,impact,true,!Piercing::endWarhead(bullet));
    }
    Piercing::detonateEnd(bullet,hit.point,hit.multiplier,directTarget);
    return true;
}

ScriptImpact scriptedImpact(BulletClass* bullet,const CoordStruct& next) {
    auto wall=firstImpact(bullet,bullet->Location,next,true,true);
    auto shield=ProjectileShield::first(bullet,bullet->Location,next);
    if(shield.guard && (!wall.building || shield.fraction<wall.fraction))
        return {shield.guard->UniqueID,shield.point,shield.multiplier,true,shield.fraction};
    if(wall.building) return {wall.building->UniqueID,wall.point,1,false,wall.fraction};
    return {};
}
bool applyScriptedImpact(BulletClass* bullet,const ScriptImpact& hit) {
    TargetClass ref;ref.m_ID=hit.id;ref.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
    auto guard=ref.As_Techno();
    if(!guard) return false;
    if(hit.shield) {
        ProjectileShield::intercept(bullet,{guard,hit.point,0,hit.multiplier});
        return true;
    }
    applyHit(bullet,{static_cast<BuildingClass*>(guard),hit.point,true});
    auto previous=surfaceImpact;
    surfaceImpact=bullet;
    bullet->Explode(true);
    surfaceImpact=previous;
    return true;
}

bool blocksAttack(TechnoClass* shooter,AbstractClass* target,WeaponTypeClass* weapon) {
    if(!shooter || !target || !weapon || !weapon->Projectile || !hasBarriers()) return false;
    const auto p=weapon->Projectile;
    if(!p->SubjectToWalls && !p->SubjectToCliffs) return false;
    auto carrier=shooter->Transporter?shooter->Transporter:shooter;
    auto from=carrier->GetCoords(),to=target->GetCoords();
    const WallShot shot{carrier,target,CellClass::Coord2Cell(from),CellClass::Coord2Cell(to),p->SubjectToCliffs,
        p->SubjectToWalls && !(weapon->Warhead && weapon->Warhead->Wall)};
    return trace(from,to,[&](BuildingClass* b,CellClass* c,Fraction,Fraction)->Impact {
        return {wallBlocks(b,c,shot)?b:nullptr};
    }).building!=nullptr;
}

namespace {
DWORD checkStep(BulletClass* bullet,const CoordStruct& next,bool destroy,bool includeLegacy) {
    auto hit=firstImpact(bullet,bullet->Location,next,includeLegacy);
    if(!bullet->Type->Inviso) {
        const auto shield=ProjectileShield::first(bullet,bullet->Location,next);
        if(shield.guard && (!hit.building || shield.fraction<hit.fraction)) {
            ProjectileShield::intercept(bullet,shield);
            return 0x467FAF;
        }
    }
    if(!hit.building) return 0;
    applyHit(bullet,hit);
    if(!hit.physical) return 0x467F9B;
    const auto previous=surfaceImpact;
    surfaceImpact=bullet;
    bullet->Explode(destroy);
    surfaceImpact=previous;
    return 0x467FAF; // native removal following the explosion
}
}

DEFINE_HOOK(0x467494,ActSp_EntityWallProjectileStep,9) {
    return checkStep(R->EBP<BulletClass*>(),R->Stack<CoordStruct>(0x44),R->Stack<bool>(0x18),true);
}

// Homing missiles can bypass the terrain sweep above. Their committed next
// position is available here, including on the first tick after loading.
DEFINE_HOOK(0x467B7A,ActSp_BlockerMovementCommit,7) {
    if(R->Stack<int>(0x20)==2 || (!hasProjectileBlockers() && ProjectileShield::active.empty())) return 0;
    auto b=R->EBP<BulletClass*>();
    const auto next=R->Stack<CoordStruct>(0x24);
    if(next==b->Location) return 0;
    return checkStep(b,next,R->Stack<bool>(0x18),false);
}

// The native explosion already copied the bullet location. Keep that physical
// impact rather than snapping the blast to the new target's center.
DEFINE_HOOK(0x468DC1,ActSp_BlockerImpactPosition,6) {
    return surfaceImpact && R->ESI<BulletClass*>()==surfaceImpact?0x468FF4:0;
}

// DamageArea's center cell already contains the struck building. Use its
// native direct-hit distance even on a tall roof. All other cells, victims,
// nested explosions and scattered cluster impacts keep their native distance.
DEFINE_HOOK(0x4897A4,ActSp_BlockerSurfaceDistance,5) {
    if(surfaceImpact && R->ESI<ObjectClass*>()==surfaceImpact->Target &&
       R->Base<DWORD>(4)==0x469A88 && R->Stack<BulletClass*>(4)==surfaceImpact &&
       *R->EDI<CoordStruct*>()==surfaceImpact->TargetCoords) return 0x48983B;
    return 0;
}

DEFINE_HOOK(0x468A3F,ActSp_EntityWallInstantHit,6) {
    auto bullet=R->EBX<BulletClass*>();
    if(bullet->Type->Inviso && !ProjectileShield::configured) {
        auto hit=firstImpact(bullet,bullet->SourceCoords,bullet->Location,true);
        if(hit.building) applyHit(bullet,hit);
    }
    return 0;
}

// An Inviso round can already be at its destination before the first movement
// step. Resolve the muzzle-to-impact segment before native target snapping.
DEFINE_HOOK(0x468D8A,ActSp_ShieldInstantImpact,6) {
    auto bullet=R->ESI<BulletClass*>();
    if(bullet->Type->Inviso && ProjectileShield::configured) {
        const auto wall=firstImpact(bullet,bullet->SourceCoords,bullet->Location,true);
        const auto shield=ProjectileShield::first(bullet,bullet->SourceCoords,bullet->Location);
        if(wall.building && (!shield.guard || wall.fraction<=shield.fraction)) applyHit(bullet,wall);
        else if(shield.guard) {
            ProjectileShield::intercept(bullet,shield);
            return 0x4690A6;
        }
    }
    return 0;
}
}
