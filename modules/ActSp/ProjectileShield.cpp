#include "../../src/ProjectileShield.h"
#include "../../src/VisualScale.h"
#include "EntityWalls.h"
#include "FireOrigin.h"
#include <AnimClass.h>
#include <BulletClass.h>
#include <HouseClass.h>
#include <LaserDrawClass.h>
#include <RadBeam.h>
#include <MapClass.h>
#include <WeaponTypeClass.h>
#include <WarheadTypeClass.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ProjectileShield {
void forget(TechnoClass* unit) {
    std::erase_if(active,[unit](const Provider& provider){return provider.unit==unit;});
}

namespace {
bool bypassesShield(WeaponTypeClass* weapon) {
    return weapon && (weapon->DiskLaser || weapon->IsRailgun || weapon->IsSonic || weapon->IsMagBeam ||
        (weapon->IsRadBeam && weapon->Warhead && weapon->Warhead->Temporal));
}

Hit firstOnSegment(TechnoClass* firer,HouseClass* house,bool instant,const CoordStruct& from,const CoordStruct& to) {
    Hit best;
    if(!house || from==to) return best;
    const double dx=double(to.X)-from.X,dy=double(to.Y)-from.Y,dz=double(to.Z)-from.Z;
    const double length2=dx*dx+dy*dy+dz*dz;
    for(const auto& [unit,rules]:active) {
        if((instant && !rules.blockInviso) || unit==firer ||
           !unit->IsAlive || unit->Health<=0 || unit->InLimbo || !unit->IsOnMap ||
           !unit->Owner || unit->Owner->IsAlliedWith(house)) continue;
        const auto center=unit->GetCoords();
        const double x=double(from.X)-center.X,y=double(from.Y)-center.Y;
        const double z=double(from.Z)-center.Z-rules.heightOffset*Unsorted::LeptonsPerCell;
        const double radius=rules.radius*Unsorted::LeptonsPerCell;
        const double outside=x*x+y*y+z*z-radius*radius;
        const double approach=-(x*dx+y*dy+z*dz);
        if(outside<0 || approach<=0) continue;
        const double discriminant=approach*approach-length2*outside;
        if(discriminant<0) continue;
        // Equivalent to the smaller quadratic root, stable near the sphere surface.
        const double t=outside/(approach+std::sqrt(discriminant));
        if(t>best.fraction) continue;
        if(t==best.fraction && best.guard &&
           (unit->UniqueID>best.guard->UniqueID ||
            (unit==best.guard && rules.damageMultiplier>=best.multiplier))) continue;
        best={unit,{int(std::lround(from.X+t*dx)),int(std::lround(from.Y+t*dy)),
                    int(std::lround(from.Z+t*dz))},t,rules.damageMultiplier};
    }
    return best;
}
}

Hit first(BulletClass* bullet,const CoordStruct& from,const CoordStruct& to) {
    if(active.empty() || bypassesShield(bullet->WeaponType)) return {};
    return firstOnSegment(bullet->Owner,EntityWalls::firingHouse(bullet),bullet->Type->Inviso,from,to);
}

CoordStruct beamEnd(TechnoClass* firer,WeaponTypeClass* weapon,const CoordStruct& from,const CoordStruct& to,HouseClass* owner) {
    if(active.empty() || !weapon || !weapon->Projectile || !weapon->Projectile->Inviso || bypassesShield(weapon))
        return to;
    const auto hit=firstOnSegment(firer,firer?firer->Owner:owner,true,from,to);
    return hit.guard?hit.point:to;
}

// FireAt still holds the actual weapon and firer here. Clip the returned visual
// after engine/extension setup without changing the firing order or its target.
DEFINE_HOOK_AGAIN(0x6FF504,SF_ShieldLaserEnd,8)
DEFINE_HOOK(0x6FF55B,SF_ShieldLaserEnd,8) {
    auto laser=R->EAX<LaserDrawClass*>();
    if(!laser) return 0x6FF656;
    if(FireOrigin::enabled(R->EBX<WeaponTypeClass*>())) laser->Source=R->Stack<CoordStruct>(0x44);
    laser->Target=beamEnd(R->ESI<TechnoClass*>(),R->EBX<WeaponTypeClass*>(),laser->Source,laser->Target);
    return R->Origin()+8;
}

// CreateEBolt has moved its muzzle argument into EBX. Carry the firer in that
// argument slot until the target coordinates are resolved, then restore it.
DEFINE_HOOK(0x6FD49F,SF_ShieldBoltFirer,5) {
    R->Stack(0x3C,R->EDI<TechnoClass*>());
    return R->EAX<int>()==int(AbstractType::Building)?0x6FD4A4:0x6FD4ED;
}

