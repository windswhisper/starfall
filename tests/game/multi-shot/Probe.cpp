#include "../../../src/Starfall.cpp"
std::vector<CoordStruct> laserEnds;
DEFINE_HOOK(0x54FE60,SF_TestExtraLasers,6) {
    laserEnds.push_back(R->Stack<CoordStruct>(0x10));
    return 0;
}
namespace {
void check(bool ok,const char* name) {
    std::ofstream("multishot.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
UnitClass* place(HouseClass* owner,CellStruct p) {
    auto unit=static_cast<UnitClass*>(UnitTypeClass::Find("MTNK")->CreateObject(owner));
    check(unit && unit->Unlimbo(MapClass::Instance.GetCellAt(p)->GetCoordsWithBridge(),DirType::North),"place");
    if(owner!=HouseClass::CurrentPlayer) unit->Health=unit->EstimatedHealth=20000;
    return unit;
}
void fire(UnitClass* shooter,TechnoClass* primary,std::set<DWORD> expected,const char* label) {
    shooter->SetTarget(primary);shooter->RearmTimer.Stop();
    auto facing=shooter->GetTargetDirection(primary);
    shooter->PrimaryFacing.SetCurrent(facing);shooter->SecondaryFacing.SetCurrent(facing);
    auto fireError=shooter->GetFireError(primary,0,false);
    int before=triggerTotals[shooter->UniqueID]["countshots"];
    std::set<DWORD> old;
    for(auto b:BulletClass::Array) old.insert(b->UniqueID);
    auto main=shooter->Fire(primary,0);
    for(auto b:BulletClass::Array) if(b->Owner==shooter && b->Target==primary && !old.contains(b->UniqueID)) main=b;
    if(!main || main->Target!=primary) std::ofstream("multishot.log",std::ios::app)<<label<<" primary error "<<int(fireError)
        <<" weapon "<<shooter->GetWeapon(0)->WeaponType->ID<<" distance "<<shooter->DistanceFrom(primary)<<" bullets "<<BulletClass::Array.Count<<"\n";
    check(main && main->Target==primary,"primary bullet");
    std::set<DWORD> actual;int size=0;
    for(auto b:BulletClass::Array) if(b->Owner==shooter && !old.contains(b->UniqueID)) {
        ++size;check(b->WeaponType==main->WeaponType && b->Health==main->Health && b->WH==main->WH &&
            b->Type==main->Type && b->Target,"same weapon and damage");
        actual.insert(b->Target->UniqueID);
    }
    dispatch();
    std::ofstream("multishot.log",std::ios::app)<<label<<" bullets "<<size<<" expected "<<expected.size()<<"\n";
    if(actual!=expected) {
        std::ofstream out("multishot.log",std::ios::app);
        for(auto id:expected) if(!actual.contains(id)) {
            for(auto u:TechnoClass::Array) if(u->UniqueID==id)
                out<<"missing "<<u->GetTechnoType()->ID<<" hp "<<u->Health<<" err "<<int(shooter->GetFireError(u,0,false))<<" dist "<<shooter->DistanceFrom(u)<<"\n";
        }
    }
    check(shooter->Target==primary && actual==expected && size==static_cast<int>(expected.size()) &&
          triggerTotals[shooter->UniqueID]["countshots"]==before+1,label);
}
}
#include "TracerCheck.inc"
#include "ContinuousBeamCheck.inc"
DEFINE_HOOK(0x55B4EB,SF_TestMultiShot,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    auto f=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(!loading && f==20) {
        Research::ensureReady();
        check(skills.contains("splitfire") && !skills.contains("invalidsplit") &&
            !skills.contains("badcount") && !skills.contains("badformula"),"configuration");
        auto human=HouseClass::CurrentPlayer,foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
        auto u=place(human,{45,73}),p=place(foe,{47,73});
        place(human,{45,72});place(foe,{55,73});
        auto hidden=place(foe,{44,73});hidden->CloakState=CloakState::Cloaked;
        grantToUnit(u,PassiveGrant{{"splitfire","countshots"},-1});
        u->FirepowerMultiplier=1.5;
        fire(u,p,{p->UniqueID},"zero stacks");
        testContinuousBeams(u,p);
        changeStacks(u->UniqueID,"splitfire",1,6);
        fire(u,p,{p->UniqueID},"lone target");
        auto n=place(foe,{46,73}),q=place(foe,{45,76});
        fire(u,p,{p->UniqueID,n->UniqueID},"nearest extra");
        {
            auto weapon=u->GetWeapon(0)->WeaponType;
            auto projectile=weapon->Projectile;
            const bool previousAA=projectile->AA;
            projectile->AA=true;
            auto plane=[&](CellStruct cell) {
                auto air=static_cast<AircraftClass*>(AircraftTypeClass::Find("ORCA")->CreateObject(foe));
                check(air && air->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge(),DirType::North),"place air target");
                air->SetHeight(300);
                check(air->IsInAir(),"airborne target");
                return air;
            };
            auto nearAir=plane({45,74}),farAir=plane({45,77});
            grantToUnit(u,PassiveGrant{{"samelayer"},-1});
            fire(u,nearAir,{nearAir->UniqueID,n->UniqueID},"zero preference stacks keep nearest");
            changeStacks(u->UniqueID,"splitfire",-1,6);
            changeStacks(u->UniqueID,"samelayer",1,3);
            fire(u,nearAir,{nearAir->UniqueID,farAir->UniqueID},"air primary prefers air");
            check(u->DistanceFrom(nearAir)<u->DistanceFrom(p),"air candidate closer than ground");
            fire(u,n,{n->UniqueID,p->UniqueID},"ground primary prefers ground");
            farAir->Limbo();
            fire(u,nearAir,{nearAir->UniqueID,n->UniqueID},"air primary falls back to ground");
            nearAir->Limbo();
            removeHeldPassive(u,"samelayer");
            changeStacks(u->UniqueID,"splitfire",1,6);
            projectile->AA=previousAA;
        }
        changeStacks(u->UniqueID,"splitfire",1,6);
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"two distinct extras");
        auto challenger=place(foe,{45,74});
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"nearer enemy does not steal split locks");
        n->CloakState=CloakState::Cloaked;
        challenger->CloakState=CloakState::Cloaked;
        fire(u,p,{p->UniqueID,q->UniqueID},"one split remains");
        check(splitTargets.at(u->UniqueID).branches==vector<DWORD>{0,q->UniqueID},"survivor keeps its branch");
        challenger->CloakState=CloakState::Uncloaked;
        fire(u,p,{p->UniqueID,q->UniqueID,challenger->UniqueID},"vacant split reacquires");
        check(splitTargets.at(u->UniqueID).branches==vector<DWORD>{challenger->UniqueID,q->UniqueID},"replacement fills original vacancy");
        challenger->Limbo();
        n->CloakState=CloakState::Uncloaked;
        testSplitBeams(u,p,n,q);
        testTracers(p,n,q);
        auto weapon=u->GetWeapon(0)->WeaponType;
        auto originalProjectile=weapon->Projectile;
        for(auto type:{"AAHeatSeeker2","Ballistic"}) {
            weapon->Projectile=BulletTypeClass::Find(type);
            check(weapon->Projectile!=nullptr,"projectile fixture");
            fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},type);
        }
        weapon->Projectile=originalProjectile;
        bool laser=weapon->IsLaser;
        weapon->IsLaser=true;laserEnds.clear();
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"laser targets");
        check(laserEnds.size()==3,"three laser beams");
        for(auto enemy:{p,n,q})
            check(std::find(laserEnds.begin(),laserEnds.end(),enemy->GetTargetCoords())!=laserEnds.end() ||
                std::find(laserEnds.begin(),laserEnds.end(),enemy->GetCenterCoords())!=laserEnds.end(),"laser reaches intended target");
        weapon->IsLaser=laser;
        auto endpoints=[&](const auto& effects,int start,auto endpoint) {
            std::set<int> hits;
            const std::array<UnitClass*,3> enemies={p,n,q};
            for(int i=start;i<effects.Count;++i) for(int j=0;j<3;++j)
                if(endpoint(effects[i])==enemies[j]->GetTargetCoords() || endpoint(effects[i])==enemies[j]->GetCenterCoords()) hits.insert(j);
            return hits.size()==3;
        };
        int bolts=EBolt::Array.Count;
        weapon->IsElectricBolt=true;
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"electric targets");
        check(EBolt::Array.Count==bolts+3 && endpoints(EBolt::Array,bolts,[](auto b){return b->Point2;}),"electric arcs reach three endpoints");
        weapon->IsElectricBolt=false;
        int beams=RadBeam::Array.Count;
        weapon->IsRadBeam=true;
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"radiation targets");
        check(RadBeam::Array.Count==beams+3 && endpoints(RadBeam::Array,beams,[](auto b){return b->TargetLocation;}),"radiation beams reach three endpoints");
        weapon->IsRadBeam=false;
        auto oldParticles=weapon->AttachedParticleSystem;
        weapon->IsRailgun=true;weapon->AttachedParticleSystem=ParticleSystemTypeClass::Find("LargeRailgunSys");
        check(weapon->AttachedParticleSystem!=nullptr,"railgun fixture");
        int particles=ParticleSystemClass::Array.Count;
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"railgun targets");
        check(endpoints(ParticleSystemClass::Array,particles,[](auto s){return s->TargetCoords;}),"rail trails reach three endpoints");
        weapon->IsRailgun=false;weapon->AttachedParticleSystem=oldParticles;
        for(bool sparks:{false,true}) {
            weapon->UseSparkParticles=sparks;weapon->UseFireParticles=!sparks;
            weapon->AttachedParticleSystem=ParticleSystemTypeClass::Find(sparks?"WeldingSys":"FireStreamSys");
            check(weapon->AttachedParticleSystem!=nullptr,"directed particle fixture");
            int start=ParticleSystemClass::Array.Count;
            fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},sparks?"spark targets":"flame targets");
            std::set<AbstractClass*> directed;
            for(int i=start;i<ParticleSystemClass::Array.Count;++i) {
                auto system=ParticleSystemClass::Array[i];
                if(system->Type==weapon->AttachedParticleSystem && system->Owner==u) directed.insert(system->Target);
            }
            check(directed.contains(n) && directed.contains(q),"particles bound to both secondary targets");
            weapon->UseSparkParticles=weapon->UseFireParticles=false;
            weapon->AttachedParticleSystem=oldParticles;
        }
        {
            weapon->IsSonic=true;
            int secondaryBeams=RadBeam::Array.Count;
            fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"sonic targets");
            check(RadBeam::Array.Count==secondaryBeams && u->Target==p,"sonic extras preserve primary and allocate no beams");
            MapClass::Instance.Reveal(human);
            auto tactical=TacticalClass::Instance;
            auto previous=tactical->TacticalPos;
            tactical->TacticalPos=TacticalClass::CoordsToScreen(u->Location)-Point2D{200,150};
            auto surface=GameCreate<BSurface>(400,300);
            auto lit=[&](Point2D center) {
                for(int y=-8;y<=8;++y) for(int x=-8;x<=8;++x) {
                    Point2D sample{center.X+x,center.Y+y};
                    if(surface->GetPixel(&sample)) return true;
                }
                return false;
            };
            for(unsigned age:{6u,18u,19u}) {
                surface->Fill(0);
                SonicRipple::draw(surface,{0,0,400,300},f+age);
                for(auto enemy:{n,q}) {
                    auto dest=tactical->CoordsToClient(enemy->GetTargetCoords()).first;
                    auto muzzle=tactical->CoordsToClient(u->GetFLH(0,CoordStruct::Empty)).first;
                    auto point=age==6?Point2D{(muzzle.X+dest.X)/2,(muzzle.Y+dest.Y)/2}:dest;
                    check(lit(point)==(age!=19),"both sonic paths advance reach their endpoints and fade");
                }
            }
            GameDelete(surface);
            tactical->TacticalPos=previous;
            weapon->IsSonic=false;
            weapon->IsMagBeam=true;
            fire(u,p,{p->UniqueID},"magnetic primary only");
            check(RadBeam::Array.Count==secondaryBeams,"no secondary magnetic beams");
            weapon->IsMagBeam=false;
        }
        changeStacks(u->UniqueID,"splitfire",4,6);
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"fewer enemies than layers");
        u->Health=1;
        fire(u,p,{p->UniqueID},"conditional exit");
        auto third=place(foe,{45,77});
        grantToUnit(u,PassiveGrant{{"fixedshot"},-1});
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID,third->UniqueID},"three extra without stacks");
        grantToUnit(u,PassiveGrant{{"formulashot"},-1});
        check(multiShotSettings(u,p).count==4,"fixed plus formula");
        removeHeldPassive(u,"fixedshot");
        changeStacks(u->UniqueID,"formulashot",1,6);
        fire(u,p,{p->UniqueID,n->UniqueID},"fraction rounded down");
        changeStacks(u->UniqueID,"formulashot",1,6);
        fire(u,p,{p->UniqueID,n->UniqueID,q->UniqueID},"formula follows stacks");
        removeHeldPassive(u,"formulashot");
        third->Limbo();
        u->Health=u->Type->Strength;
        hidden->Limbo();
        std::ofstream("multishot.ids")<<u->UniqueID<<" "<<p->UniqueID<<" "<<n->UniqueID<<" "<<q->UniqueID;
        check(ScenarioClass::SaveGame("Saved Games\\MultiShot.sav",L"MultiShot"),"saved");
        TerminateProcess(GetCurrentProcess(),0);
    }
    if(loading && f==21) {
        DWORD uid,pid,nid,qid;std::ifstream("multishot.ids")>>uid>>pid>>nid>>qid;
        UnitClass *u=nullptr,*p=nullptr;
        for(auto unit:UnitClass::Array) {
            if(unit->UniqueID==uid) u=unit;
            if(unit->UniqueID==pid) p=unit;
        }
        check(u && p && stackCount(uid,"splitfire")==6,"restored units and stacks");
        check(splitTargets.contains(uid) && splitTargets.at(uid).branches==vector<DWORD>{nid,qid},"restored split target order");
        u->Type->Ammo=1;
        u->Ammo=1;
        fire(u,p,{pid,nid,qid},"restored layered shot");
        check(u->Ammo==0,"one round consumed");
        u->Ammo=1;
        removeHeldPassive(u,"splitfire");
        fire(u,p,{pid},"removed passive");
        for(auto unit:UnitClass::Array) unit->Deactivate();
    }
    if(loading && f==90) {
        DWORD uid,pid,nid,qid;
        std::ifstream("multishot.ids")>>uid>>pid>>nid>>qid;
        std::set<DWORD> hit;
        for(auto unit:UnitClass::Array)
            if((unit->UniqueID==nid || unit->UniqueID==qid) && unit->Health<20000) hit.insert(unit->UniqueID);
        check(hit.size()==2,"both extra targets took damage");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
