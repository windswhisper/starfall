#include "../../../src/Starfall.cpp"
#include "../../../modules/ActSp/GuardPoints.h"

namespace DockTest {
constexpr CellStruct airportCell{66,67},station{74,69};
void check(bool ok,const char* label) {
    std::ofstream("spawn-docks.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
bool parked(BuildingClass* airport,AircraftClass* plane) {
    if(!plane || !plane->IsOnMap || plane->InLimbo || plane->SpawnOwner!=airport ||
       airport->FindLinkIndex(plane)<0 || !plane->ContainsLink(airport)) return false;
    return plane->GetCoords()==airport->GetDestination(plane);
}
AircraftClass* fillBerth(BuildingClass* airport) {
    auto air=static_cast<AircraftClass*>(AircraftTypeClass::Find("ORCA")->CreateObject(airport->Owner));
    check(air && air->SendCommand(RadioCommand::RequestLink,airport)==RadioCommand::AnswerPositive,"reserve ordinary aircraft berth");
    check(air->Unlimbo(airport->GetDestination(air),DirType::North),"place ordinary aircraft");
    air->ForceMission(Mission::Sleep);
    return air;
}
}
DEFINE_HOOK(0x55B4EB,CheckSpawnDocks,5) {
    using namespace DockTest;
    const int frame=Unsorted::CurrentFrame;
    const bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    static AircraftClass* visitor;
    static AircraftClass* visitors[2]{};
    static int filledAt=-1;
    static bool departing[2]{},restored=false,checkedTakeoff=false;
    static DWORD lostId=0;
    if(frame<24) return 0;
    if(frame==24 && !load) {
        check(BuildingSpawn::createAt(BuildingTypeClass::Find("GAAIRC"),HouseClass::CurrentPlayer,airportCell),"placed airport");
        auto airport=MapClass::Instance.GetCellAt(airportCell)->GetBuilding();
        fillBerth(airport);
        fillBerth(airport);
        visitor=fillBerth(airport);
    }
    auto airport=MapClass::Instance.GetCellAt(airportCell)->GetBuilding();
    if(!airport || !airport->SpawnManager || airport->SpawnManager->SpawnedNodes.Count!=2) check(false,"two spawn slots");
    auto& slots=airport->SpawnManager->SpawnedNodes;
    if(frame==80 && !load) {
        check(parked(airport,slots[0]->Unit) && slots[1]->Unit->InLimbo && !airport->HasFreeLink(),"full airport preserves ordinary aircraft berths");
        visitor->UnInit();
    }
    if(frame==110 && !load) {
        check(parked(airport,slots[0]->Unit) && parked(airport,slots[1]->Unit) &&
            airport->FindLinkIndex(slots[0]->Unit)!=airport->FindLinkIndex(slots[1]->Unit),"new spawns use free docks");
        check(GuardPoints::set(airport,station),"issued patrol");
    }
    if(!load && frame>110) {
        if(filledAt<0 && slots[0]->Unit->GetMapCoords().X>=72 && slots[1]->Unit->GetMapCoords().X>=72) {
            for(int i=0;i<2;++i) {
                check(!airport->ContainsLink(slots[i]->Unit),"takeoff frees the berth");
                visitors[i]=fillBerth(airport);
                slots[i]->Unit->Ammo=0;
            }
            filledAt=frame;
        }
        if(filledAt>=0 && frame==filledAt+110) {
            for(int i=0;i<2;++i) {
                auto plane=slots[i]->Unit;
                check(plane->IsOnMap && !plane->InLimbo && plane->Ammo==0 && !airport->ContainsLink(plane) &&
                    slots[i]->Status==SpawnNodeStatus::Returning && airport->ContainsLink(visitors[i]),"returning spawn waits for a free berth");
                visitors[i]->UnInit();
            }
        }
        if(slots[0]->Status==SpawnNodeStatus::Reloading && slots[1]->Status==SpawnNodeStatus::Reloading) {
            check(parked(airport,slots[0]->Unit) && parked(airport,slots[1]->Unit) &&
                slots[0]->Unit->GetCoords()!=slots[1]->Unit->GetCoords(),"both returned to separate docks");
            check(ScenarioClass::SaveGame("Saved Games\\SpawnDocks.sav",L"Spawn docks"),"saved parked spawns");
            TerminateProcess(GetCurrentProcess(),0);
        }
    }
    if(load) {
        if(!restored) {
            check(parked(airport,slots[0]->Unit) && parked(airport,slots[1]->Unit) && !airport->HasFreeLink(),"parked spawns restored");
            restored=true;
        }
        for(int i=0;i<2;++i) {
            auto plane=slots[i]->Unit;
            if(!plane) continue;
            if(slots[i]->Status==SpawnNodeStatus::Reloading && (plane->Ammo!=0 || !parked(airport,plane)))
                check(false,"airfield does not reload before spawn timer");
            if(slots[i]->Status==SpawnNodeStatus::Preparing && plane->Ammo==1 && !airport->ContainsLink(plane))
                departing[i]=true;
        }
        if(departing[0] && departing[1] && !checkedTakeoff) {
            check(true,"both took off after native reload");
            checkedTakeoff=true;
        }
        if(checkedTakeoff && !lostId && slots[0]->Unit->GetMapCoords().X>=72)
            slots[0]->Unit->Ammo=0;
        if(checkedTakeoff && !lostId && slots[0]->Status==SpawnNodeStatus::Reloading && parked(airport,slots[0]->Unit)) {
            lostId=slots[0]->Unit->UniqueID;
            int damage=slots[0]->Unit->Health+100;
            slots[0]->Unit->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,nullptr,true,false,nullptr);
        }
        if(lostId && slots[0]->Unit && slots[0]->Unit->UniqueID!=lostId && slots[0]->Unit->IsOnMap) {
            check(slots[0]->Unit->SpawnOwner==airport,"destroyed parked child replaced");
            TerminateProcess(GetCurrentProcess(),0);
        }
    }
    if(frame>1600) check(false,"dock cycle timed out");
    return 0;
}
