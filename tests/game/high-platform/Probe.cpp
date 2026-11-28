#include "../../../src/Starfall.cpp"
#include <DisplayClass.h>
#include <Drawing.h>
#include <EventClass.h>
#include <IsometricTileTypeClass.h>
#include <HouseClass.h>
#include <MapClass.h>
#include <RulesClass.h>
#include <ScenarioClass.h>
#include <Surface.h>
#include <TacticalClass.h>
#include <UnitClass.h>
#include <WeaponTypeClass.h>
#include <algorithm>
#include <fstream>

namespace {
constexpr CellStruct anchor{67,67}, edge{68,68}, buildingSite{69,70}, tankSite{68,69}, destination{71,69};
constexpr CellStruct groundTankSite{66,69}, upCliffDestination{70,69}, downCliffDestination{73,69};
int originalLevel=0, highZ=0;
bool sawFall=false, verifiedFall=false;
DWORD tankId=0;
TargetClass groundTank;
TargetClass wallTarget,wallAttacker;
int platformHealthBeforeAttack=0, wallAttackFrame=0;
CellStruct wallTargetSite{};
int transferFrame=0;
TargetClass entryTank, entryInfantry, rejectedTank;
std::vector<TargetClass> deckBlockers;

void check(bool condition,const char* message) {
    std::ofstream("platform-production.log",std::ios::app)<<(condition?"PASS ":"FAIL ")<<message<<'\n';
    if(!condition) TerminateProcess(GetCurrentProcess(),2);
}

UnitClass* tankOnDeck() {
    for(auto tank:UnitClass::Array)
        if(tank && tank->Owner==HouseClass::CurrentPlayer &&
           (tankId ? tank->UniqueID==tankId : tank->GetMapCoords()==destination && !_stricmp(tank->Type->ID,"MTNK")))
            return tank;
    return nullptr;
}

void moveTank(UnitClass* tank,CellStruct target) {
    tank->SetDestination(MapClass::Instance.GetCellAt(target),true);
    tank->QueueMission(Mission::Move,true);
    tank->NextMission();
}

ObjectClass* pick(Point2D point,CellStruct& cell) {
    ObjectClass* target=nullptr;
    CoordStruct coords{};
    BYTE a=0,b=0;
    return DisplayClass::Instance.ProcessClickCoords(&point,&cell,&coords,&target,&a,&b)?target:nullptr;
}

Point2D projectedDeckCenter(CellStruct controller) {
    const CellStruct middle{static_cast<short>(controller.X+3),static_cast<short>(controller.Y+3)};
    auto center=MapClass::Instance.GetCellAt(middle)->GetCoords();
    center.X-=Unsorted::LeptonsPerCell/2;
    center.Y-=Unsorted::LeptonsPerCell/2;
    return TacticalClass::Instance->CoordsToClient(center).first;
}

HouseClass* enemyOf(HouseClass* house) {
    for(auto candidate:HouseClass::Array) {
        if(candidate && candidate!=house && !candidate->Type->MultiplayPassive && !house->IsAlliedWith(candidate))
            return candidate;
    }
    return nullptr;
}

FootClass* placeTransferUnit(TechnoTypeClass* type, HouseClass* house, CellStruct cell) {
    auto unit=static_cast<FootClass*>(type->CreateObject(house));
    check(unit && unit->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoords(),DirType::East),"placed transfer fixture unit");
    return unit;
}