DEFINE_HOOK(0x6FD51E,SF_ShieldBoltEnd,6) {
    auto muzzle=R->EBX<CoordStruct*>();
    auto firer=R->Stack<TechnoClass*>(0x3C);
    R->Stack(0x3C,muzzle);
    if(!active.empty()) {
        const auto end=beamEnd(firer,R->Stack<WeaponTypeClass*>(0x38),*muzzle,
            {R->EAX<int>(),R->EDI<int>(),R->EBP<int>()});
        R->EAX(end.X);
        R->EDI(end.Y);
        R->EBP(end.Z);
    }
    return 0;
}

DEFINE_HOOK(0x6FF5CB,SF_ShieldRadEnd,5) {
    if(auto beam=R->EAX<RadBeam*>()) {
        if(FireOrigin::enabled(R->EBX<WeaponTypeClass*>())) beam->SetCoordsSource(R->Stack<CoordStruct>(0x44));
        beam->SetCoordsTarget(beamEnd(R->ESI<TechnoClass*>(),R->EBX<WeaponTypeClass*>(),beam->SourceLocation,beam->TargetLocation));
    }
    return 0x6FF656;
}

namespace {
struct Impact { BulletClass* round; DWORD guard; };
Impact current{};

void playAbsorbedImpact(BulletClass* bullet,CellClass* cell,const CoordStruct& point) {
    if(!bullet->WH || !cell) return;
    // Native impacts select artwork using the incoming shot's damage. An
    // airborne contact above water uses the ordinary list, not a water splash.
    const auto land=bullet->GetHeight()<Unsorted::CellHeight?cell->LandType:LandType::None;
    auto type=MapClass::SelectDamageAnimation(bullet->Health,bullet->WH,land,point);
    if(!type) return;
    if(auto anim=GameCreate<AnimClass>(type,point,0,1,0x2600)) {
        anim->IsInert=true;
        anim->Owner=EntityWalls::firingHouse(bullet);
        if(auto scale=VisualScale::projectiles.find(bullet->UniqueID);scale!=VisualScale::projectiles.end())
            VisualScale::explosions.emplace(anim->UniqueID,scale->second);
    }
}
}

void endBlast(BulletClass* bullet,const CoordStruct& point,WarheadTypeClass* wh,double factor,DWORD directTarget) {
    auto cell=MapClass::Instance.TryGetCellAt(CellClass::Coord2Cell(point));
    if(!cell) return;
    bullet->SetTarget(cell);
    bullet->TargetCoords=point;
    bullet->SetLocation(point);
    bullet->WH=wh;
    const int64_t base=(int64_t(bullet->Health)*bullet->DamageMultiplier)>>8;
    const int damage=int(std::round(std::clamp(base*factor,double(INT_MIN),double(INT_MAX))));
    if(!damage) {
        playAbsorbedImpact(bullet,cell,point);
        return;
    }
    const auto previous=current;
    current={bullet,directTarget};
    bullet->Health=damage;
    bullet->DamageMultiplier=256;
    bullet->Detonate(point);
    current=previous;
}

void intercept(BulletClass* bullet,const Hit& hit,bool directOnly,bool contactAnimation) {
    auto cell=MapClass::Instance.TryGetCellAt(CellClass::Coord2Cell(hit.point));
    bullet->SetTarget(cell);
    bullet->TargetCoords=hit.point;
    bullet->SetLocation(hit.point);
    bullet->Velocity=BulletVelocity::Empty;
    bullet->Speed=0;
    if(hit.multiplier==0) {
        if(contactAnimation) playAbsorbedImpact(bullet,cell,hit.point);
        return;
    }

    // Fold the native 8.8 fixed-point bullet factor into the intercepted damage once.
    const int64_t base=(int64_t(bullet->Health)*bullet->DamageMultiplier)>>8;
    int damage=int(std::round(std::clamp(base*hit.multiplier,double(INT_MIN),double(INT_MAX))));
    const auto previous=current;
    current={bullet,hit.guard->UniqueID};
    if(damage && bullet->WH) {
        int direct=damage;
        hit.guard->ReceiveDamage(&direct,0,bullet->WH,bullet->Owner,false,false,EntityWalls::firingHouse(bullet));
    }
    if(directOnly) {
        if(contactAnimation) playAbsorbedImpact(bullet,cell,hit.point);
        current=previous;
        return;
    }
    bullet->Health=damage;
    bullet->DamageMultiplier=256;
    bullet->Detonate(hit.point);
    current=previous;
}

// The native pre-area product can overflow before its fixed-point shift.
// Shield shots already folded this factor into Health above.
DEFINE_HOOK(0x469A62,SF_ShieldSplashDamage,7) {
    if(!current.round || R->ESI<BulletClass*>()!=current.round) return 0;
    R->EDX(current.round->Health);
    return 0x469A69;
}

// The explicit direct hit already passed through the guard's armor. The rest
// of this bullet's native blast can damage other objects near the shield edge.
DEFINE_HOOK(0x489A97,SF_ShieldSkipDuplicateSplash,7) {
    if(current.round && R->Base<DWORD>(4)==0x469A88 && R->Stack<BulletClass*>(4)==current.round &&
       R->ESI<ObjectClass*>()->UniqueID==current.guard) return 0x489AC1;
    return 0;
}
}
