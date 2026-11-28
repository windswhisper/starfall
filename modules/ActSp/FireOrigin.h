#pragma once
#include <GeneralStructures.h>
class AbstractClass;
class WeaponTypeClass;

namespace FireOrigin {
void readRules();
void reset();
bool supports(WeaponTypeClass* weapon);
bool enabled(WeaponTypeClass* weapon);
CoordStruct resolve(WeaponTypeClass* weapon,AbstractClass* target,const CoordStruct& muzzle);
}