void testSeaPlatform(int frame,bool load,HouseClass* house) {
    if(frame!=48 && frame!=100 && frame!=140 && frame!=142) return;
    auto& map=MapClass::Instance;
    CellStruct anchor{54,76},tankSite{55,77},tankGoal{58,77},turretSite{57,79};
    auto base=map.GetCellAt(anchor);
    auto deck=map.GetCellAt(tankSite);
    auto type=BuildingTypeClass::Find("ASHIGHPL");
    auto turret=BuildingTypeClass::Find("GAPILL");
    if(!load && frame==48) {
        auto seaOnly=BuildingTypeClass::Find("ASSEAPL");
        auto disabled=BuildingTypeClass::Find("ASNOPL");
        check(type->CanPlaceHere(&anchor,house) && !seaOnly->CanPlaceHere(&anchor,house) &&
              !disabled->CanPlaceHere(&anchor,house),"land placement follows AllowLand independently of AllowWater");
        int water=0;
        for(;water<IsometricTileTypeClass::Array.Count;++water) {
            base->IsoTileTypeIndex=water;
            base->Height=0;
            if(base->Tile_Is_Water()) break;
        }
        check(water<IsometricTileTypeClass::Array.Count,"found native seawater tile");
        for(short y=0;y<6;++y) for(short x=0;x<8;++x) {
            auto cell=map.GetCellAt(CellStruct{short(anchor.X+x),short(anchor.Y+y)});
            cell->IsoTileTypeIndex=water;
            cell->Height=0;
            cell->RecalcAttributes(-1);
        }
        deck->IsoTileTypeIndex=0xFFFF;deck->RecalcAttributes(-1);
        check(!type->CanPlaceHere(&anchor,house) && !DisplayClass::Instance.PassesProximityCheck(
              type,house->ArrayIndex,type->GetFoundationData(false),&anchor),"mixed interior fails placement and preview");
        deck->IsoTileTypeIndex=water;deck->RecalcAttributes(-1);
        check(!turret->CanPlaceHere(&anchor,house),"ordinary land building rejects the water");
        check(!BuildingTypeClass::Find("ASRECTPL")->CanPlaceHere(&anchor,house),"land-only platform rejects the water");
        auto edge=map.GetCellAt(CellStruct{short(anchor.X+5),short(anchor.Y+4)});
        edge->IsoTileTypeIndex=0xFFFF;
        edge->RecalcAttributes(-1);
        check(!type->CanPlaceHere(&anchor,house),"mixed perimeter rejects the platform");
        edge->IsoTileTypeIndex=water;
        edge->RecalcAttributes(-1);
        check(type->CanPlaceHere(&anchor,house),"pure sea accepts platform placement");
        check(seaOnly->CanPlaceHere(&anchor,house) && !disabled->CanPlaceHere(&anchor,house),
              "sea-only platform accepts water and both flags off reject it");
        CellStruct shore{short(anchor.X-3),anchor.Y};
        auto powerType=BuildingTypeClass::Find("GAPOWR");
        auto power=static_cast<BuildingClass*>(powerType->CreateObject(house));
        check(power && power->Unlimbo(map.GetCellAt(shore)->GetCoords(),DirType::North),"placed friendly base on shore");
        check(DisplayClass::Instance.PassesProximityCheck(type,house->ArrayIndex,type->GetFoundationData(false),&anchor),
              "sea platform preview accepts water within build radius");
        auto post=static_cast<BuildingClass*>(type->CreateObject(house));
        check(post && post->Unlimbo(base->GetCoords(),DirType::North),"built sea platform");
        deck->RecalcAttributes(-1);
        check(deck->LandType==LandType::Clear && deck->Level==base->Level+4 &&
              base->Tile_Is_Water() && turret->CanPlaceHere(&turretSite,house),"sea deck supports land construction");
        auto tower=static_cast<BuildingClass*>(turret->CreateObject(house));
        check(tower && tower->Unlimbo(map.GetCellAt(turretSite)->GetCoords(),DirType::North),"placed land building on sea deck");
        auto tank=static_cast<UnitClass*>(placeTransferUnit(UnitTypeClass::Find("MTNK"),house,tankSite));
        moveTank(tank,tankGoal);
    }
    if(load && frame==140) {
        check(deck->LandType==LandType::Clear && base->Tile_Is_Water() &&
              map.GetCellAt(turretSite)->GetBuilding(),"sea deck and land building survive load");
    }
    if(!load && frame==100) {
        bool moved=false;
        for(auto unit:UnitClass::Array) if(unit && unit->GetMapCoords()==tankGoal) moved=true;
        check(moved,"tank drives across the sea deck");
    }
    if(frame==140) {
        auto post=base->GetBuilding();
        check(post!=nullptr,"sea platform present before removal");
        post->UnInit();
    }
    if(frame==142) {
        check(deck->LandType==LandType::Water && deck->Level==base->Level &&
              deck->IsoTileTypeIndex==base->IsoTileTypeIndex && deck->Height==base->Height &&
              !map.GetCellAt(turretSite)->GetBuilding() && !turret->CanPlaceHere(&tankSite,house),
              "removing sea platform restores original water tiles and clears deck buildings");
    }
}

