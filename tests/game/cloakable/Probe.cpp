#include "../../../src/Starfall.cpp"

namespace {
void checkCloak(bool ok,const char* label) {
    std::ofstream("cloakable.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
UnitClass* testTank(DWORD id) {
    for(auto unit:UnitClass::Array) if(unit && unit->UniqueID==id) return unit;
    return nullptr;
}
void checkSaveValidation() {
    IStream* stream=nullptr;
    checkCloak(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)) && saveState(stream),"memory save");
    HGLOBAL memory=nullptr;GetHGlobalFromStream(stream,&memory);
    STATSTG stat{};stream->Stat(&stat,STATFLAG_NONAME);
    auto bytes=static_cast<const BYTE*>(GlobalLock(memory));
    vector<BYTE> saved(bytes,bytes+stat.cbSize.LowPart);GlobalUnlock(memory);stream->Release();
    const auto expected=cloakableUnits;
    auto load=[&](const vector<BYTE>& data) {
        IStream* input=nullptr;CreateStreamOnHGlobal(nullptr,TRUE,&input);
        input->Write(data.data(),static_cast<ULONG>(data.size()),nullptr);
        LARGE_INTEGER zero{};input->Seek(zero,STREAM_SEEK_SET,nullptr);
        bool ok=loadState(input);input->Release();return ok;
    };
    checkCloak(!expected.empty(),"save contains cloak sources");
    auto bad=saved;bad.pop_back();
    auto payload=static_cast<DWORD>(bad.size()-8);memcpy(bad.data()+4,&payload,4);
    checkCloak(!load(bad) && cloakableUnits==expected,"truncated cloak data is atomic");
    bad=saved;memset(bad.data()+bad.size()-4,0,4);
    checkCloak(!load(bad) && cloakableUnits==expected,"zero cloak ID rejected atomically");
    auto old=saved;
    old.resize(old.size()-4*(expected.size()+1));
    DWORD tag=bulletOwnerSaveTag;payload=static_cast<DWORD>(old.size()-8);
    memcpy(old.data(),&tag,4);memcpy(old.data()+4,&payload,4);
    checkCloak(load(old) && cloakableUnits.empty(),"previous save format accepted");
    checkCloak(load(saved) && cloakableUnits==expected,"cloak capability restored immediately");
    Research::ensureReady();
}
}

