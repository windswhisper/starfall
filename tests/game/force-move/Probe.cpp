#include "../../../src/Starfall.cpp"
#include <EventClass.h>
#include <IsometricTileTypeClass.h>
namespace {
void check(bool ok,const char* name) {
    std::ofstream("force-move.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
double tiltAngle(UnitClass* u) {
    auto [forward,side]=ForceMove::visualTilt(u);
    return std::acos(std::clamp(std::cos(double(forward))*std::cos(double(side)),-1.0,1.0));
}
vector<WORD> bodyPixels(TechnoClass* u) {
    auto surface=DSurface::Temp;
    Point2D pos{200,180};RectangleStruct bounds=DSurface::ViewBounds,area{140,110,120,130};
    ZBuffer::Instance->FillRect(0xFFFF,area);
    surface->FillRect(&area,0x39E7);
    u->DrawIt(&pos,&bounds);
    auto pitch=surface->GetPitch();
    auto bytes=static_cast<const BYTE*>(surface->Lock(area.X,area.Y));
    check(bytes!=nullptr,"voxel surface");
    vector<WORD> result;
    for(int y=0;y<area.Height;++y) {
        auto row=reinterpret_cast<const WORD*>(bytes+y*pitch);
        result.insert(result.end(),row,row+area.Width);
    }
    surface->Unlock();
    return result;
}
FootClass* place(TechnoTypeClass* type,CellStruct pos) {
    auto foot=static_cast<FootClass*>(type->CreateObject(HouseClass::CurrentPlayer));
    check(foot && foot->Unlimbo(MapClass::Instance.GetCellAt(pos)->GetCoordsWithBridge(),DirType::North),"place");
    return foot;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestForceMove,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int f=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    auto tank=UnitTypeClass::Find("MTNK");
    if(!loading && f==20) {
        Research::ensureReady();
        check(skills.contains("scriptpush") && skills.contains("pull") && !skills.contains("badoffset") && !skills.contains("badease"),"config");
        auto caster=place(tank,{45,73}),target=place(tank,{48,70});
        auto pulled=place(InfantryTypeClass::Find("E1"),{48,73});
        auto blocked=place(tank,{45,76});
        place(tank,{46,76});
        check(skills.contains("weightlift") && !skills.contains("badweight") && !skills.contains("badiron"),"weight and iron configuration");
        auto lift=skills.at("weightlift").actions.front().forceMove;
        auto weight=tank->Weight;
        auto mission=target->CurrentMission;
        tank->Weight=4.5;
        check(!ForceMove::start(target,lift,target->Location),"equal weight rejected");
        tank->Weight=4.75;
        check(!ForceMove::start(target,lift,target->Location) && target->CurrentMission==mission && !ForceMove::locked(target),"heavier target retains control");
        tank->Weight=4.25;
        target->IronCurtain(3,caster->Owner,false);
        const auto& ordinary=skills.at("slowstart").actions.front().forceMove;
        check(target->IsIronCurtained() && !ForceMove::start(target,ordinary,target->Location) && !ForceMove::locked(target),"iron immune by default");
        check(ForceMove::start(target,lift,target->Location),"INI permits light iron target");
        check(!ForceMove::start(target,ordinary,target->Location) && ForceMove::motions.at(target->UniqueID).duration==6,"rejected replacement preserves motion");
        grantToUnit(caster,PassiveGrant{{"scriptpush","pull"},-1});
        capture("onfire",caster,target,caster,target,0);dispatch();
        tank->Weight=weight;
        check(ForceMove::motions.at(target->UniqueID).dx==768,"Lua overrides weight and iron");
        capture("ondealdamage",caster,pulled,caster,pulled,5);dispatch();
        check(ForceMove::locked(target) && ForceMove::locked(pulled),"Lua and INI start");
        ForceMove::Spec push;push.mode=ForceMove::Mode::Push;push.distance=3*256;push.duration=1;push.ignoreCollision=false;
        auto center=blocked->Location;center.X-=256;
        check(ForceMove::start(blocked,push,center),"one frame push");
        auto slow=place(tank,{40,75}),both=place(tank,{42,75});
        check(ForceMove::start(slow,skills.at("slowstart").actions.front().forceMove,slow->Location) &&
            ForceMove::start(both,skills.at("slowends").actions.front().forceMove,both->Location),"eased motions started");
        std::ofstream("motion.ease")<<slow->UniqueID<<" "<<both->UniqueID<<" "<<slow->Location.Y<<" "<<both->Location.Y<<" "<<caster->Location.X;
        auto vehicle=static_cast<UnitClass*>(target);
        vehicle->PrimaryFacing.SetCurrent(DirStruct(DirType::North));
        auto lean=ForceMove::visualTilt(vehicle);
        check(lean.forward>0 && lean.side>0 && std::abs(tiltAngle(vehicle)-ForceMove::maxTilt)<0.0001,"immediate directional tilt");
        auto renderTilt=bodyPixels(target);
        ForceMove::motions.at(target->UniqueID).tilt=false;
        auto renderFlat=bodyPixels(target);
        ForceMove::motions.at(target->UniqueID).tilt=true;
        check(renderFlat!=renderTilt && target->AngleRotatedForwards==0 && target->AngleRotatedSideways==0,"tilted voxel pixels only");
        auto flyer=place(tank,{40,80}),under=place(tank,{42,80}),victim=place(tank,{44,80});
        auto crash=place(tank,{47,81}),drown=place(tank,{40,83});
        auto powerType=BuildingTypeClass::Find("GAPOWR");
        auto building=static_cast<BuildingClass*>(powerType->CreateObject(HouseClass::CurrentPlayer));
        check(building && building->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{51,81})->GetCoordsWithBridge(),DirType::North),"place building");
        ForceMove::Spec through;through.dx=4*256;through.duration=10;
        check(ForceMove::start(flyer,through,flyer->Location),"ignore collision default");
        crash->Health=crash->EstimatedHealth=60;
        auto buildingCell=building->GetMapCoords();
        through.dx=(buildingCell.X-crash->GetMapCoords().X)*256;
        through.dy=(buildingCell.Y-crash->GetMapCoords().Y)*256;
        check(ForceMove::start(crash,through,crash->Location),"aim at building");
        through.dx=4*256;through.dy=0;
        auto water=MapClass::Instance.GetCellAt(CellStruct{44,83});
        for(int i=0;i<IsometricTileTypeClass::Array.Count;++i) {
            water->IsoTileTypeIndex=i;
            if(water->Tile_Is_Water()) break;
        }
        water->RecalcAttributes(-1);
        check(water->LandType==LandType::Water,"water landing fixture");
        check(ForceMove::start(drown,through,drown->Location),"aim at water");
        std::ofstream("landing.ids")<<flyer->UniqueID<<" "<<under->UniqueID<<" "<<victim->UniqueID<<" "
            <<crash->UniqueID<<" "<<building->UniqueID<<" "<<drown->UniqueID<<" "<<building->Health<<" "<<under->Health;
        auto pos=target->Location;
        std::ofstream("motion.ids")<<caster->UniqueID<<" "<<target->UniqueID<<" "<<pulled->UniqueID<<" "<<blocked->UniqueID<<" "<<pos.X<<" "<<pos.Y;
        check(!target->IsControllable() && !ActiveSkills::readyCaster(target,target->Owner),"native and active control blocked");
        target->QueueMission(Mission::Unload,true);
        target->ForceMission(Mission::Attack);
        target->SetDestination(MapClass::Instance.GetCellAt(CellStruct{52,72}),true);
        check(target->CurrentMission==Mission::Sleep && !target->Destination,"orders rejected");
        int bullets=BulletClass::Array.Count;
        target->Fire(caster,0);
        check(BulletClass::Array.Count==bullets,"fire blocked");
    }
    if(f!=21 && f!=23 && f!=29 && f!=25 && f!=26 && f!=28 && f!=30 && f!=33 && (f<34 || f>220)) return 0;
    DWORD c,t,p,b;int x,y;
    std::ifstream("motion.ids")>>c>>t>>p>>b>>x>>y;
    Objects objects;for(auto u:TechnoClass::Array) if(alive(u)) objects.emplace(u->UniqueID,u);
    auto target=findLive(objects,t),block=findLive(objects,b),pulled=findLive(objects,p);
    check(target && block && pulled,"live units");
    if(f==23 || f==29 || f==33) {
        DWORD slow,both;int slowY,bothY,casterX;
        std::ifstream("motion.ease")>>slow>>both>>slowY>>bothY>>casterX;
        auto a=static_cast<UnitClass*>(objects.at(slow)),v=static_cast<UnitClass*>(objects.at(both)),caster=static_cast<UnitClass*>(objects.at(c));
        int in=f==23?16:f==29?144:256,out=f==23?28:f==29?60:64,inOut=f==23?40:f==29?216:256;
        if(f==33) {
            in=((slowY+in)/256)*256+128-slowY;
            inOut=((bothY+inOut)/256)*256+128-bothY;
            out=((casterX+out)/256)*256+128-casterX;
        }
        check(a->Location.Y-slowY==in && v->Location.Y-bothY==inOut && caster->Location.X-casterX==out,
            f==23?"eased first quarter":f==29?"eased last quarter":"all curves arrive");
        check(tiltAngle(a)==0 && tiltAngle(caster)==0,"INI and Lua disabled tilt");
    }
    DWORD flyer,under,victim,crash,building,drown;int initialBuilding,initialUnder;
    std::ifstream("landing.ids")>>flyer>>under>>victim>>crash>>building>>drown>>initialBuilding>>initialUnder;
    if(f==25) {
        check(objects.at(flyer)->GetMapCoords()==objects.at(under)->GetMapCoords() &&
            objects.at(under)->Health==initialUnder && objects.contains(victim),"overlap in transit without damage");
        double angle=tiltAngle(static_cast<UnitClass*>(block));
        check(angle>0 && angle<ForceMove::maxTilt && std::abs(tiltAngle(static_cast<UnitClass*>(target))-ForceMove::maxTilt)<0.0001,"held and settling tilt");
    }
    if(f==21) check(!ForceMove::locked(block) && block->GetMapCoords()==CellStruct{45,76},"wall stops ground push");
    if(f==25) {
        check(ForceMove::motions.at(t).elapsed==5 && ForceMove::motions.at(t).lift()>0 && !target->IsInAir(),"ground target in flight");
        if(!loading) check(ScenarioClass::SaveGame("Saved Games\\ForceMove.sav",L"ForceMove"),"saved in flight");
    }
    if(f==26) {
        check(ForceMove::motions.at(t).elapsed==6 && target->Location.X==x+384 && target->Location.Y==y-192,"resumed motion");
        ForceMove::Spec launch;launch.mode=ForceMove::Mode::Launch;launch.height=512;launch.duration=8;
        check(ForceMove::start(block,launch,block->Location),"vertical launch");
        int hp=target->Health,damage=5;
        target->ReceiveDamage(&damage,0,WarheadTypeClass::Find("AP"),nullptr,true,false,nullptr);
        check(target->Health<hp && ForceMove::locked(target),"damage during forced move");
    }
    if(f==28) {
        int lift=ForceMove::motions.at(b).lift();
        ForceMove::Spec shove;shove.dx=768;shove.duration=4;shove.ignoreCollision=false;
        check(lift>0 && ForceMove::start(block,shove,block->Location) && ForceMove::motions.at(b).lift()==lift,"airborne replacement");
    }
    if(f==30) {
        check(objects.contains(flyer) && objects.at(flyer)->GetMapCoords()==CellStruct{44,80} &&
            !objects.contains(victim) && objects.contains(under),"landing crushes occupant only");
        check(!objects.contains(crash) && objects.contains(building) && objects.at(building)->Health<initialBuilding,"building hurt and falling unit lost");
        if(auto found=objects.find(drown);found!=objects.end()) {
            auto u=found->second;
            auto cell=u->GetCell();
            std::ofstream("force-move.log",std::ios::app)<<"water result "<<u->GetMapCoords().X<<","<<u->GetMapCoords().Y
                <<" land "<<int(cell->LandType)<<" speed "<<int(u->GetTechnoType()->SpeedType)
                <<" pass "<<ForceMove::terrainAllows(static_cast<FootClass*>(u),cell)<<" sink "<<u->IsSinking<<" hp "<<u->Health<<"\n";
        }
        check(!objects.contains(drown) || objects.at(drown)->IsSinking || objects.at(drown)->IsCrashing,"invalid terrain landing");
        check((MapClass::Instance.GetCellAt(CellStruct{42,80})->OccupationFlags&0x20)!=0,"crossed unit retains occupation");
    }
    if(f==30) check(ForceMove::motions.at(b).blocked && ForceMove::motions.at(b).lift()>0,"airborne collision waits for landing");
    if(f==33) {
        check(!ForceMove::locked(block),"airborne collision released");
        double angle=tiltAngle(static_cast<UnitClass*>(target));
        check(angle>0 && angle<ForceMove::maxTilt,"released before upright");
        auto expectedCell=CellClass::Coord2Cell(CoordStruct{x+768,y-384,target->Location.Z});
        check(!ForceMove::locked(target) && target->GetMapCoords()==expectedCell &&
            target->Location.X%256==128 && target->Location.Y%256==128 && !target->IsInAir(),"Lua offset lands vehicle at destination cell center");
        check(target->IsControllable() && ActiveSkills::readyCaster(target,target->Owner),"orders enabled after landing");
        check(!ForceMove::locked(pulled) && pulled->GetMapCoords()==CellStruct{46,73},"pull stops at occupied center");
        auto foot=static_cast<FootClass*>(target);
        foot->QueueMission(Mission::Move,true);
        foot->SetDestination(MapClass::Instance.GetCellAt(CellStruct{56,69}),true);
    }
    static unsigned interrupted=0;
    static CoordStruct movingStart{};
    if(f>=34 && !interrupted && target->Location.X!=x+768) {
        movingStart=target->Location;
        ForceMove::Spec lift;lift.height=256;lift.duration=6;
        check(ForceMove::start(target,lift,movingStart),"interrupt moving unit");
        interrupted=f;
    }
    if(interrupted && f==interrupted+9) {
        check(!ForceMove::locked(target) && target->GetMapCoords()==CellClass::Coord2Cell(movingStart) &&
            target->Location.X%256==128 && target->Location.Y%256==128 &&
            !static_cast<FootClass*>(target)->Destination,"canceled path stays canceled");
        auto cell=MapClass::Instance.GetCellAt(CellStruct{54,71});
        check(target->Select() && target->IsControllable(),"released vehicle selectable");
        EventClass order(target->Owner->ArrayIndex,TargetClass(target),Mission::Move,TargetClass(),TargetClass(cell),TargetClass());
        check(EventClass::OutList.Add(order),"new player movement order");
    }
    if(interrupted && f>interrupted+9 && target->GetMapCoords()==CellStruct{54,71}) {
        check(true,"interrupted vehicle reaches new order");
        TerminateProcess(GetCurrentProcess(),0);
    }
    if(f==220) {
        auto foot=static_cast<FootClass*>(target);
        std::ofstream("force-move.log",std::ios::app)<<"stuck "<<foot->Location.X<<","<<foot->Location.Y
            <<" mission "<<int(foot->CurrentMission)<<" dest "<<(foot->Destination!=nullptr)<<" powered "<<bool(foot->Locomotor->Is_Powered())
            <<" moving "<<bool(foot->Locomotor->Is_Moving())<<" speed "<<foot->SpeedPercentage<<"\n";
        check(false,"interrupted vehicle reaches new order");
    }
    return 0;
}
