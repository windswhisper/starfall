#include "FireOrigin.h"
#include "../../src/ConfigText.h"
#include <CCINIClass.h>
#include <FootClass.h>
#include <CellClass.h>
#include <MapClass.h>
#include <WeaponTypeClass.h>
#include <WarheadTypeClass.h>
#include <unordered_map>
#include <cstdint>
#include <climits>

namespace FireOrigin {
namespace {
std::unordered_map<WeaponTypeClass*,CoordStruct> offsets;
}
void reset() { offsets.clear(); }

bool supports(WeaponTypeClass* weapon) {
    return weapon && weapon->Projectile && !weapon->Spawner && !weapon->LimboLaunch && !weapon->IsRailgun &&
        !weapon->DiskLaser && !weapon->IsSonic && !weapon->IsMagBeam && !weapon->IsRadEruption &&
        !(weapon->Warhead && weapon->Warhead->Temporal);
}

void readRules() {
    reset();
    auto ini=CCINIClass::INI_Rules;
    if(!ini) return;
    for(auto weapon:WeaponTypeClass::Array) {
        char mode[32]{};
        ini->ReadString(weapon->ID,"FireOrigin","Firer",mode);
        if(!_stricmp(mode,"Firer")) continue;
        if(_stricmp(mode,"Target")) {
            ConfigText::configLog(std::string("[")+weapon->ID+"] expected FireOrigin=Firer or Target");
            continue;
        }
        if(!supports(weapon)) continue;
        CoordStruct offset{};
        ini->GetPoint3D(weapon->ID,"FireOrigin.Offset",offset);
        offsets.emplace(weapon,offset);
    }
}
bool enabled(WeaponTypeClass* weapon) { return offsets.contains(weapon); }

CoordStruct resolve(WeaponTypeClass* weapon,AbstractClass* target,const CoordStruct& muzzle) {
    auto it=offsets.find(weapon);
    if(it==offsets.end() || !target) return muzzle;
    CoordStruct anchor;
    if(target->WhatAmI()==AbstractType::Cell) anchor=static_cast<CellClass*>(target)->GetCoordsWithBridge();
    else if((target->AbstractFlags&AbstractFlags::Object)!=AbstractFlags::None)
        anchor=static_cast<ObjectClass*>(target)->GetTargetCoords();
    else anchor=target->GetCenterCoords();
    const auto& offset=it->second;
    const int64_t x=int64_t(anchor.X)+offset.X,y=int64_t(anchor.Y)+offset.Y,z=int64_t(anchor.Z)+offset.Z;
    // The map cell table has 512 columns and rows. Check before narrowing to native coordinates.
    constexpr int mapExtent=512*256;
    if(x<0 || y<0 || x>=mapExtent || y>=mapExtent || z<INT_MIN || z>INT_MAX) return muzzle;
    const CoordStruct origin{int(x),int(y),int(z)};
    return MapClass::Instance.CoordinatesLegal(origin)?origin:muzzle;
}

// FireAt has selected the weapon and computed its normal muzzle. Replace only
// this shot's local origin, before native scatter, ballistics and MoveTo.
DEFINE_HOOK(0x6FE302,ActSp_TargetFireOrigin,10) {
    auto& muzzle=R->ref_Stack<CoordStruct>(0x44);
    muzzle=resolve(R->Stack<WeaponTypeClass*>(0x40),R->Base<AbstractClass*>(8),muzzle);
    return 0;
}
// Vertical projectiles read their firer's coordinates a second time.
DEFINE_HOOK(0x6FE976,ActSp_VerticalFireOrigin,6) {
    auto& muzzle=*R->EAX<CoordStruct*>();
    muzzle=resolve(R->Stack<WeaponTypeClass*>(0x40),R->Base<AbstractClass*>(8),muzzle);
    return 0;
}
}
