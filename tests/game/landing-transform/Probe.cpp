#include "../../../src/Starfall.cpp"
namespace {
void verify(bool ok,const char* msg) {
    std::ofstream("landing.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<msg<<'\n';
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
}
DEFINE_HOOK(0x55B4EB,SF_TestLanding,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const auto frame=Unsorted::CurrentFrame;
    const bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    static DWORD id=0;static int before=0;static bool started=false,saved=false,blockTest=false;
    if(frame<30) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    if(load && !id) {std::ifstream("landing.id")>>id>>before;started=saved=true;}
    if(!load && frame==30) {
        auto z=static_cast<UnitClass*>(UnitTypeClass::Find("ZEP")->CreateObject(house));
        verify(z && z->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{56,80})->GetCoords(),DirType::North),"placed");
        id=z->UniqueID;
        z->SetDestination(MapClass::Instance.GetCellAt(CellStruct{62,80}),true);z->ForceMission(Mission::Move);
        z->SetDestination(MapClass::Instance.GetCellAt(CellStruct{62,80}),true);z->ForceMission(Mission::Move);
    }
    UnitClass* unit=nullptr;
    for(auto u:UnitClass::Array) if(u->UniqueID==id) unit=u;
    if(!unit) verify(false,"unit exists");
    if(!started && unit->GetHeight()>160) {
        started=true;before=house->Available_Money();
        UnitClass* blocker=nullptr;
        if(blockTest) {
            for(auto candidate:UnitClass::Array)
                if(candidate->Owner==house && candidate->Type==UnitTypeClass::Find("MTNK") && candidate->IsOnMap) {blocker=candidate;break;}
            auto under=unit->GetCell()->GetCoords();
            verify(blocker && ForceMove::moveTo(blocker,under.X,under.Y,true),"ground below occupied");
        }
        grantToUnit(unit,PassiveGrant{{"testland"},-1});capture("onpromote",unit,nullptr,unit,unit,0);dispatch();
        verify(!pending.empty() && house->Available_Money()==before && unit->Type==UnitTypeClass::Find("ZEP"),"queued without reward");
        if(blockTest) {
            const auto site=pending.back().landing->cell;
            verify(site!=unit->GetMapCoords(),"nearby landing selected");
            auto at=MapClass::Instance.GetCellAt(site)->GetCoords();
            verify(ForceMove::moveTo(blocker,at.X,at.Y,true),"landing site occupied");
        }
    }
    auto loco=locomotion_cast<JumpjetLocomotionClass*>(unit->Locomotor);
    if(started && !saved && loco && loco->State==JumpjetLocomotionClass::Descending && unit->GetHeight()>0) {
        std::ofstream("landing.id")<<id<<" "<<before;
        verify(ScenarioClass::SaveGame("Saved Games\\Landing.sav",L"Landing"),"saved");
        saved=true;
    }
    if(started && unit->Type==UnitTypeClass::Find("MTNK")) {
        verify(!blockTest && unit->GetHeight()==0 && house->Available_Money()==before+77 && pending.empty(),"converted then rewarded");
        auto second=static_cast<UnitClass*>(UnitTypeClass::Find("ZEP")->CreateObject(house));
        verify(second && second->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{52,78})->GetCoords(),DirType::North),"second balloon");
        second->SetDestination(MapClass::Instance.GetCellAt(CellStruct{59,78}),true);second->ForceMission(Mission::Move);
        id=second->UniqueID;started=false;blockTest=true;
    }
    if(blockTest && started && pending.empty()) {
        verify(unit->Type==UnitTypeClass::Find("ZEP") && house->Available_Money()==before,"blocked landing cancels remaining actions");
        TerminateProcess(GetCurrentProcess(),0);
    }
    if(frame>650) verify(false,"timed out");
    return 0;
}
