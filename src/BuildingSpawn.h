#pragma once
#include <GeneralStructures.h>

class BuildingTypeClass;
class HouseClass;

namespace BuildingSpawn {
bool supported(const BuildingTypeClass* type);
bool available(BuildingTypeClass* type,HouseClass* owner,CellStruct anchor);
bool createAt(BuildingTypeClass* type,HouseClass* owner,CellStruct anchor);
bool create(BuildingTypeClass* type,HouseClass* owner,const CoordStruct& location,int radius);
}
