#pragma once
#include <GeneralStructures.h>
#include <vector>

class BulletClass;
class TechnoClass;
class HouseClass;
class WarheadTypeClass;
class WeaponTypeClass;

namespace ProjectileShield {
struct Rules {
    double radius=0; // map cells, in all three dimensions
    double heightOffset=0; // map cells above the holder's world position
    double damageMultiplier=1;
    bool blockInviso=false;
};
struct Provider { TechnoClass* unit; Rules rules; };
inline std::vector<Provider> active;
inline bool configured=false;
void forget(TechnoClass* unit);
struct Hit {
    TechnoClass* guard=nullptr;
    CoordStruct point{};
    double fraction=1;
    double multiplier=1;
};
Hit first(BulletClass* bullet,const CoordStruct& from,const CoordStruct& to);
CoordStruct beamEnd(TechnoClass* firer,WeaponTypeClass* weapon,const CoordStruct& from,const CoordStruct& to,HouseClass* owner=nullptr);
void intercept(BulletClass* bullet,const Hit& hit,bool directOnly=false,bool contactAnimation=true);
void endBlast(BulletClass* bullet,const CoordStruct& point,WarheadTypeClass* wh,double factor,DWORD directTarget);
}