void testRectangle(int frame,bool load,HouseClass* house) {
    static CellStruct site{};
    static TargetClass support,infantry;
    static bool exitedAtFarEdge=false;
    auto& map=MapClass::Instance;
    if(!load && frame==84) {
        auto type=BuildingTypeClass::Find("ASRECTPL");
        check(type && type->GetFoundationWidth()==8 && type->GetFoundationHeight(true)==5,
            "loaded rectangular 8x5 foundation from art");
        for(short y=75;y<=83 && site==CellStruct{};++y) for(short x=73;x<=81;++x) {
            CellStruct candidate{x,y};
            if(!type->CanPlaceHere(&candidate,house)) continue;
            site=candidate;
            break;
        }
        check(site!=CellStruct{},"found level space for rectangle");
        auto post=static_cast<BuildingClass*>(type->CreateObject(house));
        check(post && post->Unlimbo(map.GetCellAt(site)->GetCoords(),DirType::North),"built rectangular high platform");
        support=TargetClass(post);
    }
    if(load && frame==132) {
        for(auto post:BuildingClass::Array) if(post && !_stricmp(post->Type->ID,"ASRECTPL")) {
            support=TargetClass(post);
            site=post->GetMapCoords();
            break;
        }
        check(site!=CellStruct{},"restored rectangular platform building");
    }
    if(site==CellStruct{}) return;
    const CellStruct last{static_cast<short>(site.X+6),static_cast<short>(site.Y+2)};
    const CellStruct rim{static_cast<short>(site.X+7),last.Y};
    const CellStruct outside{static_cast<short>(site.X+8),last.Y};
    auto post=support.As_Building();
    const auto floor=map.GetCellAt(last);
    if((!load && frame==85) || (load && frame==132)) {
        const auto edgeCell=map.GetCellAt(rim);
        const auto ground=map.GetCellAt(outside);
        check(post && floor->Level==ground->Level+4 && !floor->GetBuilding() &&
              edgeCell->Level==ground->Level && edgeCell->GetBuilding()==post &&
              !edgeCell->IsClearToMove(SpeedType::Track,true,true,-1,MovementZone::Normal,-1,false),
              load?"rectangular deck and rim survive save":"rectangle raises its full 6x3 deck and leaves its outer rim blocked");
        if(!load) {
            auto tankType=UnitTypeClass::Find("MTNK");
            const CellStruct behind{static_cast<short>(site.X-1),static_cast<short>(site.Y+4)};
            auto rear=placeTransferUnit(tankType,house,behind);
            auto front=placeTransferUnit(tankType,house,outside);
            check(rear->GetYSort()<post->GetYSort() && post->GetYSort()<front->GetYSort(),
                "rectangular platform draws over rear units and behind foreground units");
            rear->UnInit();
            front->UnInit();
            auto unit=placeTransferUnit(InfantryTypeClass::Find("E1"),house,outside);
            infantry=TargetClass(unit);
            const auto action=unit->MouseOverObject(post,true);
            check(action==Action::Enter && unit->ObjectClickedAction(action,post,false),
                "ordered entry from the far edge of the rectangle");
        } else {
            auto occupant=abstract_cast<InfantryClass*>(floor->GetContent());
            check(occupant && occupant->Owner==house,"saved infantry remains on the rectangle's far deck edge");
            int damage=post->Health;
            post->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,nullptr,true,false,nullptr);
        }
    }
    if(!load && frame==103) {
        auto unit=infantry.As_Infantry();
        check(unit && unit->GetMapCoords()==last && unit->GetCoords().Z==floor->GetCoords().Z,
            "infantry boards a deck cell beyond the old platform size");
        unit->ForceMission(Mission::Sleep);
    }
    if(!load && frame==135) {
        check(post && post->ObjectClickedAction(Action::Self_Deploy,post,false),"ordered rectangular platform deployment");
    }
    if(!load && frame>135) {
        auto unit=infantry.As_Infantry();
        if(unit) {
            const auto at=unit->GetMapCoords();
            if(at.X==outside.X && at.Y>=site.Y-1 && at.Y<=site.Y+5 &&
               unit->GetCoords().Z==map.GetCellAt(at)->GetCoords().Z) exitedAtFarEdge=true;
        }
        if(frame==151) {
            check(exitedAtFarEdge && unit && post,"deployment reaches the rectangle's far ground edge");
            unit->UnInit();
            post->UnInit();
        }
    }
    if(load && frame==146) {
        check(floor->Level==map.GetCellAt(outside)->Level &&
              map.GetCellAt(rim)->IsClearToMove(SpeedType::Track,true,true,-1,MovementZone::Normal,-1,false),
            "collapse restores terrain and pathing across the rectangular footprint");
    }
}

