#pragma once
#include "Research.h"

class BuildingClass;
class BuildingTypeClass;
class InfantryClass;

namespace BuildingResearch {
struct RewardRule {
    std::vector<std::string> options;
    bool engineer=false,spy=false;
};
const RewardRule& rewardRule(BuildingTypeClass* type);
void authorizeBuildingReward(InfantryClass* infantry,BuildingClass* building,Research::BuildingRewardSource source);
void tickBuildings();
void trackBuilding(BuildingClass* building);
void ownerChanged(BuildingClass* building);
void forget(unsigned long id);
void clearTypeCaches();
}
