#pragma once
#include <GeneralDefinitions.h>

class Surface;
class BulletClass;
class WeaponTypeClass;

namespace ArtSp::Tracers {
struct Style {
    ColorStruct color{255,205,96}, tailColor{210,92,28};
    int length=96, width=2, speed=192;
    bool gradient=false;
};

void readRules();
void clearRules();
void forget(unsigned firer);
// Sample cadence once for the entire shot, including any additional targets.
const Style* select(unsigned firer, WeaponTypeClass* weapon);
const Style* onShot(unsigned firer,BulletClass* bullet,const CoordStruct& muzzle,unsigned frame);
void launch(const Style& style, const CoordStruct& from, const CoordStruct& to, unsigned frame);
void expire(unsigned frame);
void clearTrails();
void draw(Surface* surface, RectangleStruct viewport, unsigned frame);
}
