#include "../../../src/Starfall.cpp"
#include <EventClass.h>

namespace {
const std::array<const char*,3> types={"E1","GGI","DESO"};
const std::array<CellStruct,3> cells={CellStruct{48,70},CellStruct{62,84},CellStruct{80,80}};
void check(bool ok,const char* message) {
    std::ofstream("deployment.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<message<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void deployCommand(InfantryClass* unit) {
    EventClass ev(unit->Owner->ArrayIndex,EventType::Deploy);
    ev.Deploy.Whom=TargetClass(unit);
    check(EventClass::OutList.Add(ev),"deployment command queued");
}
}

DEFINE_HOOK(0x55B4EB,SF_TestDeployedState,5) {
    static const bool enabled=GetFileAttributesA("Starfall.test")!=INVALID_FILE_ATTRIBUTES;
    if(!enabled) return 0;
    const int frame=Unsorted::CurrentFrame;
    if(frame>215) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    static const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(frame==20 && !loading) {
        auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
        PassiveGrant grant{{"deployedrate","mobilerange","entrenchedshell","deploycounter"},-1};
        for(size_t i=0;i<types.size();++i) {
            auto type=InfantryTypeClass::Find(types[i]);
            auto cell=MapClass::Instance.TryGetCellAt(cells[i]);
            check(type && cell && house,"valid test unit and cell");
            auto unit=static_cast<InfantryClass*>(type->CreateObject(house));
            check(unit && unit->Unlimbo(cell->GetCoordsWithBridge(),DirType::North),"spawned infantry");
            grantToUnit(unit,grant);
        }
        for(auto u:UnitClass::Array) if(!_stricmp(u->Type->ID,"MTNK")) {grantToUnit(u,grant);break;}
    }
    if(frame<22) return 0;
    static std::map<DWORD,int> firstDeployed;
    bool allDeployed=true,allMobile=true,allBuffed=true,noBonuses=true;
    int seen=0;
    for(auto u:InfantryClass::Array) if(warheadGrants.contains(u->UniqueID)) {
        ++seen;
        auto id=u->UniqueID;
        const bool deployed=u->IsDeployed();
        const bool fast=fireRateState.contains(id);
        const bool mobile=rangeState.contains(id);
        const bool delayed=projectileState.contains(id);
        if(deployed && !firstDeployed.contains(id)) firstDeployed[id]=frame;
        if(!deployed) firstDeployed.erase(id);
        if(!loading && frame>25 && frame<90) {
            if(fast!=deployed || mobile==deployed) check(false,"state follows native posture");
            if(delayed!=(deployed && frame-firstDeployed[id]>=15)) check(false,"delay follows state entry");
        }
        allDeployed&=deployed && fast && !mobile;
        allMobile&=!deployed && !fast && mobile && !delayed;
        allBuffed&=delayed;
        noBonuses&=!fast && !delayed;
        if(frame==24 || frame==115 || frame==165) deployCommand(u);
        if(frame==24 || frame==85 || frame==150 || frame==208) {
            std::ofstream("deployment.log",std::ios::app)<<frame<<" "<<u->Type->ID<<" native "<<deployed<<" seq "<<int(u->SequenceAnim)<<" rate "<<fast<<" mobile "<<mobile<<" delayed "<<delayed<<" deploy events "<<counts[id]["deploycounter"].count<<"\n";
        }
    }
    if(frame==24 || frame==85 || frame==110 || frame==150 || frame==208) check(seen==3,"all three infantry present");
    if(frame==24) {
        check(allMobile && noBonuses,"initial posture");
        for(auto u:UnitClass::Array) if(warheadGrants.contains(u->UniqueID))
            check(!rangeState.contains(u->UniqueID) && !fireRateState.contains(u->UniqueID),"non-deployer excluded");
    }
    if(frame==85 && !loading) {check(allDeployed,"deployed effects");check(allBuffed,"delay");}
    if(frame==90 && !loading) check(ScenarioClass::SaveGame("Saved Games\\DeployedState.sav",L"DeployedState"),"saved");
    if(frame==110) check(allDeployed && allBuffed,"deployed after save");
    if(frame==150) check(allMobile,"packed up");
    if(frame==208) check(allDeployed && allBuffed,"redeployed");
    return 0;
}
