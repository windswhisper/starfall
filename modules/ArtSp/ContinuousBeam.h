#pragma once
#include <GeneralDefinitions.h>
#include <vector>

class AbstractClass;
class TechnoClass;
class WeaponTypeClass;
class Surface;

namespace ArtSp::ContinuousBeam {
void readRules();
void reset();
void stop(TechnoClass* firer);
void forget(TechnoClass* unit);
bool collectExtra(unsigned shot,TechnoClass* firer,AbstractClass* primary,WeaponTypeClass* weapon,TechnoClass* target);
std::vector<TechnoClass*> takeExtras(unsigned shot);
bool onShot(TechnoClass* firer,AbstractClass* aim,WeaponTypeClass* weapon,int slot,const CoordStruct& muzzle,
            const std::vector<TechnoClass*>& extras);
void tick();
void draw(Surface* surface,RectangleStruct view,unsigned frame);
}
