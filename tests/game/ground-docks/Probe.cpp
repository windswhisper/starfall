#include "../../../src/Starfall.cpp"
#include "../../../modules/ActSp/GroundGuards.h"
#include "../../../modules/ActSp/GuardPoints.h"

namespace GroundDockTest {
constexpr CellStruct site{80,84},station{88,84},recall{81,84},visitorCell{85,88};
void check(bool ok,const char* label) {
    std::ofstream("ground-docks.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
FootClass* child(const GroundGuards::Slot& slot) {
    TargetClass target;
    target.m_ID=slot.child;
    target.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
    return slot.child?target.As_Foot():nullptr;
}
bool parked(BuildingClass* b,FootClass* u) {
    return u && u->IsOnMap && !u->InLimbo && b->ContainsLink(u) && u->ContainsLink(b) &&
        u->GetCoords()==MapClass::Instance.GetCellAt(site)->GetCoords() && u->CurrentMission==Mission::Sleep;
}
}
DEFINE_HOOK(0x55B4EB,CheckGroundDockCycle,5) {
    using namespace GroundDockTest;
    using GroundGuards::DockState;
    const int f=Unsorted::CurrentFrame;
    static const bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    static int stage=0,since=0,hitpoints=0,id=0,due=0;
    static UnitClass* visitor=nullptr;
    if(f<24) return 0;
    if(f==24 && !load) {
        check(BuildingSpawn::createAt(BuildingTypeClass::Find("GAPILL"),HouseClass::CurrentPlayer,site),"placed ordinary outpost");
        auto b=MapClass::Instance.GetCellAt(site)->GetBuilding();
        b->IronCurtain(2500,b->Owner,false);
        visitor=static_cast<UnitClass*>(UnitTypeClass::Find("MTNK")->CreateObject(b->Owner));
        check(visitor && visitor->Unlimbo(MapClass::Instance.GetCellAt(visitorCell)->GetCoords(),DirType::North),"ordinary tank placed");
        visitor->IronCurtain(2500,b->Owner,false);
        visitor->ForceMission(Mission::Sleep);
        check(visitor->SendCommand(RadioCommand::RequestLink,b)==RadioCommand::AnswerPositive,"ordinary tank reserved bay");
    }
    auto b=MapClass::Instance.GetCellAt(site)->GetBuilding();
    if(!b || b->Type->UnitRepair) check(false,"non-repair host");
    auto found=GroundGuards::slots.find(b->UniqueID);
    if(found==GroundGuards::slots.end()) {
        if(f>56) check(false,"missing vehicle slot");
        return 0;
    }
    auto& slot=found->second;
    auto u=child(slot);
    if(!load) {
        if(f==56) {
            check(!u && b->ContainsLink(visitor),"reserved bay waits");
            visitor->SendCommand(RadioCommand::NotifyUnlink,b);
            check(visitor->Limbo(),"move visitor onto open pad");
            ++Unsorted::ScenarioInit;
            bool placed=visitor->Unlimbo(MapClass::Instance.GetCellAt(site)->GetCoords(),DirType::North);
            --Unsorted::ScenarioInit;
            check(placed,"visitor occupies pad without a reservation");
            visitor->ForceMission(Mission::Sleep);
        }
        if(f==80) {
            check(!u && !b->ContainsLink(visitor),"physical occupant blocks spawning");
            check(visitor->Limbo() && visitor->Unlimbo(MapClass::Instance.GetCellAt(visitorCell)->GetCoords(),DirType::North),"visitor cleared pad");
            visitor->ForceMission(Mission::Sleep);
        }
        if(stage==0 && f>70 && parked(b,u)) {
            check(u->Health==u->GetTechnoType()->Strength && !u->GetHeight(),"full-health tank parked");
            u->IronCurtain(2500,b->Owner,false);
            id=slot.child;
            check(GuardPoints::set(b,station),"dispatch to rally");
            stage=1;
        } else if(stage==1 && u && u->GetMapCoords()==station) {
            check(!b->ContainsLink(u) && slot.child==id && !u->GetHeight(),"drove out");
            hitpoints=u->Health-60;
            u->Health=hitpoints;
            check(GuardPoints::set(b,recall),"recall via adjacent rally");
            stage=2;
        } else if(stage==2 && parked(b,u)) {
            check(u->Health==hitpoints && slot.child==id,"manual recall without healing");
            GuardPoints::set(b,station);
            stage=3;
        } else if(stage==3 && u && u->GetMapCoords()==station) {
            check(visitor->SendCommand(RadioCommand::RequestLink,b)==RadioCommand::AnswerPositive,"visitor reserved vacated bay");
            u->Ammo=0;
            u->ReloadTimer.Start(0);
            since=f;
            stage=4;
        } else if(stage==4 && f>=since+40) {
            check(u && !u->Ammo && b->ContainsLink(visitor) && !b->ContainsLink(u) && u->GetMapCoords()!=site,"occupied return waits");
            visitor->SendCommand(RadioCommand::NotifyUnlink,b);
            stage=5;
        } else if(stage==5 && parked(b,u) && slot.readyFrame) {
            check(!u->Ammo && u->Health==hitpoints,"returned without field reload or repair");
            std::ofstream("ground-dock-id.txt")<<id<<" "<<hitpoints<<" "<<slot.readyFrame;
            check(ScenarioClass::SaveGame("Saved Games\\GroundDocks.sav",L"Ground docks"),"saved while reloading");
            TerminateProcess(GetCurrentProcess(),0);
        }
    } else {
        if(stage==0) {
            std::ifstream("ground-dock-id.txt")>>id>>hitpoints>>due;
            check(parked(b,u) && id==slot.child && u->Ammo==0 && slot.readyFrame==due && due>=f,"parked vehicle restored");
            stage=1;
        }
        if(stage==1 && f<due && (!parked(b,u) || u->Ammo || u->Health!=hitpoints)) check(false,"premature service");
        if(stage==1 && u && u->GetMapCoords()==station) {
            check(slot.child==id && u->Ammo==2 && u->Health==hitpoints && !b->ContainsLink(u),"same tank drove out with ammo and unchanged health");
            GuardPoints::set(b,recall);
            stage=2;
        } else if(stage==2 && parked(b,u)) {
            u->UnInit();
            since=f;
            stage=3;
        } else if(stage==3 && parked(b,u) && slot.child!=id) {
            check(f>=since+48,"respawn delay respected");
            check(u->Ammo==2 && u->Health==u->GetTechnoType()->Strength,"destroyed vehicle replaced at bay");
            TerminateProcess(GetCurrentProcess(),0);
        }
    }
    if(f>1900) {
        std::ofstream("ground-docks.log",std::ios::app)<<"stage="<<stage<<" dock="<<int(slot.dock)<<" link="<<(u?b->FindLinkIndex(u):-1)
            <<" unit="<<(u?u->GetMapCoords().X:-1)<<","<<(u?u->GetMapCoords().Y:-1)<<"\n";
        check(false,"dock cycle timed out");
    }
    return 0;
}
