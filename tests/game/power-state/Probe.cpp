#include "../../../src/Starfall.cpp"

namespace {
void check(bool ok,const char* label) {
    std::ofstream("power-state.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
BuildingClass* place(const char* id,HouseClass* house,CellStruct site) {
    auto type=BuildingTypeClass::Find(id);
    auto b=type?static_cast<BuildingClass*>(type->CreateObject(house)):nullptr;
    bool placed=false;
    for(short y=site.Y;b && !placed && y<site.Y+12;++y) {
        for(short x=site.X;!placed && x<site.X+12;++x) {
            CellStruct position{x,y};
            auto cell=MapClass::Instance.TryGetCellAt(position);
            if(cell && MapClass::Instance.IsWithinUsableArea(position,false))
                placed=b->Unlimbo(cell->GetCoordsWithBridge(),DirType::North);
        }
    }
    check(placed,"placed building");
    b->QueueMission(Mission::Guard,false);
    return b;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestPowerState,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    int frame=Unsorted::CurrentFrame;
    if((!load && frame!=20) || (load && frame!=35)) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    if(!load) {
        check(skills.contains("plantboost") && skills.contains("efficiency") &&
            !skills.contains("invalidoutput") && !skills.contains("invaliddrain"),"configuration");
        auto plant=place("GAPOWR",house,{50,65});
        place("GAPOWR",house,{56,65});
        auto barracks=place("GAPILE",house,{62,65});
        house->UpdatePower();
        const int baseOut=house->PowerOutput,baseDrain=house->PowerDrain;
        const int gen=plant->GetPowerOutput(),draw=barracks->GetPowerDrain();
        check(gen>0 && draw>0 && baseOut>=gen*2 && baseDrain>=draw,"native grid");
        grantToUnit(plant,PassiveGrant{{"efficiency"},-1});
        grantToUnit(barracks,PassiveGrant{{"efficiency"},-1});
        passiveStacks[plant->UniqueID]["efficiency"]=1;
        passiveStacks[barracks->UniqueID]["efficiency"]=1;
        tickStates();house->UpdatePower();
        check(house->PowerOutput==baseOut-gen+(gen+1)/2 && house->PowerDrain==baseDrain-draw+(draw+1)/2,
            "same passive halves output and drain");
        passiveStacks[plant->UniqueID].erase("efficiency");
        grantToUnit(plant,PassiveGrant{{"plantboost","plantdouble"},-1});
        grantToUnit(barracks,PassiveGrant{{"nodrain"},-1});
        passiveStacks[plant->UniqueID]["plantboost"]=1;
        tickStates();
        check(house->RecheckPower,"grid marked dirty");
        house->UpdatePower();
        const int half=(draw+1)/2;
        check(house->PowerOutput==baseOut+2*gen && house->PowerDrain==baseDrain-draw+half,"stacked output and formula drain");
        tickStates();
        check(!house->RecheckPower,"steady grid is not dirtied");
        passiveStacks[plant->UniqueID].erase("plantboost");
        passiveStacks[barracks->UniqueID]["nodrain"]=1;
        tickStates();house->UpdatePower();
        check(house->PowerOutput==baseOut+gen && house->PowerDrain==baseDrain-draw,"zero and withdrawal");
        passiveStacks[barracks->UniqueID].erase("nodrain");
        passiveStacks[plant->UniqueID]["plantboost"]=1;
        tickStates();house->UpdatePower();
        std::ofstream("power-state.txt")<<plant->UniqueID<<" "<<baseOut<<" "<<baseDrain<<" "<<gen<<" "<<draw;
        check(ScenarioClass::SaveGame("Saved Games\\PowerState.sav",L"Power State"),"saved");
    } else {
        DWORD id=0;int baseOut=0,baseDrain=0,gen=0,draw=0;
        std::ifstream("power-state.txt")>>id>>baseOut>>baseDrain>>gen>>draw;
        check(house->PowerOutput==baseOut+2*gen && house->PowerDrain==baseDrain-draw+(draw+1)/2,"restored grid");
        BuildingClass* plant=nullptr;
        for(auto b:BuildingClass::Array) if(b && b->UniqueID==id) plant=b;
        foe->UpdatePower();int before=foe->PowerOutput;
        check(plant && plant->SetOwningHouse(foe,false),"transfer building");
        tickStates();house->UpdatePower();foe->UpdatePower();
        check(house->PowerOutput==baseOut-gen && foe->PowerOutput==before+3*gen,"owner transfer");
        warheadGrants.erase(id);tickStates();foe->UpdatePower();
        check(foe->PowerOutput==before+gen,"removed source");
    }
    TerminateProcess(GetCurrentProcess(),0);
    return 0;
}
