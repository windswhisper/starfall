#include "../../../src/Starfall.cpp"
namespace {
void checkTaunt(bool ok,const char* label) {
    std::ofstream("taunt.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
UnitClass* placeTank(const char* type,HouseClass* house,CellStruct cell) {
    auto unit=static_cast<UnitClass*>(UnitTypeClass::Find(type)->CreateObject(house));
    checkTaunt(unit && unit->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge(),DirType::North),"place");
    unit->Health=unit->EstimatedHealth=20000;
    return unit;
}
void executeOrder(EventClass& event) {
    event.IsExecuted=false;
    reinterpret_cast<void(__thiscall*)(EventClass*)>(0x4C6CB0)(&event);
}
void issueMove(UnitClass* unit) {
    EventClass order(unit->Owner->ArrayIndex,TargetClass(unit),Mission::Move,TargetClass{},TargetClass(CellStruct{44,70}),TargetClass{});
    executeOrder(order);
}
}
DEFINE_HOOK(0x55B4EB,SF_TestTaunt,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    auto frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(!loading && frame==20) {
        Research::ensureReady();
        auto player=HouseClass::CurrentPlayer,enemy=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
        auto unit=placeTank("MTNK",player,{43,73});
        auto caster=placeTank("MTNK",enemy,{54,73});
        auto decoy=placeTank("HTNK",enemy,{53,74});
        auto peer=placeTank("MTNK",player,{42,73});
        auto gi=static_cast<InfantryClass*>(InfantryTypeClass::Find("E1")->CreateObject(player));
        checkTaunt(gi && gi->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{48,66})->GetCoordsWithBridge(),DirType::North),"place GI");
        EventClass deploy(player->ArrayIndex,EventType::Deploy);
        deploy.Deploy.Whom=TargetClass(gi);executeOrder(deploy);
        auto wh=WarheadTypeClass::Find("HE");
        deliverWarheadGrants(wh,peer->GetCoords(),enemy,peer,caster);
        checkTaunt(peer->Target==caster && Taunt::targetOf(peer)==caster->UniqueID,"warhead locks to firer");
        grantToUnit(peer,PassiveGrant{{"challenge"},8},true,decoy);
        checkTaunt(peer->Target==caster && Taunt::controls.at(peer->UniqueID).bindings.size()==2,"equal priority keeps target");
        checkTaunt(removeHeldPassive(peer,"challenge") && !Taunt::targetOf(peer),"remove clears every source");
        checkTaunt(skills.contains("challenge") && grantToUnit(unit,PassiveGrant{{"challenge"},240},true,caster)
            && unit->Target==caster && unit->CurrentMission==Mission::Attack,"source lock");
        issueMove(unit);issueMove(peer);
        for(auto type:{EventType::Idle,EventType::Deploy,EventType::Scatter}) {
            EventClass order(player->ArrayIndex,type);order.Deploy.Whom=TargetClass(unit);executeOrder(order);
        }
        checkTaunt(unit->Target==caster && unit->CurrentMission==Mission::Attack && peer->Destination,"player commands discarded");
        unit->ForceMission(Mission::Move);unit->QueueMission(Mission::Unload,true);
        checkTaunt(unit->CurrentMission==Mission::Attack && unit->QueuedMission!=Mission::Unload &&
            !ActiveSkills::readyCaster(unit,player),"AI and skill orders blocked");
        Objects objects;
        for(auto t:TechnoClass::Array) if(alive(t)) objects[t->UniqueID]=t;
        auto e=eventSnapshot("ondealdamage",caster,unit,caster,unit,1);
        checkTaunt(runScript(skills.at("placedecoy").actions.front(),e,objects,0) && unit->Target==decoy,"Lua decoy");
        std::ofstream("taunt.ids")<<unit->UniqueID<<" "<<caster->UniqueID<<" "<<decoy->UniqueID<<" "<<unit->Location.X<<" "<<gi->UniqueID;
        checkTaunt(ScenarioClass::SaveGame("Saved Games\\Taunt.sav",L"Taunt"),"save");
        TerminateProcess(GetCurrentProcess(),0);
    }
    if(loading && (frame==21 || frame==37 || frame==65 || frame==150)) {
        DWORD id,c,d,giId;int startX;std::ifstream("taunt.ids")>>id>>c>>d>>startX>>giId;
        Objects live;for(auto unit:TechnoClass::Array) if(alive(unit)) live.emplace(unit->UniqueID,unit);
        auto unit=static_cast<UnitClass*>(live.at(id));
        if(frame==21) checkTaunt(Taunt::targetOf(unit)==d && unit->Target==live.at(d),"restored decoy");
        if(frame==37) checkTaunt(Taunt::targetOf(unit)==c && unit->Target==live.at(c),"priority expires to original caster");
        if(frame==65) {
            auto gi=static_cast<InfantryClass*>(live.at(giId));
            checkTaunt(gi->IsDeployed() && grantToUnit(gi,PassiveGrant{{"challenge"},180},true,live.at(c)),"deployed unit accepts lock");
        }
        if(frame==150) {
            auto gi=static_cast<InfantryClass*>(live.at(giId));
            checkTaunt(gi->IsDeployed() && gi->GetMapCoords()==CellStruct{48,66} && Taunt::targetOf(gi)==c,"deployed unit holds position");
            checkTaunt(unit->Location.X>startX+128,"pursued out of range target");
            checkTaunt(live.at(c)->Health<20000,"attack dealt damage");
            live.at(c)->CloakState=CloakState::Cloaked;
            Taunt::tick();
            issueMove(unit);
            checkTaunt(!Taunt::targetOf(unit) && ActiveSkills::readyCaster(unit,unit->Owner) && unit->Destination,"loss releases commands");
            TerminateProcess(GetCurrentProcess(),0);
        }
    }
    return 0;
}