void testTransfers(int elapsed,BuildingClass* platform,HouseClass* house) {
    auto& map=MapClass::Instance;
    auto tankType=UnitTypeClass::Find("MTNK");
    if(elapsed==0) {
        const auto type=BuildingTypeClass::Find("ASHIGHPL");
        platform=static_cast<BuildingClass*>(type->CreateObject(house));
        check(platform && platform->Unlimbo(map.GetCellAt(anchor)->GetCoords(),DirType::North),"rebuilt transfer platform");
        for(short y=edge.Y;y<edge.Y+4;++y) for(short x=edge.X;x<edge.X+4;++x) {
            auto blocker=placeTransferUnit(tankType,house,{x,y});
            blocker->ForceMission(Mission::Sleep);
            deckBlockers.emplace_back(blocker);
        }
    }
    if(elapsed==1) {
        auto tank=placeTransferUnit(tankType,house,{64,69});
        auto infantry=placeTransferUnit(InfantryTypeClass::Find("E1"),house,{63,71});
        auto rejectedType=UnitTypeClass::Find("HTNK");
        CCINIClass::INI_Rules->WriteBool(rejectedType->ID,"HighPlatform.Enter",false);
        auto rejected=placeTransferUnit(rejectedType,house,{66,66});
        entryTank=TargetClass(tank);
        entryInfantry=TargetClass(infantry);
        rejectedTank=TargetClass(rejected);
        tank->Health-=40;
        check(tank->MouseOverObject(platform)==Action::Enter && infantry->MouseOverObject(platform)==Action::Enter,
            "ground vehicle and infantry show the native Enter action");
        check(rejected->MouseOverObject(platform)==Action::NoEnter,"unit INI switch gives the unavailable entry cursor");
        check(!rejected->ObjectClickedAction(Action::Enter,platform,false),"unit INI switch disables entry");
        check(tank->ObjectClickedAction(Action::Enter,platform,false) &&
            infantry->ObjectClickedAction(Action::Enter,platform,false),"issued native entry orders for both units");
    }
    if(elapsed==90) {
        const auto tank=entryTank.As_Foot();
        const auto infantry=entryInfantry.As_Foot();
        const auto atEntrance=[](const FootClass* unit) {
            if(!unit) return false;
            const auto cell=unit->GetMapCoords();
            return unit->GetCoords().Z<highZ && cell.X>=66 && cell.X<=73 && cell.Y>=66 && cell.Y<=73 &&
                (cell.X==66 || cell.X==73 || cell.Y==66 || cell.Y==73);
        };
        if(!atEntrance(tank) || !atEntrance(infantry)) {
            std::ofstream out("platform-production.log",std::ios::app);
            for(const auto unit:{tank,infantry}) {
                if(!unit) out<<"Transfer fixture unit is absent\n";
                else {
                    const auto at=unit->GetMapCoords();
                    out<<"Transfer fixture unit at "<<at.X<<','<<at.Y<<','<<unit->GetCoords().Z
                        <<" with mission "<<int(unit->CurrentMission)<<'\n';
                }
            }
        }
        check(atEntrance(tank) && atEntrance(infantry),"both units walk to the outside of the full deck and wait");
        check(infantry->ClickedEvent(EventType::Idle),"issued native Stop for waiting infantry");
    }
    if(elapsed==100) {
        for(auto& blocker:deckBlockers) blocker.As_Foot()->UnInit();
        deckBlockers.clear();
    }
    if(elapsed==112) {
        const auto tank=entryTank.As_Foot();
        const auto infantry=entryInfantry.As_Foot();
        check(tank && tank->GetCoords().Z==highZ && tank->Health==tankType->Strength-40,
            "vehicle boards when deck space opens with its original identity and health");
        check(infantry && infantry->GetCoords().Z<highZ,"Stop cancels a waiting entry order");
        check(infantry->ObjectClickedAction(Action::Enter,platform,false),"reissued the infantry entry order");
    }
    if(elapsed==126) {
        const auto infantry=entryInfantry.As_Foot();
        check(infantry && infantry->GetCoords().Z==highZ,"infantry boards using a free deck cell");
        check(platform->MouseOverObject(platform)==Action::Self_Deploy &&
            platform->ObjectClickedAction(Action::Self_Deploy,platform,false),"issued native platform Deploy by clicking");
    }
    if(elapsed==140) {
        const auto tank=entryTank.As_Foot();
        const auto infantry=entryInfantry.As_Foot();
        check(tank && infantry && tank->GetCoords().Z<highZ && infantry->GetCoords().Z<highZ &&
            tank->GetMapCoords()!=infantry->GetMapCoords(),"Deploy sends both units to distinct outside cells");
        check(infantry->ObjectClickedAction(Action::Enter,platform,false),"boarded again for the deploy hotkey");
    }
    if(elapsed==150) {
        const auto infantry=entryInfantry.As_Foot();
        check(infantry && infantry->GetCoords().Z==highZ && platform->Select(),"selected occupied platform for the deploy hotkey");
        // The native Deploy command calls this routine for the current selection.
        reinterpret_cast<void(__cdecl*)()>(0x730AF0)();
    }
    if(elapsed==165) {
        auto infantry=entryInfantry.As_Foot();
        check(infantry && infantry->GetCoords().Z<highZ,"native Deploy hotkey brings down deck units");
        if(auto rejected=rejectedTank.As_Foot()) rejected->UnInit();
        if(auto tank=entryTank.As_Foot()) tank->UnInit();
        infantry->UnInit();
        platform->UnInit();
        TerminateProcess(GetCurrentProcess(),0);
    }
}

