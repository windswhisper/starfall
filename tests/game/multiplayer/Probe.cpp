#include "../../../src/Starfall.cpp"
#include <MessageListClass.h>
#include <sstream>
#include <iomanip>
#include <TacticalClass.h>
#include <Surface.h>
#include "../WindowMouse.h"
EXPORT_FUNC(SF_ResearchDraw);

namespace MultiplayerTest {
std::vector<HouseClass*> players;
std::array<TechnoClass*,3> casters{},targets{};
std::array<int,3> credits{},health{};
std::array<int,3> buildingCounts{};
std::array<CellStruct,3> starts{};
int skillIndex=-1;
void check(bool ok,const char* message) {
    std::ofstream("multiplayer.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<message<<'\n';
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
unsigned long long hash(const std::string& data) {
    unsigned long long value=14695981039346656037ull;
    for(unsigned char c:data) {value^=c;value*=1099511628211ull;}
    return value;
}
void snapshot(int frame) {
    std::ostringstream native;
    const auto& random=ScenarioClass::Instance->Random;
    native<<random.Next1<<' '<<random.Next2;
    for(auto n:random.Table) native<<' '<<n;
    native<<'\n';
    for(auto h:HouseClass::Array) if(h) native<<"H "<<h->ArrayIndex<<' '<<h->Type->ID<<' '
        <<h->IsHumanPlayer<<' '<<h->Defeated<<' '<<h->Available_Money()<<' '
        <<hash(std::string(reinterpret_cast<const char*>(h->ThreatPosedEstimates),sizeof(h->ThreatPosedEstimates)))<<'\n';
    std::map<DWORD,TechnoClass*> objects;
    for(auto t:TechnoClass::Array) if(t) objects.emplace(t->UniqueID,t);
    for(auto [id,t]:objects) native<<"T "<<id<<' '<<t->GetTechnoType()->ID<<' '
        <<(t->Owner?t->Owner->ArrayIndex:-1)<<' '<<t->Health<<' '<<t->IsAlive<<' '<<t->InLimbo<<' '
        <<t->Location.X<<' '<<t->Location.Y<<' '<<t->Location.Z<<' '<<int(t->CurrentMission)<<' '<<t->Ammo<<' '
        <<t->Cloakable<<' '<<int(t->CloakState)<<' '<<t->CloakProgress.Value<<' '<<t->CloakDelayTimer.GetTimeLeft()<<'\n';
    for(auto foot:FootClass::Array) if(foot) {
        // A docked spawn has no map cell or registered threat. The engine can leave
        // the cached threat uninitialized until placement; keep checking deployed units.
        const bool docked=foot->SpawnOwner && foot->InLimbo && !foot->IsOnMap;
        native<<"F "<<foot->UniqueID<<' '<<foot->LastMapCoords.X<<' '<<foot->LastMapCoords.Y<<' '
            <<(docked?0:foot->ThreatPosed)<<' '<<foot->IsInAir()<<' '<<foot->GetTargetCoords().Z<<'\n';
    }
    for(auto manager:SpawnManagerClass::Array) if(manager && manager->Owner) {
        native<<"D "<<manager->Owner->UniqueID<<' '<<int(manager->Status);
        for(auto node:manager->SpawnedNodes) {
            auto child=node->Unit;
            native<<' '<<int(node->Status)<<' '<<(child?child->UniqueID:0)<<' '<<(child && child->Target?child->Target->UniqueID:0);
            if(child && child->Destination) {
                auto pos=child->Destination->GetCoords();
                native<<'@'<<pos.X<<','<<pos.Y;
            }
        }
        native<<'\n';
    }
    for(int i=0;i<400;++i) for(auto unit:ForceMove::trackerVector(i)) native<<"A "<<i<<' '<<unit->UniqueID<<'\n';
    IStream* stream=nullptr;
    if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))) check(false,"snapshot stream");
    // Unrefreshed offer boards are lazy local presentation state. Compare all
    // committed refresh boards and all gameplay state, without making either
    // client generate the remote player's offers as a side effect of sampling.
    const auto boards=Research::savedOffers;
    const auto seen=Research::seenOffers;
    for(auto it=Research::savedOffers.begin();it!=Research::savedOffers.end();) {
        if(!Research::refreshedSlots.contains(it->first)) it=Research::savedOffers.erase(it); else ++it;
    }
    for(auto it=Research::seenOffers.begin();it!=Research::seenOffers.end();) {
        if(!Research::refreshedSlots.contains(it->first)) it=Research::seenOffers.erase(it); else ++it;
    }
    bool saved=saveState(stream);
    Research::savedOffers=boards;Research::seenOffers=seen;
    if(!saved) check(false,"snapshot extension state");
    STATSTG stat{};stream->Stat(&stat,STATFLAG_NONAME);
    std::string extension(stat.cbSize.LowPart,'\0');
    stream->Seek({},STREAM_SEEK_SET,nullptr);stream->Read(extension.data(),stat.cbSize.LowPart,nullptr);stream->Release();
    std::ofstream("snapshots.csv",std::ios::app)<<frame<<','<<std::hex<<hash(native.str())<<','<<hash(extension)<<'\n';
    // Keep the latest full sample for diagnosing a mismatch without hashing pointers.
    std::ofstream("native-last.txt")<<native.str();
    if(frame%10==0) {
        auto name="samples/"+std::to_string(frame);
        std::ofstream(name+".txt")<<native.str();
        std::ofstream(name+".bin",std::ios::binary).write(extension.data(),extension.size());
    }
}
long long state(int role,const char* key) {
    auto it=scriptTimers.state.find({casters[role]->UniqueID,"mp_cast",key});
    return it==scriptTimers.state.end()?0:it->second.integer;
}
void send(const EventClass& event,const char* label) {
    check(EventClass::OutList.Add(event),label);
    std::ofstream("multiplayer.log",std::ios::app)<<"SENT frame="<<Unsorted::CurrentFrame<<" house="<<int(event.HouseIndex)<<" type="<<int(event.Type)<<'\n';
}
void cast(int role,bool duplicate=false) {
    auto h=players[role];
    if(h!=HouseClass::CurrentPlayer) return;
    EventClass event(h->ArrayIndex,EventType::Place,static_cast<AbstractType>(0x53460000u|skillIndex),
        casters[role]->UniqueID,targets[role]->UniqueID,targets[role]->GetMapCoords());
    send(event,"local active command queued");
    if(duplicate) send(event,"duplicate active command queued");
}
void clickCast(int role) {
    if(players[role]!=HouseClass::CurrentPlayer) return;
    ObjectClass::CurrentObjects.Clear();casters[role]->Select();
    auto tactical=TacticalClass::Instance;
    auto world=targets[role]->GetCenterCoords();
    auto screen=tactical->CoordsToScreen(world);
    auto view=DSurface::ViewBounds;
    tactical->TacticalPos={screen.X-view.Width/2,screen.Y-view.Height/2};
    SF_ResearchDraw(nullptr);
    Point2D button{view.X+25,view.Y+view.Height-30};
    check(WindowMouse::button(button,1) && WindowMouse::button(button,4),"real skill button click captured");
    auto point=tactical->CoordsToClient(world).first;
    point.X+=view.X;point.Y+=view.Y;
    auto before=EventClass::OutList.Count;
    check(WindowMouse::button(point,1) && WindowMouse::button(point,4),"real unit target click captured");
    check(EventClass::OutList.Count==before+1 && state(role,"casts")==0,"UI queues exactly one command without local execution");
}
void refresh(int role) {
    auto h=players[role];if(h!=HouseClass::CurrentPlayer) return;
    auto board=Research::offers(h->ArrayIndex,h->Type->ID);
    check(board.size()==3,"local research offers");
    EventClass event(h->ArrayIndex,EventType::SpecialPlace,-0x5347,{0,static_cast<short>(board[0])});
    send(event,"local refresh queued");send(event,"duplicate refresh queued");
}
int selectionMessages(HouseClass* house) {
    auto completed=Research::completedProjects(house->ArrayIndex);
    check(completed.size()==1,"research selected for notification test");
    auto message=std::wstring(house->UIName)+L" 已研究「"+Research::options[completed.front()].name+L"」。";
    int count=0;
    for(const auto& text:MessageListClass::Instance.MessageBuffers) if(message==text) ++count;
    return count;
}
void purchase(int role) {
    auto h=players[role];if(h!=HouseClass::CurrentPlayer) return;
    auto board=Research::offers(h->ArrayIndex,h->Type->ID);
    check(board.size()==3,"refreshed local research offers");
    EventClass event(h->ArrayIndex,EventType::SpecialPlace,-0x5346,{0x5243,static_cast<short>(board[0])});
    send(event,"local purchase queued");send(event,"duplicate purchase queued");
}
void setup() {
    CreateDirectoryA("samples",nullptr);
    Research::ensureReady();
    check(Research::sharedRaritySequence,"shared rarity enabled on both peers");
    for(auto h:HouseClass::Array) if(h && h->IsHumanPlayer && !h->IsObserver()) players.push_back(h);
    check(players.size()==Game::PlayerCount && ActiveSkills::specs.size()==1,"network fixture configuration");
    skillIndex=0;
    for(int role=0;role<static_cast<int>(players.size());++role) {
        auto h=players[role];credits[role]=h->Available_Money();
        for(auto b:BuildingClass::Array) if(b && b->Owner==h && !_stricmp(b->Type->ID,"GAPOWR")) ++buildingCounts[role];
        for(int kind=0;kind<2;++kind) {
            auto unit=static_cast<TechnoClass*>(UnitTypeClass::Find("MTNK")->CreateObject(h));
            CellStruct cell{static_cast<short>(45+role*7),static_cast<short>(73+kind*3)};
            check(unit && unit->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge(),DirType::North),"place network fixture unit");
            unit->ForceMission(Mission::Sleep);
            (kind?targets:casters)[role]=unit;
        }
        starts[role]=casters[role]->GetMapCoords();health[role]=targets[role]->Health;
        grantToUnit(casters[role],PassiveGrant{{"mp_cast","mp_random"},-1});
        MapClass::Instance.Reveal(h);
    }
    // Only this client's panel is populated. The remote client must independently
    // derive the same board when the serialized refresh/purchase arrives.
    auto local=HouseClass::CurrentPlayer;
    check(Research::offers(local->ArrayIndex,local->Type->ID).size()==3,"asymmetric local UI preparation");
}

