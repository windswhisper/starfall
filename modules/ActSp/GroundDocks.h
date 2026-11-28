#pragma once
#include "GroundGuards.h"

namespace GroundDocks {
int berthCount(BuildingClass* post);
FootClass* spawn(BuildingClass* post,TechnoTypeClass* type);
bool update(BuildingClass* post,FootClass* unit,GroundGuards::Slot& slot,CellClass* station,DWORD frame);
}
