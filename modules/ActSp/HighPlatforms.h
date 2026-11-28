#pragma once
#include <GeneralStructures.h>

struct IStorage;
class AbstractClass;
class BulletTypeClass;
class CellClass;
class TechnoClass;

namespace HighPlatforms {
bool landingRim(CellStruct at);
DWORD landingDeck(CellStruct at);
void tick();
void reset();
void forget(DWORD id);
void forgetEntry(DWORD id);
CellClass* arcingWallCell(const AbstractClass* target,const BulletTypeClass* projectile,const CoordStruct& source);
int deckDepthOffset(const TechnoClass* object);
bool drawsPlatformBody(TechnoClass* object);
bool save(IStorage* storage);
bool load(IStorage* storage);
}
