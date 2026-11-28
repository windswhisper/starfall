#include "../../../src/Starfall.cpp"
#include <ScriptClass.h>

namespace {
constexpr CellStruct first{67,89}, second{71,78};
struct Route {
    DWORD unit=0,team=0;
    bool firstReached=false,secondReached=false,reissued=false;
    int previousLine=-9;
};
Route affected,baseline;
UnitClass* caster=nullptr;
int started=-1,released=-1;
void require(bool ok,const char* label) {
    std::ofstream("taunt-team.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
UnitClass* place(const char* type,HouseClass* owner,CellStruct cell) {
    auto unit=static_cast<UnitClass*>(UnitTypeClass::Find(type)->CreateObject(owner));
    require(unit && unit->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge(),DirType::North),"placed unit");
    unit->Health=unit->EstimatedHealth=20000;
    return unit;
}
void makeTeam(Route& route,CellStruct start) {
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    auto unit=place("MTNK",foe,start);
    auto type=TeamTypeClass::Find("SFTauntRoute");
    require(type && type->TaskForce && type->ScriptType,"map team loaded");
    auto team=type->CreateTeam(foe);
    require(team && team->CurrentScript && team->AddMember(unit,false),"native team recruited tank");
    team->IsHasBeen=true;
    team->IsMoving=true;
    if(team->UniqueID==DWORD(-1)) team->Create_ID();
    route.unit=unit->UniqueID;route.team=team->UniqueID;
}
int distance(CellStruct a,CellStruct b) {return std::max(std::abs(a.X-b.X),std::abs(a.Y-b.Y));}
void observe(Route& route,bool challenge,int frame) {
    if(route.secondReached) return;
    UnitClass* unit=nullptr;
    for(auto t:UnitClass::Array) if(t->UniqueID==route.unit) unit=t;
    if(!unit || !unit->Team || unit->Team->UniqueID!=route.team || !unit->Team->CurrentScript)
        require(false,"tank lost its original team or script");
    auto team=unit->Team;
    auto line=team->CurrentScript->CurrentMission;
    auto cell=unit->GetMapCoords();
    auto dest=unit->Destination?CellClass::Coord2Cell(unit->Destination->GetCoords()):CellStruct::Empty;
    const bool locked=Taunt::targetOf(unit)!=0;
    if(frame%50==0 || route.previousLine!=line || (challenge && started>=0 && !locked && released<0)) {
        std::ofstream("taunt-team.log",std::ios::app)<<"frame "<<frame<<(challenge?" taunted":" baseline")<<" line "<<line
            <<" pos "<<cell.X<<","<<cell.Y<<" dest "<<dest.X<<","<<dest.Y<<" lock "<<locked<<" mark "<<team->AchievedGreatSuccess<<"\n";
    }
    route.previousLine=line;
    if(challenge && started<0 && line==0 && cell.X>=48 && distance(dest,first)<=2 && distance(cell,first)>8) {
        caster=place("AMCV",HouseClass::CurrentPlayer,{48,80});
        require(grantToUnit(unit,PassiveGrant{{"challenge"},75},true,caster) && unit->Target==caster,"taunted during native waypoint move");
        started=frame;
    } else if(challenge && started>=0 && !locked && released<0) {
        released=frame;
        require(frame>=started+75,"taunt expired naturally");
    }
    if(challenge && released<0) return;
    if(line==0 && distance(dest,first)<=2) route.reissued=true;
    if(distance(cell,first)<=4) route.firstReached=true;
    if(route.firstReached && team->AchievedGreatSuccess && line>=3 && distance(cell,second)<=4) {
        route.secondReached=true;
        require(route.reissued,challenge?"team reissued first waypoint":"baseline waypoint issued");
        require(true,challenge?"team resumed original waypoint and completed next step":"control completed both waypoints");
    }
}
}
DEFINE_HOOK(0x55B4EB,SF_TestTauntTeam,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    if(frame==20) {
        Research::ensureReady();
        auto scenario=ScenarioClass::Instance;
        require(scenario->GetWaypointCoords(20)==first && scenario->GetWaypointCoords(21)==second,"map waypoints match both destinations");
        makeTeam(affected,{45,89});makeTeam(baseline,{45,92});
    }
    if(frame>=20) {
        observe(affected,true,frame);observe(baseline,false,frame);
        if(affected.secondReached && baseline.secondReached) TerminateProcess(GetCurrentProcess(),0);
        if(frame>=1500) require(false,"route did not finish within 1500 frames");
    }
    return 0;
}
