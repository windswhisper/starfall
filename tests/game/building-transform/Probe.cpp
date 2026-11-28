#include "../../../src/Starfall.cpp"

namespace {
void check(bool ok,const char* text) {
    std::ofstream("building-transform.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<text<<'\n';
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
BuildingClass* findBuilding(DWORD id) {
    for(auto b:BuildingClass::Array) if(b && b->UniqueID==id) return b;
    return nullptr;
}
BuildingClass* place(BuildingTypeClass* type,HouseClass* owner,CellStruct cell) {
    check(BuildingSpawn::create(type,owner,MapClass::Instance.GetCellAt(cell)->GetCoords(),0),"place building fixture");
    return MapClass::Instance.GetCellAt(cell)->GetBuilding();
}
int state(DWORD id,const char* key) {
    auto it=scriptTimers.state.find({id,"transformscript",key});
    return it==scriptTimers.state.end()?-1:static_cast<int>(it->second.integer);
}
void identities() {
    std::set<DWORD> ids;
    bool unique=true;
    for(auto object:TechnoClass::Array) if(object) unique&=ids.insert(object->UniqueID).second;
    check(unique,"unique native object IDs");
}
void shapes(BuildingTypeClass* a,BuildingTypeClass* b) {
    check(BuildingTransform::sameFoundation(a,b),"matching native foundations");
    const bool originalBib=b->Bib;b->Bib=!a->Bib;
    check(!BuildingTransform::sameFoundation(a,b),"different bib footprint rejected");b->Bib=originalBib;
    auto pa=a->FoundationData,pb=b->FoundationData;
    bool ba=a->Bib,bb=b->Bib;a->Bib=b->Bib=false;
    CellStruct left[]={{0,0},{1,0},{0,1},{0x7FFF,0x7FFF}};
    CellStruct right[]={{0,0},{1,0},{1,1},{0x7FFF,0x7FFF}};
    CellStruct reordered[]={{0,1},{1,0},{0,0},{0x7FFF,0x7FFF}};
    a->FoundationData=left;b->FoundationData=right;
    check(a->GetFoundationData(false)==left && b->GetFoundationData(false)==right,"custom foundation fixture");
    check(!BuildingTransform::sameFoundation(a,b),"same bounds different occupied cells rejected");
    b->FoundationData=reordered;
    check(BuildingTransform::sameFoundation(a,b),"foundation order is irrelevant");
    a->FoundationData=pa;b->FoundationData=pb;a->Bib=ba;b->Bib=bb;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestBuildingTransform,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const int frame=Unsorted::CurrentFrame;
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto from=BuildingTypeClass::Find("GAPOWR"),to=BuildingTypeClass::Find("NAHAND");
    if(!loading && frame==20) {
        Research::ensureReady();
        check(skills.contains("transformini") && skills.contains("transformscript") && skills.contains("temporarybuilding"),"building Transform parsed");
        shapes(from,to);
        auto unit=place(from,house,{50,65});const auto id=unit->UniqueID;
        grantToUnit(unit,PassiveGrant{{"temporarybuilding"},100});
        grantToUnit(unit,PassiveGrant{{"transformscript","transformini"},-1});
        changeStacks(id,"temporarybuilding",2,INT_MAX);
        check(UnitHealth::change(unit,100),"permanent health bonus fixture");
        UnitHealth::heal(unit,UnitHealth::read(unit).maximum/2);
        unit->Veterancy.Veterancy=1.0f;
        unit->IronCurtain(90,house,false);
        tickStates();
        auto hp=UnitHealth::read(unit);const auto at=unit->GetMapCoords();
        const auto originalCount=BuildingClass::Array.Count;
        check(!transformUnit(unit,BuildingTypeClass::Find("GAREFN")) && findBuilding(id)==unit && unit->GetMapCoords()==at &&
              UnitHealth::read(unit).current==hp.current && BuildingClass::Array.Count==originalCount,"different footprint leaves original intact");
        unit->Upgrades[0]=to;
        check(!transformUnit(unit,to),"installed upgrade rejects conversion");unit->Upgrades[0]=nullptr;
        auto mission=unit->CurrentMission;unit->CurrentMission=Mission::Selling;
        check(!transformUnit(unit,to),"selling rejects conversion");unit->CurrentMission=mission;
        const int ownedCount=house->Buildings.Count;
        const auto speed=to->SpeedType;to->SpeedType=SpeedType::Float;
        const bool rejected=!transformUnit(unit,to);to->SpeedType=speed;
        check(rejected && findBuilding(id)==unit && unit->IsOnMap && !unit->InLimbo &&
              unit->GetMapCoords()==at && unit->CurrentMission==mission && house->Buildings.Count==ownedCount &&
              UnitHealth::read(unit).current==hp.current && ownsPassive(unit,"temporarybuilding"),"placement failure restores original building and state");
        const int countTo=house->CountOwnedNow(to);
        TargetClass nativeHandle(unit);
        check(nativeHandle.As_Building()==unit,"native handle fixture");
        capture("onfire",unit,nullptr,unit,unit,0);dispatch();
        unit=findBuilding(id);
        check(unit && unit->Type==to && unit->Owner==house && unit->GetMapCoords()==at && state(id,"count")==2,"Lua identity and immediate continuation");
        check(nativeHandle.As_Building()==unit && TargetClass(unit).As_Building()==unit,"native handle follows replacement");
        const auto after=UnitHealth::read(unit);
        const int maximum=to->Strength+100;
        const int current=static_cast<int>((int64_t(hp.current)*maximum+hp.maximum/2)/hp.maximum)+1;
        check(after.maximum==maximum && after.current==current && unit->Veterancy.IsVeteran(),"health ratio bonus and rank retained");
        int liveCount=0;for(auto b:BuildingClass::Array) if(alive(b) && b->IsOnMap && !b->InLimbo) ++liveCount;
        check(house->CountOwnedNow(to)==countTo+1 && liveCount==originalCount,"one live replacement before deferred cleanup");
        check(warheadGrants.at(id).unit==unit && ownsPassive(unit,"temporarybuilding") && stackCount(id,"temporarybuilding")==2 &&
              unit->IsIronCurtained() && unit->IronCurtainTimer.GetTimeLeft()==90 && unit->IsCloakable() &&
              std::abs(unit->FirepowerMultiplier-1.25)<0.0001,"temporary effects and original deadlines retained");
        bool attached=false;AnimFrame animations;
        for(auto& [key,record]:attachedAnims) if(key.first==id)
            if(auto anim=animations.resolve(record)) attached|=anim->OwnerObject==unit;
        check(attached,"passive animation follows replacement");
        check(counts.at(id).at("transformscript").next==100 && scriptTimers.queue.size()==2,"cooldown and queued callbacks retained");
        auto second=place(from,house,{60,65});const auto secondId=second->UniqueID;
        grantToUnit(second,PassiveGrant{{"transformini"},-1});
        capture("ondeploy",second,nullptr,second,second,0);dispatch();
        check(findBuilding(secondId) && findBuilding(secondId)->Type==to,"INI building conversion");
        identities();
        std::ofstream("building-transform.ids")<<id<<' '<<secondId;
    }
    if(!loading && frame==21) {
        // Save at a frame boundary, after the engine's deferred deletion queue
        // has drained, as the normal save UI does. Callbacks are still pending.
        check(BuildingClass::Array.Count==2 && house->CountOwnedNow(from)==0 && house->CountOwnedNow(to)==2,
              "retired buildings cleaned before save");
        const char* file=GetModuleHandleA("CnCNet-Spawner.dll")?"BuildingTransform.sav":"Saved Games\\BuildingTransform.sav";
        check(ScenarioClass::SaveGame(file,L"Building Transform"),"saved conversion with queued scripts");
    }
    if(frame==65 || frame==125) {
        DWORD id,second;std::ifstream("building-transform.ids")>>id>>second;
        auto unit=findBuilding(id);
        check(unit && unit->Type==from && unit->Owner==house && unit->GetMapCoords()==CellStruct{50,65} &&
              state(id,"count")==5 && scriptTimers.queue.empty(),"repeated conversion and callbacks survive save load");
        TargetClass originalHandle;originalHandle.m_ID=id;originalHandle.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
        check(originalHandle.As_Building()==unit,"native target index survives cleanup and load");
        if(frame==65) {
            check(house->CountOwnedNow(from)==1 && house->CountOwnedNow(to)==1,"native ownership counts updated once");
            check(ownsPassive(unit,"temporarybuilding") && stackCount(id,"temporarybuilding")==2 && unit->IsIronCurtained() &&
                  counts.at(id).at("transformscript").next==100,"effects and cooldown persist across second conversion");
            check(house->PowerOutput>0 && findBuilding(second)->Type==to,"converted buildings participate in power");
            check(Research::producers.contains(id) && Research::producers.at(id).elapsed>0 &&
                  !Research::producers.contains(second),"research income follows the transformed type and retained ID");
        } else {
            tickStates();
            check(!ownsPassive(unit,"temporarybuilding") && !unit->IsIronCurtained() && !unit->IsCloakable() &&
                  std::abs(unit->FirepowerMultiplier-1.0)<0.0001,"temporary effects expire at original time");
            identities();TerminateProcess(GetCurrentProcess(),0);
        }
    }
    return 0;
}
