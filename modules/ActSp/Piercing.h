#pragma once
#include <GeneralStructures.h>
struct IStorage;
class BulletClass;
class BulletTypeClass;
class HouseClass;
class TechnoClass;
class WarheadTypeClass;

namespace Piercing {
void reset();
void forget(DWORD bullet);
bool save(IStorage* storage);
bool load(IStorage* storage);
bool allowed(WarheadTypeClass* wh,HouseClass* source,TechnoClass* target);
void sweep(BulletClass* bullet,const CoordStruct& next,double stop,TechnoClass* blocker);
double interceptionFactor(BulletClass* bullet,DWORD target);
bool enabled(BulletTypeClass* type);
void startScripted(BulletClass* bullet,int scriptLife);
bool active(DWORD id);
WarheadTypeClass* endWarhead(BulletClass* bullet);
void detonateEnd(BulletClass* bullet,const CoordStruct& point,double interception,DWORD directTarget);
}