void rebuildEnemyPlatform(HouseClass* house) {
    auto enemy=enemyOf(house);
    check(enemy!=nullptr,"enemy house available for wall attack");
    auto type=BuildingTypeClass::Find("ASHIGHPL");
    auto& map=MapClass::Instance;
    for(auto at:{anchor,CellStruct{67,69},CellStruct{70,67},CellStruct{69,70}}) {
        if(!type->CanPlaceHere(&at,enemy)) continue;
        auto building=static_cast<BuildingClass*>(type->CreateObject(enemy));
        check(building && building->Unlimbo(map.GetCellAt(at)->GetCoords(),DirType::North),"placed enemy high platform");
        wallTarget=TargetClass(building);
        wallTargetSite=at;
        return;
    }
    check(false,"clear site for enemy high platform");
}

void checkCliffWeapons(UnitClass* tank,BuildingClass* upper,HouseClass* house) {
    auto enemy=enemyOf(house);
    check(enemy!=nullptr,"enemy house available for cliff attack");
    auto& map=MapClass::Instance;
    auto rifle=static_cast<InfantryClass*>(InfantryTypeClass::Find("E2")->CreateObject(enemy));
    auto cannon=static_cast<UnitClass*>(UnitTypeClass::Find("MTNK")->CreateObject(enemy));
    check(rifle && rifle->Unlimbo(map.GetCellAt(CellStruct{66,68})->GetCoords(),DirType::East) &&
          cannon && cannon->Unlimbo(map.GetCellAt(CellStruct{66,70})->GetCoords(),DirType::East),
        "placed infantry and tank below the high platform");
    auto shell=cannon->GetWeapon(0)->WeaponType->Projectile;
    check(rifle->GetWeapon(0)->WeaponType->Projectile->SubjectToCliffs && shell->SubjectToCliffs,
        "standard rifle and tank cannon are subject to cliffs");

    check(rifle->GetFireError(tank,0,true)==FireError::ILLEGAL &&
          cannon->GetFireError(tank,0,true)==FireError::ILLEGAL &&
          cannon->GetFireError(upper,0,true)==FireError::ILLEGAL,
        "lower rifle and cannon cannot target deck occupants");
    auto aim=tank->GetCell();
    check(cannon->GetFireError(aim,0,true)==FireError::ILLEGAL &&
          !cannon->Fire(aim,0) && !cannon->Fire(tank,0) && !rifle->Fire(tank,0),
        "direct firing and force-fire cannot bypass the high platform cliff");
    auto coords=CoordStruct::Empty;
    int score=0;
    check(!cannon->CanAutoTargetObject(ThreatType::Range,1<<int(AbstractType::Unit),-1,tank,&score,UINT_MAX,&coords),
        "lower tank excludes deck occupants from native auto targeting");

    shell->SubjectToCliffs=false;
    const auto exempt=cannon->GetFireError(tank,0,true);
    shell->SubjectToCliffs=true;
    check(exempt!=FireError::ILLEGAL && tank->GetFireError(cannon,0,true)!=FireError::ILLEGAL,
        "cliff-exempt projectiles and downhill firing are allowed");
    rifle->UnInit();
    cannon->UnInit();
}

void capture(const CoordStruct& center) {
    auto surface=DSurface::Composite;
    const auto [point,visible]=TacticalClass::Instance->CoordsToClient(center);
    if(!visible || !surface || surface->GetWidth()<360 || surface->GetHeight()<280) return;
    constexpr int width=360,height=280;
    const int x=std::clamp(point.X-width/2,0,surface->GetWidth()-width);
    const int y=std::clamp(point.Y-100,0,surface->GetHeight()-height);
    auto pixels=static_cast<const BYTE*>(surface->Lock(x,y));
    if(!pixels) return;
    std::ofstream out("high-platform.ppm",std::ios::binary);
    out<<"P6\n"<<width<<" "<<height<<"\n255\n";
    for(int row=0;row<height;++row) {
        auto data=reinterpret_cast<const WORD*>(pixels+row*surface->GetPitch());
        for(int column=0;column<width;++column) {
            const RGBClass color(data[column],true);
            out.put(color.Red); out.put(color.Green); out.put(color.Blue);
        }
    }
    surface->Unlock();
}
}

