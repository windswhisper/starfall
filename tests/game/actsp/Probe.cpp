#include "../../../src/Starfall.cpp"
#include <MouseClass.h>
#include <WWKeyboardClass.h>
#include "../../../modules/ActSp/GroundGuards.h"

namespace {
DWORD grizzlyId=0,grizzlyTargetId=0;
bool grizzlyShot=false;
void check(bool value,const char* label) {
    std::ofstream("actsp.log",std::ios::app)<<(value?"PASS ":"FAIL ")<<label<<"\n";
    if(!value) TerminateProcess(GetCurrentProcess(),2);
}
UnitClass* place(const char* id,CellStruct cell,HouseClass* owner) {
    auto u=static_cast<UnitClass*>(UnitTypeClass::Find(id)->CreateObject(owner));
    check(u && u->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoords(),DirType::North),"placed");
    return u;
}
UnitClass* fixture(const char* id) {
    for(auto u:UnitClass::Array)
        if(u->Owner==HouseClass::CurrentPlayer && !_stricmp(u->Type->ID,id)) return u;
    return nullptr;
}
BuildingClass* outpost(CellStruct at) {
    return MapClass::Instance.GetCellAt(at)->GetBuilding();
}
void rally(BuildingClass* building,CellStruct cell) {
    auto action=building->MouseOverCell(&cell);
    check(action==Action::Move,"guard outpost offers rally on terrain");
    check(building->CellClickedAction(action,&cell,&cell,false),"submitted rally through building click");
}
BuildingClass* placeBuilding(const char* id,CellStruct at,HouseClass* house) {
    auto type=BuildingTypeClass::Find(id);
    auto b=type?static_cast<BuildingClass*>(type->CreateObject(house)):nullptr;
    check(b && b->Unlimbo(MapClass::Instance.GetCellAt(at)->GetCoords(),DirType::North),id);
    return b;
}
#include "GroundCheck.inc"
#include "InfantryGuardCheck.inc"
#include "GarrisonCheck.inc"
void captureOutpost(CoordStruct center) {
    auto surface=DSurface::Composite;
    auto [p,visible]=TacticalClass::Instance->CoordsToClient(center);
    if(!visible) return;
    constexpr int width=320,height=196;
    auto left=std::clamp(p.X-width/2,0,surface->GetWidth()-width);
    auto top=std::clamp(p.Y-height/2,0,surface->GetHeight()-height);
    auto pixels=static_cast<const BYTE*>(surface->Lock(left,top));
    if(!pixels) return;
    std::ofstream out("grizzly-outpost.ppm",std::ios::binary);
    out<<"P6\n"<<width<<" "<<height<<"\n255\n";
    auto pitch=surface->GetPitch();
    for(int y=0;y<height;++y) {
        auto row=reinterpret_cast<const WORD*>(pixels+y*pitch);
        for(int x=0;x<width;++x) {
            RGBClass color(row[x],true);
            out.put(color.Red);out.put(color.Green);out.put(color.Blue);
        }
    }
    surface->Unlock();
}
}
DEFINE_HOOK(0x6FF672,ActSp_TestGrizzlyCannon,10) {
    auto firer=R->ESI<TechnoClass*>();
    if(grizzlyId && firer->UniqueID==grizzlyId && firer->Target && firer->Target->UniqueID==grizzlyTargetId)
        grizzlyShot=true;
    if(GroundCheck::childId && firer->UniqueID==GroundCheck::childId && firer->Target && firer->Target->UniqueID==GroundCheck::enemyId)
        GroundCheck::shot=true;
    if(InfantryGuardCheck::childId && firer->UniqueID==InfantryGuardCheck::childId &&
       firer->Target && firer->Target->UniqueID==InfantryGuardCheck::enemyId)
        InfantryGuardCheck::shot=true;
    GarrisonCheck::commit(firer);
    return 0;
}
DEFINE_HOOK(0x55B4EB,ActSp_TestFlight,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const int f=Unsorted::CurrentFrame;
    const bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    auto house=HouseClass::CurrentPlayer;
    if(f==20 && !load) {
        place("BFRT",{80,71},house);
        place("TRUCKA",{81,74},house);
        place("TNKD",{83,68},house);
        place("HTK",{80,77},house);
        place("V3",{78,79},house);
        auto post=placeBuilding("ASGRPOST",{57,88},house);
        auto type=AircraftTypeClass::Find("ASGRDRN");
        check(type && post->SpawnManager && post->SpawnManager->SpawnType==type &&
              post->SpawnManager->SpawnedNodes.Count==1 && post->GetImage() &&
              type->MainVoxel.VXL && type->MainVoxel.HVA,"Grizzly outpost art and one child");
        check(post->Type->UIName && !wcscmp(post->Type->UIName,L"Grizzly Outpost"),"Grizzly outpost sidebar name");
        for(CellStruct at:{CellStruct{73,76},CellStruct{72,80}}) {
            auto b=static_cast<BuildingClass*>(BuildingTypeClass::Find("GAPILL")->CreateObject(house));
            check(b && b->Unlimbo(MapClass::Instance.GetCellAt(at)->GetCoords(),DirType::North),"placed guard outpost");
        }
        auto factory=static_cast<BuildingClass*>(BuildingTypeClass::Find("GAPILE")->CreateObject(house));
        check(factory && factory->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{65,79})->GetCoords(),DirType::North),
              "placed guard factory");
    }
    static int excavatorStart=-1;
    if(excavatorStart>=0) {
        if(ExcavatorCheck::tick(f-excavatorStart,house)) TerminateProcess(GetCurrentProcess(),0);
        return 0;
    }
    GroundCheck::tick(f,load,house);
    InfantryGuardCheck::tick(f,load,house);
    GarrisonCheck::tick(f,load,house);
    if(f<50) return 0;
    auto guard=fixture("BFRT"),escort=fixture("TRUCKA"),orbit=fixture("TNKD"),native=fixture("HTK");
    if(!guard || !escort || !orbit || !native) {check(false,"four carriers");return 0;}
    auto node=guard->SpawnManager->SpawnedNodes[0];
    auto child=node->Unit;
    auto post=outpost({57,88});
    auto drone=post->SpawnManager->SpawnedNodes[0]->Unit;
    static bool grizzlyAirborne=false,grizzlyArrived=false,grizzlyDocked=false,grizzlyRearmed=false;
    if(f==50 && !load) rally(post,{63,88});
    if(drone && !drone->InLimbo) {
        auto pos=drone->GetMapCoords();
        if(drone->GetHeight()==128) grizzlyAirborne=true;
        if(pos==CellStruct{63,88} && drone->GetHeight()==0) grizzlyArrived=true;
    }
    if(!load && (f==140 || f==190)) {
        auto center=MapClass::Instance.GetCellAt(CellStruct{60,88})->GetCoords();
        if(f==140) {
            auto focus=MapClass::Instance.GetCellAt(CellStruct{62,90})->GetCoords();
            TacticalClass::Instance->SetTacticalPosition(&focus);
        }
        else captureOutpost(center);
    }
    auto a=outpost({73,76}),b=outpost({72,80});
    auto factory=outpost({65,79});
    if(!a || !b || !a->SpawnManager || !b->SpawnManager) {check(false,"two guard outposts");return 0;}
    auto wing=escort->SpawnManager->SpawnedNodes[0]->Unit;
    auto left=orbit->SpawnManager->SpawnedNodes[0]->Unit,right=orbit->SpawnManager->SpawnedNodes[1]->Unit;
    static bool fixed=false,follow=false,split=false,moved=false,attacked=false,spent=false,docked=false,back=false;
    static DWORD opponent=0;
    static CellStruct firstOrbit{-1,-1};
    auto dest=[](AircraftClass* u) {
        return u && u->IsOnMap && u->Destination?CellClass::Coord2Cell(u->Destination->GetCoords()):CellStruct{-1,-1};
    };
    auto guards=[&](TechnoClass* parent,CellStruct at) {
        auto u=parent->SpawnManager->SpawnedNodes[0]->Unit;
        return u && u->IsOnMap && !u->InLimbo && (dest(u)==at || u->GetMapCoords()==at);
    };
    if(child && child->GetHeight()>0 && dest(child)==CellStruct{84,71}) fixed=true;
    if(f==50 && !load) {
        check(a->SpawnManager->CountDockedSpawns()==1 && b->SpawnManager->CountDockedSpawns()==1,"unset outposts await orders");
        rally(a,{70,74});
        rally(b,{75,83});
        check(factory && factory->IsUnitFactory(),"native factory rally available");
        rally(factory,{67,77});
    }
    if(f==100 && !load) {
        check(a->ArchiveTarget==MapClass::Instance.GetCellAt(CellStruct{70,74}) &&
              b->ArchiveTarget==MapClass::Instance.GetCellAt(CellStruct{75,83}),"native rally orders executed independently");
        for(TechnoClass* u:{static_cast<TechnoClass*>(b),static_cast<TechnoClass*>(guard),static_cast<TechnoClass*>(orbit)}) {
            check(grantToUnit(u,PassiveGrant{{"orderdrone"},-1}),"granted Lua order");
            tickGains();dispatch();
            check(scriptTimers.state.contains({u->UniqueID,"orderdrone","checked"}),"Lua guard order checked");
        }
    }
    if(f==210) {
        check(guards(a,{70,74}) && guards(b,{71,82}),"distinct rally and Lua stations retained");
        check(guards(guard,{85,71}),"Lua moved mobile carrier station");
        auto factoryRally=factory->ArchiveTarget;
        check(factoryRally && factoryRally->WhatAmI()==AbstractType::Cell &&
              guards(factory,static_cast<CellClass*>(factoryRally)->MapCoords),"factory drones follow production rally");
        rally(b,{75,82});
    }
    auto loc=escort->GetMapCoords();
    const CellStruct escortPoint{short(loc.X+2),loc.Y};
    if(wing && wing->IsOnMap && !wing->InLimbo && (dest(wing)==escortPoint || wing->GetMapCoords()==escortPoint) && loc.X!=81) follow=true;
    if(dest(left).X>=0 && dest(right).X>=0 && dest(left)!=dest(right)) {
        split=true;
        if(firstOrbit.X<0) firstOrbit=dest(left);
        else if(firstOrbit!=dest(left)) moved=true;
    }
    if(f==60 && !load) {
        auto step=escort->Location;
        step.X+=3*256;
        check(ForceMove::moveTo(escort,step.X,step.Y,true),"relocated escort carrier");
    }
    if(f==120 && !load) check(ScenarioClass::SaveGame("Saved Games\\ActSp.sav",L"ActSp"),"saved");
    if(f==240) {
        check(load || fixed,"fixed guard waypoint");
        check(guards(a,{70,74}) && guards(b,{75,82}),"rally retasks airborne drone after Lua");
        check(follow,"escort follows moving carrier");
        check(native->SpawnManager->SpawnedNodes[0]->Unit->InLimbo,"patrol modes");
        auto launcher=fixture("V3");
        check(launcher && launcher->SpawnManager && launcher->SpawnManager->SpawnType->MissileSpawn &&
              launcher->SpawnManager->CountDockedSpawns()>0,"missile remains docked");
        HouseClass* foe=nullptr;
        for(auto h:HouseClass::Array) if(h && !h->IsNeutral() && !h->Defeated && !house->IsAlliedWith(h)) {foe=h;break;}
        check(foe!=nullptr,"opponent");
        grizzlyTargetId=placeBuilding("GAPOWR",{64,88},foe)->UniqueID;
        grizzlyId=drone->UniqueID;
        auto building=static_cast<BuildingClass*>(BuildingTypeClass::Find("GAPOWR")->CreateObject(foe));
        check(building && building->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{84,72})->GetCoords(),DirType::North),"placed enemy building");
        opponent=building->UniqueID;
    }
    // The load phase must observe at least one complete 180-frame orbit.
    if(f==440) check(split && moved,"orbit shifts and separates slots");
    if(f>240 && child && child->Target && child->Target->UniqueID==opponent &&
       child->CurrentMission==Mission::Attack && !guard->SpawnManager->Target) attacked=true;
    if(attacked && !spent && child && child->Ammo==0) {
        spent=true;
        for(auto b:BuildingClass::Array) if(b->UniqueID==opponent) {b->UnInit();break;}
    }
    if(spent && child && node->Status==SpawnNodeStatus::Reloading && child->InLimbo) docked=true;
    if(docked && child && child->Ammo>0 && child->IsOnMap && dest(child)==CellStruct{85,71}) back=true;
    if(grizzlyShot && drone && drone->UniqueID==grizzlyId && drone->InLimbo && drone->Ammo==0)
        grizzlyDocked=true;
    if(grizzlyDocked && drone && drone->UniqueID==grizzlyId && drone->Ammo>0 && drone->IsOnMap && !drone->InLimbo)
        grizzlyRearmed=true;
    if((back && grizzlyRearmed && grizzlyArrived && GroundCheck::done && GarrisonCheck::done) || f==1400) {
        check(GarrisonCheck::done,"better garrison lifecycle");
        check(GroundCheck::done,"ground guard lifecycle");
        check(InfantryGuardCheck::done,"infantry guard lifecycle");
        check(grizzlyAirborne && grizzlyArrived,"Grizzly flew low and landed on guard cell");
        check(grizzlyShot,"Grizzly cannon fired at designated enemy");
        check(grizzlyDocked,"Grizzly returned empty to outpost");
        check(grizzlyShot && grizzlyRearmed,"Grizzly fired and rearmed");
        check(attacked,"engaged opponent");
        check(spent,"fired live ammunition");
        check(docked,"docked for reload");
        check(back,"attack and rearm");
        excavatorStart=f;
    }
    return 0;
}