void testPlatformTransfer(int frame) {
    constexpr CellStruct platformCell{67,67}, start{63,69}, deck{68,69};
    static BuildingClass* platform=nullptr;
    static InfantryClass* unit=nullptr;
    auto& map=MapClass::Instance;
    auto owner=players[0];
    const bool local=owner==HouseClass::CurrentPlayer;
    if(frame==21) {
        auto type=BuildingTypeClass::Find("ASHIGHPL");
        platform=type?static_cast<BuildingClass*>(type->CreateObject(owner)):nullptr;
        check(platform && platform->Unlimbo(map.GetCellAt(platformCell)->GetCoords(),DirType::North),"placed network high platform");
        unit=static_cast<InfantryClass*>(InfantryTypeClass::Find("E1")->CreateObject(owner));
        check(unit && unit->Unlimbo(map.GetCellAt(start)->GetCoords(),DirType::East),"placed network platform infantry");
    }
    if(frame==40 && local) {
        const auto action=unit->MouseOverObject(platform,true);
        check(action==Action::Enter && unit->ObjectClickedAction(action,platform,false),"owner clicked platform entrance");
    }
    if(frame==150) {
        check(unit && unit->GetMapCoords()==deck && unit->GetCoords().Z==map.GetCellAt(deck)->GetCoords().Z,
            "platform entry reached the same deck cell on both peers");
        if(local) {
            const auto action=platform->MouseOverObject(platform,true);
            check(action==Action::Self_Deploy && platform->ObjectClickedAction(action,platform,false),"owner deployed high platform");
        }
    }
    if(frame==185) {
        const auto landing=unit->GetMapCoords();
        check(landing.X==66 && landing.Y>=66 && landing.Y<=73 &&
            unit->GetCoords().Z==map.GetCellAt(landing)->GetCoords().Z && unit->GetCoords().Z<map.GetCellAt(deck)->GetCoords().Z,
            "platform Deploy reached the outside ground ring on both peers");
        unit->UnInit();
        platform->UnInit();
    }
}
}
DEFINE_HOOK(0x55B4EB,SF_TestMultiplayer,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    using namespace MultiplayerTest;
    int frame=Unsorted::CurrentFrame;
    static const bool impaired=GetFileAttributesA("impaired.flag")!=INVALID_FILE_ATTRIBUTES;
    const int ticketClick=impaired?895:845;
    const int verifyClaim=impaired?980:870;
    const int grantA=verifyClaim+5, grantB=grantA+(impaired?85:40), upgrade=grantB+(impaired?85:40);
    const int verifyUpgrade=upgrade+(impaired?85:30);
    if(frame==20) {
        check(SessionClass::IsMultiplayer() && (Game::PlayerCount==2 || Game::PlayerCount==3),"real multiplayer session");
        int humans=0;
        for(auto h:HouseClass::Array) if(h && h->IsHumanPlayer && !h->IsObserver()) ++humans;
        check(humans==Game::PlayerCount,"distinct human houses");
        std::ofstream("multiplayer.log",std::ios::app)<<"LOCAL house="<<HouseClass::CurrentPlayer->ArrayIndex
            <<" mode="<<int(SessionClass::Instance.GameMode)<<" players="<<Game::PlayerCount<<'\n';
        setup();
    }
    static const bool testDroneGuard=GetFileAttributesA("actsp-network.flag")!=INVALID_FILE_ATTRIBUTES;
    if(testDroneGuard && frame>=21 && frame<=185) testPlatformTransfer(frame);
    if(testDroneGuard && frame>=20 && frame<=685) {
        static DWORD originalGuard=0;
        CellStruct site{73,76},rally{70,74},groundSite{80,84},groundRally{84,84};
        CellStruct infantrySite{53,81},infantryRally{55,81};
        auto cell=MapClass::Instance.GetCellAt(site);
        auto groundCell=MapClass::Instance.GetCellAt(groundSite);
        auto infantryCell=MapClass::Instance.GetCellAt(infantrySite);
        if(frame==20) {
            auto b=static_cast<BuildingClass*>(BuildingTypeClass::Find("GAPILL")->CreateObject(players[0]));
            check(b && b->Unlimbo(cell->GetCoords(),DirType::North),"placed network guard outpost");
            auto type=BuildingTypeClass::Find("ASGNDPOST");
            auto post=type?static_cast<BuildingClass*>(type->CreateObject(players[0])):nullptr;
            check(post && post->Unlimbo(groundCell->GetCoords(),DirType::North),"placed network ground outpost");
            auto infantryType=BuildingTypeClass::Find("ASINFPOST");
            auto infantryPost=infantryType?static_cast<BuildingClass*>(infantryType->CreateObject(players[0])):nullptr;
            check(infantryPost && infantryPost->Unlimbo(infantryCell->GetCoords(),DirType::North),
                  "placed network infantry outpost");
        }
        auto b=cell->GetBuilding();
        auto post=groundCell->GetBuilding();
        if(frame==70 && HouseClass::CurrentPlayer==players[0]) {
            auto action=b->MouseOverCell(&rally);
            check(action==Action::Move && b->CellClickedAction(action,&rally,&rally,false),"owner clicked guard rally");
            auto groundAction=post->MouseOverCell(&groundRally);
            check(groundAction==Action::Move && post->CellClickedAction(groundAction,&groundRally,&groundRally,false),
                  "owner clicked ground rally");
            auto infantryPost=infantryCell->GetBuilding();
            const auto infantryAction=infantryPost->MouseOverCell(&infantryRally);
            check(infantryAction==Action::Move &&
                  infantryPost->CellClickedAction(infantryAction,&infantryRally,&infantryRally,false),"owner clicked infantry rally");
        }
        if(frame==180 || frame==300) {
            auto child=b->SpawnManager->SpawnedNodes[0]->Unit;
            auto point=frame==180?rally:CellStruct{71,82};
            check(b->ArchiveTarget==MapClass::Instance.GetCellAt(rally) && child && !child->InLimbo &&
                  (child->Destination==MapClass::Instance.GetCellAt(point) || child->GetMapCoords()==point),
                  frame==180?"remote rally reached guard drone":"synchronized Lua retasked guard drone");
            if(frame==180) check(grantToUnit(b,PassiveGrant{{"mp_droneguard"},-1}),"grant network Lua guard order");
            const auto groundPoint=frame==180?groundRally:CellStruct{84,88};
            int guards=0;
            for(auto u:UnitClass::Array) {
                if(u->Owner==players[0] && !_stricmp(u->Type->ID,"MTNK") && u->GetMapCoords()==groundPoint && u->GetHeight()==0) {
                    ++guards;
                    originalGuard=u->UniqueID;
                }
            }
            check(guards==1,frame==180?"remote ground rally reached by tank":"network Lua ground rally reached by tank");
            if(frame==180) check(grantToUnit(post,PassiveGrant{{"mp_droneguard"},-1}),"grant network Lua ground order");
            if(frame==180) {
                InfantryClass* infantry=nullptr;
                for(auto unit:InfantryClass::Array) if(unit->Owner==players[0] && !_stricmp(unit->Type->ID,"E1") &&
                    unit->GetMapCoords()==infantryRally && unit->GetHeight()==0) {
                    check(!infantry,"one infantry guard per network outpost");
                    infantry=unit;
                }
                check(infantry,"remote ground rally reached by infantry");
                infantry->UnInit();
                infantryCell->GetBuilding()->UnInit();
            }
            if(frame==300) {
                check(post->SetOwningHouse(players[1],false),"human player captured guard outpost");
                post->IronCurtain(450,players[1],false);
            }
        }
        if(frame==325) {
            UnitClass* old=nullptr;
            for(auto u:UnitClass::Array) if(u->UniqueID==originalGuard) old=u;
            check(old && old->Owner==players[0],"original network guard retained ownership");
            old->UnInit();
        }
        if(frame==595 || frame==685) {
            int replacements=0;
            for(auto u:UnitClass::Array) {
                auto at=u->GetMapCoords();
                if(u->Owner==players[1] && !_stricmp(u->Type->ID,"MTNK") && at.X>=78 && at.Y>=82) ++replacements;
            }
            check(post && post->Owner==players[1] && replacements==(frame==595?0:1),
                  frame==595?"captured post waits before replenishing":"new owner's post replenished ground guard");
        }
    }
    if(testDroneGuard && frame>=705 && frame<=900) {
        static BuildingClass* bunker=nullptr;
        static InfantryClass* guard=nullptr;
        static UnitClass* target=nullptr;
        static int targetHealth=0;
        constexpr CellStruct fireCell{71,67};
        if(frame==705) {
            auto type=BuildingTypeClass::Find("NABNKR");
            bunker=static_cast<BuildingClass*>(type->CreateObject(players[0]));
            check(bunker && bunker->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{70,69})->GetCoords(),DirType::North),
                  "placed network garrison bunker");
            guard=static_cast<InfantryClass*>(InfantryTypeClass::Find("E1")->CreateObject(players[0]));
            check(guard && guard->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{69,69})->GetCoords(),DirType::North),
                  "placed network garrison infantry");
        }
        if(frame==735 && HouseClass::CurrentPlayer==players[0]) {
            const auto action=guard->MouseOverObject(bunker,true);
            check(action==Action::Capture && guard->ObjectClickedAction(action,bunker,false),"owner issued garrison order");
        }
        if(frame==810) {
            check(bunker->Occupants.Count==1 && bunker->Occupants[0]==guard && !bunker->CanOccupyFire(),
                  "both peers kept the same native garrison occupant");
            target=static_cast<UnitClass*>(UnitTypeClass::Find("MTNK")->CreateObject(players[1]));
            auto targetCoords=MapClass::Instance.GetCellAt(CellStruct{76,69})->GetCoords();
            targetCoords.X-=96;
            check(target && target->Unlimbo(targetCoords,DirType::North),
                  "placed network garrison target");
            target->Deactivate();
            targetHealth=target->Health;
            if(HouseClass::CurrentPlayer==players[0]) {
                const auto action=bunker->MouseOverObject(target,true);
                check(action==Action::Attack && bunker->ObjectClickedAction(action,target,false),"owner issued garrison attack in building type bonus range");
            }
        }
        if(frame==864) {
            check(target->Health<targetHealth,"garrison attack and damage replicated on both peers");
            if(HouseClass::CurrentPlayer==players[0]) {
                auto cell=fireCell;
                check(bunker->CellClickedAction(Action::Attack,&cell,&cell,false),"owner issued garrison ground attack");
            }
        }
        if(frame==900) {
            const auto cell=abstract_cast<CellClass*>(guard->Target);
            check(cell && cell->MapCoords==fireCell,"garrison shot at the same ground cell on both peers");
        }
    }
    static const bool mountsOnly=GetFileAttributesA("mount-network.flag")!=INVALID_FILE_ATTRIBUTES;
    if(mountsOnly) {
        if(frame==30) for(size_t i=0;i<players.size();++i)
            check(grantToUnit(casters[i],PassiveGrant{{"mp_mount"},-1}),"parent received network installer");
        if(frame==80 && HouseClass::CurrentPlayer==players[0]) {
            send(EventClass(players[0]->ArrayIndex,TargetClass(casters[0]),Mission::Attack,TargetClass(targets[1]),
                TargetClass(),TargetClass()),"player attacks with mounted parent");
        }
        if(frame==170) {
            check(UnitMount::links.size()==players.size(),"one occupied slot per parent");
            for(const auto& [id,l]:UnitMount::links) {
                check(l.slot=="port" && l.child->Owner==l.parent->Owner,"network slot and owner");
                if(l.parent==casters[0]) check(l.ordered==targets[1],"synchronized explicit target");
            }
        }
        if(frame==190) for(size_t i=0;i<players.size();++i) if(players[i]==HouseClass::CurrentPlayer) {
            auto cell=MapClass::Instance.GetCellAt(CellStruct{static_cast<short>(45+i*7),80});
            send(EventClass(players[i]->ArrayIndex,TargetClass(casters[i]),Mission::Move,TargetClass(),TargetClass(cell),TargetClass()),"move mounted parent");
        }
        if(frame==370) {
            check(UnitMount::links.size()==players.size(),"all attachments retained after travel");
            for(const auto& [id,l]:UnitMount::links)
                check(!l.ordered && l.child->Location==Mount::position(l),"children follow after synchronized move");
        }
        if(frame==410) for(size_t i=0;i<players.size();++i) check(consumeTarget(casters[i]),"remove mounted parent");
        if(frame==415) check(UnitMount::links.empty(),"network attachments released");
        if(frame==430) {
            auto cell=MapClass::Instance.GetCellAt(CellStruct{50,65});
            check(BuildingSpawn::create(BuildingTypeClass::Find("GTGCAN"),players[0],cell->GetCoords(),0),"place network turret building");
            check(grantToUnit(cell->GetBuilding(),PassiveGrant{{"mp_mount"},-1}),"install building gun");
        }
        if(frame==455 && HouseClass::CurrentPlayer==players[0]) {
            auto tower=MapClass::Instance.GetCellAt(CellStruct{50,65})->GetBuilding();
            send(EventClass(players[0]->ArrayIndex,TargetClass(tower),Mission::Attack,TargetClass(targets[1]),
                TargetClass(),TargetClass()),"building attack command queued");
        }
        if(frame==530) {
            check(UnitMount::links.size()==1,"one network building mount");
            const auto& gun=UnitMount::links.begin()->second;
            check(gun.slot=="gun" && gun.parent->WhatAmI()==AbstractType::Building &&
                gun.ordered==targets[1],"building attack command replicated");
        }
        if(frame==545) {
            auto tower=MapClass::Instance.GetCellAt(CellStruct{50,65})->GetBuilding();
            check(grantToUnit(tower,PassiveGrant{{"mp_stripguns"},-1}),"network tower grants dismount action");
        }
        if(frame==560) {
            auto tower=MapClass::Instance.GetCellAt(CellStruct{50,65})->GetBuilding();
            check(tower && UnitMount::links.empty(),"network tower slot released");
            check(Mount::create(tower,skills.at("mp_mount").actions.front()),"network tower slot reused");
        }
        if(frame>=20 && frame<=620) snapshot(frame);
        if(frame==620) {check(true,"completed network run");TerminateProcess(GetCurrentProcess(),0);}
        return 0;
    }
    if(frame>20 && frame<=900) for(int role=0;role<static_cast<int>(players.size());++role) {
        bool casterFound=false,targetFound=false;
        for(auto unit:TechnoClass::Array) {casterFound|=unit==casters[role];targetFound|=unit==targets[role];}
        if(!casterFound || !targetFound) check(false,"fixture units unexpectedly destroyed");
    }
    if(frame==60) clickCast(0);
    if(frame==120) cast(1);
    if(frame==140 && players.size()==3) cast(2);
    if(frame==180) refresh(0);
    if(frame==210) check(Research::slotUsed(players[0]->ArrayIndex,0),"remote refresh applied once on both peers");
    if(frame==220) purchase(0);
    if(frame==240) for(int i=0;i<Game::PlayerCount;++i) cast(i,true);
    if(frame==320) {
        for(int role=0;role<static_cast<int>(players.size());++role) check(state(role,"casts")==2 && state(role,"pulses")==8,"both players skills and delayed actions executed exactly twice");
        for(int role=0;role<static_cast<int>(players.size());++role) {
            int count=0;for(auto b:BuildingClass::Array) if(b && b->Owner==players[role] && !_stricmp(b->Type->ID,"GAPOWR")) ++count;
            check(count==buildingCounts[role]+1,"building fallback executed once per owner on both peers");
            auto id=static_cast<DWORD>(state(role,"buildingId"));
            BuildingClass* building=nullptr;for(auto b:BuildingClass::Array) if(b && b->UniqueID==id) building=b;
            check(building && building->Owner==players[role] && !_stricmp(building->Type->ID,"GAPOWR"),"building conversion keeps script identity on both peers");
        }
        check(Research::unlocked[players[0]->ArrayIndex].size()==1 && Research::unlocked[players[1]->ArrayIndex].empty(),"research purchase owner isolation");
        check(Research::completedRounds.at(players[0]->ArrayIndex)==1 && !Research::completedRounds.contains(players[1]->ArrayIndex),"shared rarity progress is per player");
    }
    if(frame==280) {
        check(Research::announceSelection && selectionMessages(players[0])==1,"one default research announcement on each peer");
        check(WritePrivateProfileStringA("Starfall.Research","AnnounceSelection","no",configPath),"disable announcements in fixture");
        Research::readConfiguration(configPath);
        check(!Research::announceSelection,"announcement switch read");
    }
    if(frame==340) for(int i=1;i<Game::PlayerCount;++i) refresh(i);
    if(frame==380) for(int i=1;i<Game::PlayerCount;++i) purchase(i);
    if(frame==430) check(selectionMessages(players[1])==0,"disabled research stays silent on each peer");
    if(frame==430) {
        auto first=Research::completedProjects(players[0]->ArrayIndex).front();
        auto second=Research::completedProjects(players[1]->ArrayIndex).front();
        check(Research::options[first].rarity==Research::options[second].rarity,"staggered first purchases share rarity");
        // Sample next-round rarity without populating remote lazy board state.
        const auto boards=Research::savedOffers;
        const auto seen=Research::seenOffers;
        std::array<Research::Rarity,3> rarity{};
        for(int role=0;role<static_cast<int>(players.size());++role) {
            auto h=players[role];
            check(Research::completedRounds.at(h->ArrayIndex)==1,"duplicate network purchase advances only once");
            const auto& board=Research::offers(h->ArrayIndex,h->Type->ID);
            check(board.size()==3,"second shared research round available");
            rarity[role]=Research::options[board.front()].rarity;
        }
        check(std::all_of(rarity.begin(),rarity.begin()+players.size(),[&](auto tier){return tier==rarity[0];}),"second round shares rarity across players");
        Research::savedOffers=boards;Research::seenOffers=seen;Research::clearOfferCache();
    }
    if(frame==440) {
        auto local=HouseClass::CurrentPlayer;
        int role=static_cast<int>(std::find(players.begin(),players.end(),local)-players.begin());
        // Legitimate sender with another player's caster must be rejected.
        send(EventClass(local->ArrayIndex,EventType::Place,static_cast<AbstractType>(0x53460000u|skillIndex),
            casters[(role+1)%players.size()]->UniqueID,targets[role]->UniqueID,targets[role]->GetMapCoords()),"cross-owner caster rejection command queued");
    }
    if(frame==480) for(int i=0;i<Game::PlayerCount;++i) cast(i);
    if(frame==620) {
        for(int role=0;role<static_cast<int>(players.size());++role) {
            check(state(role,"casts")==3 && state(role,"pulses")==12,"duplicate and cross-owner casts rejected");
            check(players[role]->Available_Money()==credits[role]+211,"exactly three credit rewards and one research purchase");
            check(targets[role]->Health<health[role] && targets[role]->Health>0,"delayed damage reached both targets");
            check(Research::unlocked[players[role]->ArrayIndex].size()==1 && Research::balance(players[role]->ArrayIndex)>900,"research ownership and probabilistic income");
            check(!ActiveSkills::remaining(casters[role],ActiveSkills::specs[skillIndex]),"cooldown expired on both peers");
        }
        check(scriptTimers.queue.size()==players.size() && LuaAnim::entries.empty() && ForceMove::motions.empty(),"only scheduled research rewards remain");
    }
    if(frame==640) for(auto caster:casters) if(caster) grantToUnit(caster,PassiveGrant{{"mp_cloak"},120});
    if(frame==660) for(auto caster:casters) if(caster)
        check(cloakableUnits.contains(caster->UniqueID) && caster->IsCloakable() && !caster->Cloakable,"temporary cloak capability on both peers");
    if(frame==(impaired?730:680)) {
        for(int role=0;role<static_cast<int>(players.size());++role) {
            const int house=players[role]->ArrayIndex;
            const auto projects=Research::completedProjects(house);
            std::ofstream("multiplayer.log",std::ios::app)<<"REWARDS frame="<<frame<<" house="<<house<<" grants="<<state(role,"researchRewards")<<" active="<<projects.size()<<" rounds="<<Research::completedRounds.at(house)<<'\n';
            check(state(role,"researchRewards")==2 && projects.size()==3 && Research::completedRounds.at(house)==1 &&
                Research::options[projects[1]].rarity==Research::Rarity::Rare,"random research grants synchronized without advancing ordinary rounds");
            check(players[role]->Available_Money()==credits[role]+411,"two random research effects applied once");
        }
        check(scriptTimers.queue.empty(),"timers animations and forced moves retired");
    }
    if(frame==800) for(auto caster:casters) if(caster)
        check(!cloakableUnits.contains(caster->UniqueID) && !caster->IsCloakable() && !caster->Cloakable,"temporary cloak withdrawn on both peers");
    if(frame==810) {
        BuildingClass* power=nullptr;
        for(auto b:BuildingClass::Array) if(b && b->Owner==players[1] && !_stricmp(b->Type->ID,"GAPOWR")) {power=b;break;}
        auto spy=static_cast<InfantryClass*>(InfantryTypeClass::Find("SPY")->CreateObject(players[0]));
        auto cell=MapClass::Instance.GetCellAt(CellStruct{46,69});
        check(power && spy && spy->Unlimbo(cell->GetCoordsWithBridge(),DirType::North),"placed network building reward spy");
        spy->ForceMission(Mission::Sleep);
        BuildingResearch::authorizeBuildingReward(spy,power,Research::BuildingRewardSource::Spy);
        if(HouseClass::CurrentPlayer==players[0]) {
            auto board=Research::buildingOptions(power->UniqueID,players[0]->ArrayIndex);
            check(board.size()==3,"local weighted free board");
            int id=power->UniqueID;
            EventClass claim(players[0]->ArrayIndex,EventType::SpecialPlace,-0x100000-board.back(),
                CellStruct{static_cast<short>(id),static_cast<short>(static_cast<unsigned>(id)>>16)});
            send(claim,"weighted free choice queued");send(claim,"duplicate free choice queued");
        }
    }
    if(frame==810) {
        auto h=players[1];
        check(Research::grantFree(h->ArrayIndex,h->Type->ID,{},Research::Rarity::Rare),"granted one network research choice");
        if(HouseClass::CurrentPlayer==players[0]) {
            const auto id=Research::lastTicket;
            auto board=Research::ticketOptions(id,h->ArrayIndex,h->Type->ID);
            send(EventClass(players[0]->ArrayIndex,EventType::SpecialPlace,-0x110000-board.front(),
                CellStruct{static_cast<short>(id),static_cast<short>(id>>16)}),"other house attempts ticket claim");
        }
    }
    if(frame==ticketClick) {
        auto h=players[1];
        check(Research::freeTickets.size()==1 && Research::completedProjects(h->ArrayIndex).size()==3,"other house cannot consume ticket");
        if(HouseClass::CurrentPlayer==h) {
            const auto id=Research::lastTicket;
            const auto& view=DSurface::ViewBounds;
            const Point2D tab{view.X+view.Width-32,view.Y+48};
            SF_ResearchDraw(nullptr);
            check(WindowMouse::button(tab,1) && WindowMouse::button(tab,4) &&
                Research::rewardPanel==(Research::ticketFlag|id),"research tab opens action reward");
            const auto rows=Research::freeOptions(Research::rewardPanel,h->ArrayIndex);
            check(rows.size()==3,"three rare cards in action reward");
            const Point2D middle{view.X+view.Width/2,view.Y+view.Height/2};
            auto before=EventClass::OutList.Count;
            check(WindowMouse::button(middle,1) && WindowMouse::button(middle,4) && EventClass::OutList.Count==before+1,
                "ticket card click queues exactly one command");
            send(EventClass(h->ArrayIndex,EventType::SpecialPlace,-0x110000-rows[1],
                CellStruct{static_cast<short>(id),static_cast<short>(id>>16)}),"repeat ticket claim");
        }
    }
    if(frame==verifyClaim) {
        check(Research::completedProjects(players[0]->ArrayIndex).size()==4 &&
            Research::completedProjects(players[1]->ArrayIndex).size()==4 && Research::freeTickets.empty() &&
            Research::completedRounds.at(players[1]->ArrayIndex)==1 && Research::completedRounds.at(players[0]->ArrayIndex)==1 &&
            Research::pendingBuildingRewards(players[0]->ArrayIndex).empty(),"weighted building claim commits once on both peers");
        check(players[1]->Available_Money()==credits[1]+511,"ticket click and duplicate grant one project on both peers");
    }
    if(frame==700) for(int role=0;role<static_cast<int>(players.size());++role) if(players[role]==HouseClass::CurrentPlayer) {
        auto cell=MapClass::Instance.GetCellAt(CellStruct{static_cast<short>(45+role*7),80});
        send(EventClass(players[role]->ArrayIndex,TargetClass(casters[role]),Mission::Move,TargetClass(),TargetClass(cell),TargetClass()),"native movement queued after skills");
    }
    if(frame==900) for(int role=0;role<static_cast<int>(players.size());++role) check(casters[role]->GetMapCoords()!=starts[role],"native movement resumed after skill work");
    if(frame==100 && GetFileAttributesA("diverge.flag")!=INVALID_FILE_ATTRIBUTES) ++targets[0]->Health;
    if(players.size()==3) {
        auto h=players[2];
        if(frame==grantA || frame==grantB || frame==upgrade) {
            const char* id=frame==grantA?"neta":frame==grantB?"netb":"netupgrade";
            if(!Research::owns(h->ArrayIndex,id)) {
                check(Research::grantFree(h->ArrayIndex,h->Type->ID,{id},{}),"third house scoped choice available");
                if(HouseClass::CurrentPlayer==h) {
                    const auto ticket=Research::lastTicket;
                    auto rows=Research::ticketOptions(ticket,h->ArrayIndex,h->Type->ID);
                    check(rows.size()==1,"third house exact scoped choice");
                    EventClass claim(h->ArrayIndex,EventType::SpecialPlace,-0x110000-rows.front(),
                        CellStruct{static_cast<short>(ticket),static_cast<short>(ticket>>16)});
                    send(claim,"third house choice queued");send(claim,"repeat third house choice queued");
                }
            }
        }
        if(frame==verifyUpgrade) {
            auto house=h->ArrayIndex;
            check(Research::owns(house,"netupgrade") && Research::retired.at(house)==std::set<string>{"neta","netb"} &&
                !Research::owns(house,"neta") && !Research::owns(house,"netb") && Research::freeTickets.empty(),
                "third house retires both projects once");
            check(!Research::meetsPrerequisites(house,19) && !Research::canOfferFree(house,h->Type->ID,{"netalternative","neta","netb"},{},-1) &&
                !Research::retired.contains(players[0]->ArrayIndex) && !Research::retired.contains(players[1]->ArrayIndex),
                "upgrade exclusions and retired cards isolated to third house");
        }
    }
    const int end=Game::PlayerCount==3?verifyUpgrade:std::max(900,verifyClaim);
    if(players.size()==3 && frame==end-(impaired?105:55)) {
        for(auto caster:casters) check(grantToUnit(caster,PassiveGrant{{"mp_mount"},-1}),"grant network mount");
    }
    if(players.size()==3 && frame==end-(impaired?95:45) && players[0]==HouseClass::CurrentPlayer)
        send(EventClass(players[0]->ArrayIndex,TargetClass(casters[0]),Mission::Attack,TargetClass(targets[1]),
            TargetClass(),TargetClass()),"native parent attack queued");
    if(players.size()==3 && frame==end-8) {
        check(UnitMount::links.size()==3,"each peer installed three independent mounts");
        for(const auto& [id,l]:UnitMount::links) {
            if(l.parent==casters[0]) check(l.ordered==targets[1],"replicated parent attack order");
            check(l.slot=="port" && l.child->Owner==l.parent->Owner &&
                !Mount::create(l.parent,skills.at("mp_mount").actions.front()),"network mount owns its slot");
        }
    }
    if(frame==end-6) {
        for(size_t role=0;role<players.size();++role) {
            auto unit=static_cast<UnitClass*>(casters[role]);
            auto point=unit->GetMapCoords();point.X+=2;
            grantToUnit(unit,PassiveGrant{{"mp_moveorder"},-1});tickGains();dispatch();
            check(unit->Destination==MapClass::Instance.GetCellAt(point),"Lua order applied to local and remote houses");
        }
    }
    if(frame>=20 && frame<=end) snapshot(frame);
    if(frame==end) {check(true,"completed network run");TerminateProcess(GetCurrentProcess(),0);}
    return 0;
}
