#include "../../../src/Starfall.cpp"

namespace {
DWORD shooterId=0;
int nativeShots=0;
LaserDrawClass* lastLaser=nullptr;
CoordStruct sourceAt(CellStruct cell,int dx,int dy,int height) {
    auto& map=MapClass::Instance;
    auto p=map.GetCellAt(cell)->GetCoords();
    p.X+=dx;p.Y+=dy;
    p.Z=map.GetCellFloorHeight(p);
    p=map.GetCellAt(p)->FixHeight(p);
    p.Z+=height;
    return p;
}
void checkVolley(bool ok,const char* label) {
    std::ofstream("weapon-script.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
int state(DWORD id,const char* key) {
    auto it=scriptTimers.state.find({id,"@weapon:105mm",key});
    return it==scriptTimers.state.end()?-1:static_cast<int>(it->second.integer);
}
}
DEFINE_HOOK(0x54FE60,SF_TestLuaOriginLaser,6) {
    lastLaser=R->ECX<LaserDrawClass*>();
    return 0;
}
DEFINE_HOOK(0x6FF66C,SF_TestNativeVolley,6) {
    if(shooterId==R->ESI<TechnoClass*>()->UniqueID) ++nativeShots;
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestWeaponScriptFrame,5) {
    const int f=Unsorted::CurrentFrame;
    if(f!=22 && f!=32 && f!=37 && f!=210) return 0;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    auto player=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    UnitClass* tank=nullptr;
    for(auto unit:UnitClass::Array) if(unit->Owner==player && !_stricmp(unit->Type->ID,"MTNK") &&
        (f==22 || state(unit->UniqueID,"count")>0)) {tank=unit;break;}
    checkVolley(tank!=nullptr,"shooter");
    shooterId=tank->UniqueID;
    auto weapon=tank->GetWeapon(0)->WeaponType;
    checkVolley(weaponFireScripts.contains(weapon) && weapon->Burst==3,"map weapon callback registered");
    auto cell=tank->GetMapCoords();cell.Y+=3;cell.X+=1;
    auto ground=MapClass::Instance.GetCellAt(cell);
    auto spawnTarget=[&]() {
        auto dummy=static_cast<UnitClass*>(tank->Type->CreateObject(HouseClass::FindByPlayerAt(HouseClass::PlayerAtB)));
        checkVolley(dummy && dummy->Unlimbo(ground->GetCoordsWithBridge(),DirType::North),"placed target");
        dummy->Health=30000;dummy->Deactivate();dummy->DiscoveredBy(player);
        return dummy;
    };
    if(f==22 && !loading) {
        // Exercise precedence against a real weapon setting as well as the unmodified Lua call.
        auto ini=CCINIClass::INI_Rules;
        ini->WriteString(weapon->ID,"FireOrigin","Target");
        ini->WriteString(weapon->ID,"FireOrigin.Offset","0,0,512");
        FireOrigin::readRules();
        auto dummy=spawnTarget();
        auto point=dummy->GetMapCoords();
        grantToUnit(tank,PassiveGrant{{"shotcounter"},-1});
        const auto direction=tank->GetTargetDirection(dummy);
        tank->PrimaryFacing.SetCurrent(direction);
        tank->SecondaryFacing.SetCurrent(direction);
        tank->Ammo=10;
        tank->RearmTimer.Stop();
        checkVolley(tank->GetFireError(dummy,0,false)==FireError::OK,"ready and facing target");
        tank->Fire(dummy,0);
        int fullDelay=tank->RearmTimer.GetTimeLeft();
        tank->Fire(dummy,0);
        dispatch();
        checkVolley(state(shooterId,"target")==static_cast<int>(dummy->UniqueID) &&
            state(shooterId,"x")==point.X && state(shooterId,"y")==point.Y,"live target and snapshot");
        checkVolley(state(shooterId,"count")==1 && tank->Ammo==9 && fullDelay>=20 && tank->CurrentBurstIndex==0 &&
            Research::balance(player->ArrayIndex)==1 && nativeShots==0 && dummy->Health==30000,"one volley and full rearm");

        bool direct=false,preset=false,self=false;
        for(auto shot:BulletClass::Array) if(shot->Owner==tank && shot->WeaponType==weapon) {
            if(shot->Target==dummy && shot->SourceCoords==sourceAt(point,64,-64,1152)) direct=true;
            if(shot->Target==dummy && shot->SourceCoords==tank->GetCoords()) preset=true;
            if(shot->Target==tank && shot->SourceCoords==sourceAt(point,0,0,512)) self=true;
        }
        checkVolley(direct && preset && self,"Lua origin overrides weapon rules per shot for direct list and preset calls");
        tank->RearmTimer.Stop();tank->Ammo=0;
        tank->Fire(dummy,0);dispatch();
        checkVolley(state(shooterId,"count")==1 && !tank->RearmTimer.GetTimeLeft(),"empty ammo skips volley");
        tank->Ammo=9;tank->Fire(dummy,0);
        dummy->UnInit();dispatch();
        checkVolley(state(shooterId,"count")==2 && state(shooterId,"target")==0 &&
            state(shooterId,"x")==point.X && state(shooterId,"y")==point.Y,"lost target keeps snapshot");
        tank->RearmTimer.Stop();tank->Fire(ground,0);dispatch();
        checkVolley(state(shooterId,"count")==3 && state(shooterId,"target")==0 &&
            state(shooterId,"x")==cell.X && state(shooterId,"y")==cell.Y,"ground and lost targets");
        tank->RearmTimer.Stop();tank->Fire(ground,0);
        checkVolley(state(shooterId,"count")==3 && ScenarioClass::SaveGame("Saved Games\\WeaponScript.sav",L"WeaponScript"),
            "saved pending volley");
    }
    if(f==32) {
        checkVolley(state(shooterId,"count")==4 && state(shooterId,"finished")==4 &&
            state(shooterId,"followUp")==1 && state(shooterId,"lastX")==cell.X && state(shooterId,"lastY")==cell.Y,"restored callbacks and positions");
    }
    if(f==37) {
        auto dummy=spawnTarget();
        tank->SetTarget(dummy);tank->QueueMission(Mission::Attack,true);
    }
    if(f==210) {
        checkVolley(state(shooterId,"count")>=6 && state(shooterId,"count")<=11 && nativeShots==0,"repeated attack orders");
        auto wh=WarheadTypeClass::Find("SA");
        checkVolley(wh && wh->CellSpread==0 && ActionSequence::warheadScripts.contains(wh),"map binds default detonation function");
        auto at=tank->GetCoords();
        auto balance=Research::balance(player->ArrayIndex);
        auto last=ActionSequence::lastCast;
        PassiveAction hit{};hit.action="detonatewarhead";hit.warhead=wh;hit.amount=17;
        lastLaser=nullptr;
        checkVolley(weaponEffect(hit,tank,tank,at,at,player),"direct scripted warhead fired");
        checkVolley(ActionSequence::lastCast==last+1 && Research::balance(player->ArrayIndex)==balance+7,
                    "direct-only warhead uses map parameters and suppresses synchronous self-recursion");
        checkVolley(lastLaser && lastLaser->Source==sourceAt(tank->GetMapCoords(),64,0,768),
                    "casterless Lua laser renders at its explicit origin");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
