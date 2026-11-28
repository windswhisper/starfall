#pragma once
#include <GeneralStructures.h>
#include <string>

class AbstractClass;
class BulletClass;
class BuildingClass;
class BuildingTypeClass;
class HouseClass;
class TechnoClass;
class WeaponTypeClass;

namespace EntityWalls {
struct WallRules {
    BuildingTypeClass* segment;
    std::string group;
    int maxGap;
    bool blocksProjectiles;
    bool highWall;
    bool connectionFrames;
};
void reset();
const WallRules* rules(BuildingTypeClass* type);
bool hasBarriers();
struct BlockerRules {
    int height; // terrain levels above the building's base
    bool blockAllied;
};
const BlockerRules* blockerRules(BuildingTypeClass* type);
bool hasProjectileBlockers();
HouseClass* firingHouse(BulletClass* bullet);
bool blocksAttack(TechnoClass* shooter,AbstractClass* target,WeaponTypeClass* weapon);
struct ScriptImpact {DWORD id=0; CoordStruct point{}; double multiplier=1; bool shield=false; double fraction=2;};
bool piercingStep(BulletClass* bullet,const CoordStruct& next,const ScriptImpact& hit);
ScriptImpact scriptedImpact(BulletClass* bullet,const CoordStruct& next);
bool applyScriptedImpact(BulletClass* bullet,const ScriptImpact& hit);
void drawPreview();
void built(BuildingClass* endpoint);
void redrawNeighbors(BuildingClass* wall);
}