DEFINE_HOOK(0x55B4EB,PlatformProductionProbe,5) {
    auto house=HouseClass::CurrentPlayer;
    if(!house) return 0;
    auto& map=MapClass::Instance;
    const int f=Unsorted::CurrentFrame;
    const bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    testSeaPlatform(f,load,house);
    if(f>=84 && f<=151) testRectangle(f,load,house);
    auto base=map.GetCellAt(anchor);
    auto raised=map.GetCellAt(edge);
    auto platform=base->GetBuilding();
    const CellStruct rimSite{67,69};
    auto rim=map.GetCellAt(rimSite);
    auto bunkerType=BuildingTypeClass::Find("GAPILL");
    if(transferFrame) {
        const int elapsed=f-transferFrame;
        if(elapsed>=0) testTransfers(elapsed,platform,house);
        return 0;
    }
    if(wallAttackFrame) {
        auto target=wallTarget.As_Building();
        if(f==wallAttackFrame+16) {
            const CellStruct attackSite{static_cast<short>(wallTargetSite.X+8),static_cast<short>(wallTargetSite.Y+5)};
            const auto source=map.GetCellAt(attackSite)->GetCoords();
            auto attacker=static_cast<UnitClass*>(UnitTypeClass::Find("MTNK")->CreateObject(house));
            check(attacker && attacker->Unlimbo(source,DirType::NorthWest),"placed wall attack tank");
            wallAttacker=TargetClass(attacker);
            const auto center=projectedDeckCenter(wallTargetSite);
            CellStruct cell;
            bool pickedWall=false;
            for(const int offset:{65,50,80,35,95}) {
                if(target && pick({center.X+offset,center.Y+115-offset/2},cell)==target) {
                    pickedWall=true;
                    break;
                }
            }
            check(pickedWall && attacker->MouseOverObject(target)==Action::Attack,
                "enemy wall offers the native attack action");
            platformHealthBeforeAttack=target->Health;
            check(attacker->ObjectClickedAction(Action::Attack,target,false),"issued native attack by clicking the wall");
        }
        if(platformHealthBeforeAttack && target && target->Health<platformHealthBeforeAttack) {
            check(true,"attacking the wall damages the high platform");
            if(auto attacker=wallAttacker.As_Unit()) attacker->UnInit();
            target->UnInit();
            transferFrame=f+40;
            wallAttackFrame=0;
            return 0;
        }
        if(f==wallAttackFrame+60) {
            auto attacker=wallAttacker.As_Unit();
            check(attacker && target,"wall attack fixtures remain alive");
            const auto aim=target->GetTargetCoords();
            const auto wallCell=map.GetCellAt(CellClass::Coord2Cell(aim));
            check(attacker->GetFireError(target,0,false)!=FireError::ILLEGAL &&
                attacker->GetFireError(wallCell,0,true)!=FireError::ILLEGAL,
                "wall and rim remain legal targets for a cannon below the cliff");
        }
        if(f>wallAttackFrame+240) {
            const auto attacker=wallAttacker.As_Unit();
            if(target && attacker) {
                const auto aim=target->GetTargetCoords();
                const auto source=attacker->GetCoords();
                std::ofstream("platform-production.log",std::ios::app)<<"Wall attack source "<<source.X<<','<<source.Y<<','<<source.Z
                    <<" aim "<<aim.X<<','<<aim.Y<<','<<aim.Z<<" mission "<<int(attacker->GetCurrentMission())
                    <<" target "<<(attacker->Target==target)<<" fire error "<<int(attacker->GetFireError(target,0,false))<<'\n';
            }
            check(false,"timed out waiting for the wall attack to damage the high platform");
        }
        return 0;
    }
    if(!load && f==30) {
        originalLevel=base->Level;
        auto platformType=BuildingTypeClass::Find("ASHIGHPL");
        auto anchorCell=anchor;
        check(platformType && platformType->CanPlaceHere(&anchorCell,house),"flat platform is placeable");
        const auto originalLand=raised->LandType;
        auto& roughBuildable=GroundType::Array[static_cast<int>(LandType::Rough)].Buildable;
        const bool originalBuildable=roughBuildable;
        raised->LandType=LandType::Rough;
        roughBuildable=true;
        check(platformType->CanPlaceHere(&anchorCell,house),"platform accepts buildable rough ground within the deck");
        roughBuildable=false;
        check(!platformType->CanPlaceHere(&anchorCell,house),"unbuildable ground inside the hollow foundation rejects placement");
        roughBuildable=originalBuildable;
        raised->LandType=originalLand;
        auto tankType=UnitTypeClass::Find("MTNK");
        auto blocker=static_cast<UnitClass*>(tankType->CreateObject(house));
        check(blocker->Unlimbo(raised->GetCoords(),DirType::East),"placed temporary blocker");
        check(!platformType->CanPlaceHere(&anchorCell,house),"occupied deck prevents platform construction");
        check(!DisplayClass::Instance.PassesProximityCheck(platformType,house->ArrayIndex,
            platformType->GetFoundationData(false),&anchorCell),"placement preview rejects an occupied deck");
        blocker->UnInit();
        check(platformType->CanPlaceHere(&anchorCell,house),"vacated footprint becomes constructible");
        platform=static_cast<BuildingClass*>(platformType->CreateObject(house));
        check(platform->Unlimbo(base->GetCoords(),DirType::North),"placed platform controller");
        check(raised->Level==originalLevel+4,"platform raises the native terrain by four levels");
        const auto image=platform->Type->Image;
        const auto sprite=image?image->GetData():nullptr;
        check(sprite && sprite->Frames==4,"platform uses the native building SHP");
        const auto normalShadow=image->GetFrameBounds(2);
        const auto damagedShadow=image->GetFrameBounds(3);
        check(normalShadow.Width>0 && normalShadow.Height>0 && damagedShadow.Width==normalShadow.Width &&
            damagedShadow.Height==normalShadow.Height && damagedShadow.X==normalShadow.X && damagedShadow.Y==normalShadow.Y,
            "platform SHP has matching ground shadows for both body frames");
        check(rim->GetBuilding()==platform && !raised->GetBuilding(),"native building occupies the ring and leaves the deck free");
        auto rimPlacement=rimSite;
        check(!bunkerType->CanPlaceHere(&rimPlacement,house),"outer ring blocks building placement");
    }
    if(!load && f==31) {
        highZ=raised->GetCoords().Z;
        check(highZ-base->GetCoords().Z==4*Unsorted::LevelHeight,"native floor uses raised height");
        bool platformCoversDeck=false;
        for(short y=edge.Y;y<edge.Y+4;++y) for(short x=edge.X;x<edge.X+4;++x)
            platformCoversDeck|=map.GetCellAt(CellStruct{x,y})->OccupyHeightsCoveringMe!=0;
        check(!platformCoversDeck,"platform does not register building cover above its deck");
        auto rifle=WeaponTypeClass::Find("M1Carbine");
        const auto source=map.GetCellAt(CellStruct{65,68})->GetCoords();
        const auto aim=map.GetCellAt(CellStruct{69,68})->GetCoords();
        check(rifle && TrajectoryHelper::FindFirstObstacle(source,aim,rifle->Projectile,house),"native rifle hits the platform cliff");
        auto power=BuildingTypeClass::Find("GAPOWR");
        CellStruct cross{71,70}, inside=buildingSite;
        check(!power->CanPlaceHere(&cross,house),"building cannot straddle cliff");
        check(power->CanPlaceHere(&inside,house),"building foundation fits on top");
        check(DisplayClass::Instance.PassesProximityCheck(power,house->ArrayIndex,
            power->GetFoundationData(false),&inside),"placement preview accepts a building within the deck and build radius");
        auto building=static_cast<BuildingClass*>(power->CreateObject(house));
        check(building->Unlimbo(map.GetCellAt(inside)->GetCoords(),DirType::North) && building->GetCoords().Z==highZ,
            "placed building on the raised deck");
        check(map.GetCellAt(tankSite)->OccupyHeightsCoveringMe>0,
            "ordinary buildings still register cover on the deck");
        auto tank=static_cast<UnitClass*>(UnitTypeClass::Find("MTNK")->CreateObject(house));
        check(tank->Unlimbo(map.GetCellAt(tankSite)->GetCoords(),DirType::East) && tank->GetCoords().Z==highZ,"tank stands on the deck");
        checkCliffWeapons(tank,building,house);
        check(platform->GetYSort()<tank->GetYSort(),
            "platform draws below the combat units on its deck");
        check(!rim->IsClearToMove(SpeedType::Track,true,true,-1,MovementZone::Normal,-1,false) &&
            tank->IsCellOccupied(rim,FacingType::None,-1,nullptr,false)==Move::No,"outer ring is impassable to ground vehicles");
        auto infantry=static_cast<InfantryClass*>(InfantryTypeClass::Find("E1")->CreateObject(house));
        check(infantry && infantry->IsCellOccupied(rim,FacingType::None,-1,nullptr,false)==Move::No,"outer ring is impassable to infantry");
        infantry->UnInit();
        tankId=tank->UniqueID;
        moveTank(tank,destination);
        map.Reveal(house);
        auto focus=map.GetCellAt(CellStruct{66,66})->GetCoords();
        TacticalClass::Instance->SetTacticalPosition(&focus);
    }
    if(!load && f==60) {
        auto tank=static_cast<UnitClass*>(UnitTypeClass::Find("MTNK")->CreateObject(house));
        check(tank && tank->Unlimbo(map.GetCellAt(groundTankSite)->GetCoords(),DirType::East),"placed tank below cliff");
        groundTank=TargetClass(tank);
        moveTank(tank,upCliffDestination);
    }
    if(!load && f==105) {
        auto infantry=placeTransferUnit(InfantryTypeClass::Find("E1"),house,{63,71});
        check(infantry->ObjectClickedAction(Action::Enter,platform,false),"issued platform entry shortly before saving");
    }
    if(!load && f==114) {
        auto tank=tankOnDeck();
        check(tank && tank->GetMapCoords()==destination && tank->GetCoords().Z==highZ,"tank crosses the raised deck at the raised height");
        auto below=groundTank.As_Unit();
        check(below && below->GetCoords().Z==base->GetCoords().Z && below->GetMapCoords().X<edge.X,
            "ground tank cannot climb the four-level cliff");
        capture(map.GetCellAt(CellStruct{70,70})->GetCoords());
        const auto center=projectedDeckCenter(anchor);
        CellStruct cell;
        const auto leftWall=pick({center.X-65,center.Y+85},cell);
        const auto rightWall=pick({center.X+65,center.Y+85},cell);
        check(leftWall==platform && rightWall==platform,
            "both visible platform walls resolve to the controller");
        const auto tankPoint=TacticalClass::Instance->CoordsToClient(tank->GetRenderCoords()).first;
        check(tank->BehindAnim==nullptr,"deck tank has no false behind-building indicator");
        bool pickedTank=false;
        for(int y=0;y<=18 && !pickedTank;y+=3) pickedTank=pick({tankPoint.X,tankPoint.Y-y},cell)==tank;
        check(pickedTank,"unit on the deck is still selectable");
        check(ScenarioClass::SaveGame("Saved Games\\HighPlatform.sav",L"High Platform"),"saved living platform and occupants");
    }
    if(!load && f==115) moveTank(tankOnDeck(),downCliffDestination);
    if(!load && f==180) {
        auto tank=tankOnDeck();
        check(tank && tank->GetCoords().Z==highZ && tank->GetMapCoords().X<72,"deck tank cannot drive off the four-level cliff");
        if(auto below=groundTank.As_Unit()) below->UnInit();
    }
    // Allow the walking infantry to finish its last subcell after reloading.
    const int collapseFrame=load?245:181;
    if(f==collapseFrame) {
        originalLevel=base->Level;
        highZ=raised->GetCoords().Z;
        auto tank=tankOnDeck();
        auto power=map.GetCellAt(buildingSite)->GetBuilding();
        check(platform && raised->Level==originalLevel+4 && tank && tank->GetCoords().Z==highZ && power && power->GetCoords().Z==highZ,
            "platform, terrain and both occupants survive save phase");
        check(!platform->Type->CanHideThings && !tank->BehindAnim,"standing platform does not hide the deck tank");
        check(!rim->IsClearToMove(SpeedType::Track,true,true,-1,MovementZone::Normal,-1,false),"outer ring remains blocked while the saved platform stands");
        tankId=tank->UniqueID;
        if(load) {
            bool restoredEntry=false;
            for(auto unit:InfantryClass::Array) {
                const auto cell=unit->GetMapCoords();
                if(unit->Owner==house && unit->GetCoords().Z==highZ &&
                   cell.X>=edge.X && cell.X<edge.X+4 && cell.Y>=edge.Y && cell.Y<edge.Y+4) restoredEntry=true;
            }
            check(restoredEntry,"pending platform entry completes after loading");
            platform->Sell(1);
        } else {
            int damage=platform->Health;
            platform->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,nullptr,true,false,nullptr);
        }
    }
    if(f>collapseFrame && raised->Level==originalLevel && !verifiedFall) {
        auto upper=map.GetCellAt(buildingSite)->GetBuilding();
        check(!upper || upper->Health<=0 || !upper->IsAlive,"collapse destroys the upper building");
        auto tank=tankOnDeck();
        check(tank && tank->IsFallingDown,"collapse begins bridge-style fall");
        auto rimPlacement=rimSite;
        check(rim->IsClearToMove(SpeedType::Track,true,true,-1,MovementZone::Normal,-1,false) &&
            bunkerType->CanPlaceHere(&rimPlacement,house),"collapse releases the outer ring for walking and building");
        verifiedFall=true;
    }
    if(verifiedFall) {
        auto tank=tankOnDeck();
        if(!tank || tank->Health<=0 || !tank->IsAlive) {
            check(sawFall,"occupant descends before landing");
            check(true,load?"sold platform collapses and kills the occupant":"destroyed platform collapses and kills the occupant");
            if(load) TerminateProcess(GetCurrentProcess(),0);
            rebuildEnemyPlatform(house);
            wallAttackFrame=f;
            return 0;
        }
        if(tank->GetCoords().Z<highZ) sawFall=true;
    }
    if(f==500) check(false,"timed out waiting for platform collapse");
    return 0;
}