DEFINE_HOOK(0x55B4EB,SF_TestCloakable,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int f=Unsorted::CurrentFrame;
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(f>=165) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    if(f!=20 && f!=30 && f!=40 && f!=60 && f!=65 && f!=75 && f!=95 && f!=115 && f!=135 && f!=160) return 0;
    DWORD tankId=0,peerId=0;
    UnitClass *tank=nullptr,*peer=nullptr;
    if(f==20) {
        auto human=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
        for(auto u:UnitClass::Array) if(u->Owner==human && !_stricmp(u->Type->ID,"MTNK")) {
            if(!tank) tank=u;else if(!peer) peer=u;
        }
        checkCloak(tank && peer && !tank->Cloakable && !tank->Type->Cloakable,"native non-cloaking tanks");
        std::ofstream("cloakable.ids")<<tank->UniqueID<<' '<<peer->UniqueID;
        checkCloak(skills.contains("delayedcloak") && skills.contains("alwayscloak") && skills.contains("cloakoff") &&
            !skills.contains("invalidcloakvalue") && !skills.contains("invalidcloakevent") && !skills.contains("invalidcloakcondition"),"configuration validation");
        tank->SetTarget(nullptr);tank->ForceMission(Mission::Guard);
        peer->SetTarget(nullptr);peer->ForceMission(Mission::Guard);
        tank->Health=tank->Type->Strength/2;
        grantToUnit(tank,PassiveGrant{{"delayedcloak"},-1});
        grantToUnit(peer,PassiveGrant{{"cloakoff"},-1});
        tickStates();
        checkCloak(!tank->IsCloakable() && !peer->IsCloakable(),"delayed entry and no switch");
    } else {
        std::ifstream("cloakable.ids")>>tankId>>peerId;
        tank=testTank(tankId);peer=testTank(peerId);
        checkCloak(tank && peer,"test units survive");
    }
    if(f==30) checkCloak(!tank->IsCloakable(),"delay remains pending");
    if(f==40) checkCloak(tank->IsCloakable() && !peer->IsCloakable() && !tank->Cloakable && !tank->Type->Cloakable,"same type isolation without native flag writes");
    if(f==60 && !loading) {
        std::ofstream("cloakable.log",std::ios::app)<<"cloak state="<<int(tank->CloakState)<<" peer="<<int(peer->CloakState)
            <<" ready="<<tank->IsReadyToCloak()<<" forbidden="<<tank->ShouldNotBeCloaked()<<" delay="<<tank->CloakDelayTimer.GetTimeLeft()
            <<" stages="<<RulesClass::Instance->CloakingStages<<" progress="<<tank->CloakProgress.Value<<" rate="<<tank->CloakProgress.Rate<<'\n';
        checkCloak(tank->CloakState==CloakState::Cloaked && peer->CloakState==CloakState::Uncloaked,"native cloak transition");
        grantToUnit(tank,PassiveGrant{{"alwayscloak"},70});
        tickStates();
        checkSaveValidation();
        checkCloak(ScenarioClass::SaveGame(GetModuleHandleA("CnCNet-Spawner.dll")?"Cloakable.sav":"Saved Games\\Cloakable.sav",L"Cloakable"),"saved while cloaked");
    }
    if(f==65) checkCloak(tank->IsCloakable() && !tank->Cloakable && tank->CloakState==CloakState::Cloaked,"cloak and capability survive save");
    if(f==75) {
        tank->Health=tank->Type->Strength;tickStates();
        checkCloak(tank->IsCloakable(),"overlapping source remains");
        peer->Cloakable=true; // Native crate/type capability is independent of the modifier.
        grantToUnit(peer,PassiveGrant{{"alwayscloak"},30});tickStates();
        checkCloak(peer->IsCloakable() && peer->Cloakable,"native capability coexists");
        for(const char* typeId:{"E1","GAPOWR","ORCA"}) {
            auto type=TechnoTypeClass::Find(typeId);
            auto other=type?static_cast<TechnoClass*>(type->CreateObject(tank->Owner)):nullptr;
            checkCloak(other!=nullptr,"other class created");
            CellStruct cell={static_cast<short>(typeId[0]=='E'?42:typeId[0]=='G'?46:50),72};
            checkCloak(other->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoords(),DirType::North),"other class placed");
            grantToUnit(other,PassiveGrant{{"alwayscloak"},-1});tickStates();
            checkCloak(other->IsCloakable() && !other->Cloakable,"infantry building aircraft capability");
            removeHeldPassive(other,"alwayscloak");tickStates();
            checkCloak(!other->IsCloakable(),"other class withdrawal");
            grantToUnit(other,PassiveGrant{{"alwayscloak"},-1});tickStates();
            DWORD id=other->UniqueID;other->UnInit();tickStates();
            checkCloak(!cloakableUnits.contains(id),"destroyed object cleaned");
        }
    }
    if(f==95) {
        auto at=tank->GetCoords();tank->Limbo();tickStates();
        checkCloak(!cloakableUnits.contains(tank->UniqueID) && !tank->IsCloakable(),"off-map capability suspended");
        checkCloak(tank->Unlimbo(at,DirType::North),"returned to map");tickStates();
        checkCloak(tank->IsCloakable(),"return restores active source");
        removeHeldPassive(tank,"alwayscloak");tickStates();
        checkCloak(!tank->IsCloakable(),"explicit removal withdraws capability");
        grantToUnit(tank,PassiveGrant{{"alwayscloak"},35});tickStates();
        checkCloak(tank->IsCloakable(),"regrant restores capability");
    }
    if(f==115) checkCloak(!cloakableUnits.contains(peer->UniqueID) && peer->Cloakable && peer->IsCloakable(),"native capability survives expiry");
    if(f==135) checkCloak(!tank->IsCloakable() && !tank->Cloakable && !cloakableUnits.contains(tank->UniqueID),"last source expires");
    if(f==160) checkCloak(tank->CloakState==CloakState::Uncloaked && peer->IsCloakable(),"native decloak and independent capability retained");
    return 0;
}
