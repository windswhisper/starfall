#include "../../../src/Starfall.cpp"
#include <fstream>

namespace BlockerTest {
constexpr CellStruct site{69,68},gunCell{65,68},rear{74,68};
BuildingClass* cover=nullptr;
BuildingClass* target=nullptr;
BuildingClass* bystander=nullptr;
TechnoClass* gun=nullptr;
AbstractClass* victim=nullptr;
BulletTypeClass* expected=nullptr;
CoordStruct point{};
bool exploded=false;
int stage=0,started=0,coverHP=0,targetHP=0;
DWORD missileID=0;
TechnoClass* guard=nullptr;
int splashHP=0;
int rearTestHits=0,splashTestHits=0;
std::set<DWORD> seenShieldAnims;
int shieldAnims=0;
LaserDrawClass* lastLaser=nullptr;
WeaponTypeClass* savedGun=nullptr;

void check(bool ok,const char* message) {
    std::ofstream("blocker.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<message<<"\n";
    if(!ok && guard) std::ofstream("blocker.log",std::ios::app)<<"stage="<<stage<<" guard="<<guard->Health
        <<" rear="<<target->Health<<"/"<<targetHP<<" probe hits="<<rearTestHits<<" splash="<<bystander->Health<<"/"<<splashHP<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
CellClass* cell(CellStruct at) { return MapClass::Instance.GetCellAt(at); }
BuildingClass* place(const char* id,HouseClass* owner,CellStruct at) {
    check(BuildingSpawn::createAt(BuildingTypeClass::Find(id),owner,at),id);
    return cell(at)->GetBuilding();
}
void replace(const char* id,HouseClass* owner) {
    if(cover) cover->UnInit();
    cover=place(id,owner,site);
}
void observe() {
    rearTestHits=splashTestHits=0;
    shieldAnims=0;
    exploded=false;victim=nullptr;point={};
    started=Unsorted::CurrentFrame;
    coverHP=cover->Health;targetHP=target->Health;
}
void fire(int slot) {
    observe();
    auto facing=gun->GetTargetDirection(target);
    gun->SetTarget(target);
    gun->PrimaryFacing.SetCurrent(facing);
    gun->SecondaryFacing.SetCurrent(facing);
    gun->RearmTimer.Stop();
    expected=gun->GetWeapon(slot)->WeaponType->Projectile;
    check(gun->GetFireError(target,slot,false)==FireError::OK,"rear target can be fired upon");
    gun->Fire(target,slot);
    gun->RearmTimer.Start(10000);
}
void fireBeam(int kind,bool blocked) {
    auto weapon=WeaponTypeClass::Find("PBInstantGun");
    gun->GetWeapon(0)->WeaponType=weapon;
    weapon->IsLaser=kind==0;
    weapon->IsElectricBolt=kind==1;
    weapon->IsRadBeam=kind==2;
    lastLaser=nullptr;
    const int bolts=EBolt::Array.Count,beams=RadBeam::Array.Count;
    fire(0);
    CoordStruct end;
    if(kind==0) {
        check(lastLaser!=nullptr,"laser drawn");
        end=lastLaser->Target;
    } else if(kind==1) {
        check(EBolt::Array.Count==bolts+1,"electric bolt drawn");
        end=EBolt::Array.Items[bolts]->Point2;
    } else {
        check(RadBeam::Array.Count==beams+1,"radiation beam drawn");
        end=RadBeam::Array.Items[beams]->TargetLocation;
    }
    auto center=guard->GetCoords();
    if(guard->Health>700) center.Z+=64;
    if(blocked) check(end.X<center.X && std::abs(center.DistanceFrom(end)-512)<=2,"beam ends on incoming shield face");
    else check(end==target->GetTargetCoords() || end==target->GetCenterCoords(),"beam passes disabled instant shield");
}
void orbitalBeam(int kind) {
    auto weapon=WeaponTypeClass::Find("PBOrbital");
    check(weapon && FireOrigin::enabled(weapon),"read weapon-relative origin");
    gun->GetWeapon(0)->WeaponType=weapon;
    weapon->IsLaser=kind==0;
    weapon->IsElectricBolt=kind==1;
    weapon->IsRadBeam=kind==2;
    lastLaser=nullptr;
    int bolts=EBolt::Array.Count,beams=RadBeam::Array.Count;
    observe();
    expected=weapon->Projectile;
    gun->SetTarget(guard);
    gun->RearmTimer.Stop();
    auto bullet=gun->Fire(guard,0);
    gun->RearmTimer.Start(10000);
    const auto above=guard->GetTargetCoords()+CoordStruct{0,0,1536};
    check(bullet && bullet->SourceCoords==above && bullet->Target==guard,"instant round starts above its own target");
    CoordStruct from{},to{};
    if(kind==0 && lastLaser) {from=lastLaser->Source;to=lastLaser->Target;}
    if(kind==1 && EBolt::Array.Count==bolts+1) {
        auto bolt=EBolt::Array.Items[bolts];from=bolt->Point1;to=bolt->Point2;
        check(!bolt->Owner,"overhead bolt stays detached from the shooter");
    }
    if(kind==2 && RadBeam::Array.Count==beams+1) {
        auto beam=RadBeam::Array.Items[beams];from=beam->SourceLocation;to=beam->TargetLocation;
    }
    const auto top=guard->GetCoords()+CoordStruct{0,0,576};
    check(from==above && to==top,"beam travels from overhead to the top of the shield");
}

void launch(const char* id,AbstractClass* destination,CoordStruct from,BulletVelocity velocity) {
    observe();
    auto weapon=WeaponTypeClass::Find(id);
    expected=weapon->Projectile;
    auto b=weapon->Projectile->CreateBullet(destination,gun,100,weapon->Warhead,weapon->Speed,false);
    b->SetWeaponType(weapon);
    if(weapon->Projectile==BulletTypeClass::Find("PBMissile")) missileID=b->UniqueID;
    check(b->MoveTo(from,velocity),id);
}
}
DEFINE_HOOK(0x54FE60,ObserveShieldLaser,6) {
    BlockerTest::lastLaser=R->ECX<LaserDrawClass*>();
    return 0;
}
// Only count hits from this probe. Other actors on the range can fire at the
// rear building during the longer sequence.
DEFINE_HOOK(0x701906,ObserveShieldVictim,8) {
    using namespace BlockerTest;
    const auto unit=R->ECX<TechnoClass*>();
    if(stage>=8 && (unit==target || unit==bystander)) {
        auto wh=R->Stack<WarheadTypeClass*>(0xC0);
        auto source=R->Stack<TechnoClass*>(0xC4);
        if(source==gun && wh && !_stricmp(wh->ID,"PBWH")) {
            if(unit==target) ++rearTestHits;
            else ++splashTestHits;
        }
    }
    return 0;
}
DEFINE_HOOK(0x4690B0,ObserveBlockerWarhead,6) {
    using namespace BlockerTest;
    auto b=R->ECX<BulletClass*>();
    if(!gun && !missileID && GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES)
        std::ifstream("blocker-missile.txt")>>missileID;
    if((gun?b->Owner==gun:b->UniqueID==missileID) && b->WH && !_stricmp(b->WH->ID,"PBWH") &&
       b->Type==(expected?expected:BulletTypeClass::Find("PBMissile")) && !exploded) {
        exploded=true;point=*R->Stack<CoordStruct*>(4);victim=b->Target;
        std::ofstream("blocker.log",std::ios::app)<<"impact stage "<<stage<<" at "<<point.X<<","<<point.Y<<","<<point.Z
            <<" projectile="<<b->Type->ID<<" target="<<(victim?victim->UniqueID:0)<<"\n";
    }
    return 0;
}

DEFINE_HOOK(0x55B4EB,CheckProjectileBlocker,5) {
    using namespace BlockerTest;
    const int f=Unsorted::CurrentFrame;
    if(f<28) return 0;
    if(stage==15 || stage==16) {
        for(auto anim:AnimClass::Array) {
            if(!anim->IsInert || anim->Owner!=gun->Owner || _stricmp(anim->Type->ID,"PIFFPIFF") ||
               !seenShieldAnims.insert(anim->UniqueID).second) continue;
            ++shieldAnims;
            auto center=guard->GetCoords(),at=anim->Location;
            check(at.X<center.X && std::abs(center.DistanceFrom(at)-512)<=2,"absorbed shot animates on incoming sphere face");
        }
    }
    auto owner=HouseClass::CurrentPlayer;
    auto enemy=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    if(GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES) {
        cover=cell(site)->GetBuilding();target=cell(rear)->GetBuilding();
        if(exploded) {
            check(cover && victim==cover && cover->Health<cover->Type->Strength && point.X==site.X*256 &&
                point.Z>cover->Location.Z+4*Unsorted::LevelHeight && point.Z<=cover->Location.Z+7*Unsorted::LevelHeight &&
                target->Health==target->Type->Strength,"restored missile hit ordinary building");
            TerminateProcess(GetCurrentProcess(),0);
        }
        if(f>1200) check(false,"restored projectile timeout");
        return 0;
    }
    if(f==28) {
        MapClass::Instance.Reveal(owner);
        // Remove the range's other tanks with this test weapon before placing
        // the targets. Their independent shots would change the guard's HP.
        auto gunType=UnitTypeClass::Find("MTNK");
        for(int i=UnitClass::Array.Count-1;i>=0;--i)
            if(auto unit=UnitClass::Array.Items[i];unit->Type==gunType) unit->UnInit();
        target=place("PBREAR",enemy,rear);
        cover=place("PBWALL",enemy,site);
        gun=static_cast<TechnoClass*>(gunType->CreateObject(owner));
        check(gun && gun->Unlimbo(cell(gunCell)->GetCoords(),DirType::North),"placed gun");
        gun->ForceMission(Mission::Sleep);
        target->DiscoveredBy(owner);
        const auto spec=EntityWalls::rules(cover->Type);
        check(spec && !spec->blocksProjectiles && !spec->highWall,"omitted physical wall flags allow aim");
        stage=1;fire(0);
    } else if(exploded) {
        auto origin=gun->GetCoords();origin.Z+=48;
        switch(stage) {
        case 1:
            check(victim==cover && point.X==site.X*256 && cover->Health<coverHP && target->Health==targetHP &&
                gun->Target==target,"legal shot hit front face");
            replace("PBLOW",enemy);stage=2;fire(1);
            break;
        case 2:
            check(victim==target && cover->Health==coverHP && target->Health<targetHP,"real arc cleared low wall");
            replace("PBWALL",enemy);stage=3;
            origin=cover->GetCoords();origin.Z+=1000;
            launch("PBFlatGun",cell(site),origin,{0,0,-200});
            break;
        case 3:
            check(victim==cover && point.Z==cover->Location.Z+4*Unsorted::LevelHeight && cover->Health<coverHP,
                "vertical shot hit roof");
            replace("PBWALL",owner);stage=4;fire(0);
            break;
        case 4:
            check(victim==target && cover->Health==coverHP && target->Health<targetHP,"ally filter");
            replace("PBSOLID",enemy);stage=5;
            launch("PBInstantGun",target,origin,{200,0,0});
            break;
        case 5: {
            check(victim==target && cover->Health==coverHP && target->Health<targetHP,"instant shot passed");
            replace("PBHIGH",enemy);
            auto w=gun->GetWeapon(0)->WeaponType;
            check(EntityWalls::blocksAttack(gun,target,w) && gun->GetFireError(target,0,false)==FireError::ILLEGAL,"explicit high wall wins");
            replace("PBGATE",enemy);
            check(EntityWalls::blocksAttack(gun,target,w) && !EntityWalls::blocksAttack(gun,cover,w),"explicit wall gates");
            replace("PBWALL",enemy);
            bystander=place("PBREAR",enemy,{69,67});
            w->Warhead->CellSpread=1.75f;
            stage=6;fire(0);
            break;
        }
        case 6: {
            check(victim==cover && cover->Health<coverHP && bystander->Health<bystander->Type->Strength &&
                target->Health==targetHP,"original warhead splashed beside contact");
            replace("PBWALL",owner);
            guard=static_cast<TechnoClass*>(UnitTypeClass::Find("HTNK")->CreateObject(enemy));
            check(guard && guard->Unlimbo(cell({70,68})->GetCoords(),DirType::North),"placed shield guard");
            guard->ForceMission(Mission::Sleep);
            stage=8;exploded=false;started=f;
            break;
        }
        case 9:
            check(guard->Health==970 && !rearTestHits && splashTestHits && bystander->Health<splashHP &&
                  std::abs(point.X-(guard->Location.X-512))<32,"shield 0.3 deals 30 once and splashes at entry");
            stage=10;launch("PBInstantGun",target,origin,{200,0,0});
            break;
        case 10:
            check(guard->Health==940 && !rearTestHits,"opt-in Inviso deals 30 to guard");
            gun->GetWeapon(0)->WeaponType->Warhead->CellSpread=0;
            guard->Health=guard->EstimatedHealth=600;stage=11;exploded=false;started=f;
            break;
        case 12:
            check(guard->Health==600 && rearTestHits==1 && target->Health<targetHP,"Inviso passes default shield");
            stage=13;fire(0);
            break;
        case 13:
            check(guard->Health==500 && !rearTestHits,"default multiplier deals 100 once");
            gun->GetWeapon(0)->WeaponType->Warhead->CellSpread=2.5f;
            guard->Health=guard->EstimatedHealth=300;stage=14;exploded=false;started=f;
            break;
        case 7:
            check(false,"missile detonated before save");
            break;
        }
    }
    if(stage==8 && f==started+2) {
        check(!skills.contains("invalidshield") && ProjectileShield::configured &&
            std::any_of(ProjectileShield::active.begin(),ProjectileShield::active.end(),[](const auto& p){
                return p.unit==guard && p.rules.radius==2 && p.rules.heightOffset==0.25 &&
                    p.rules.blockInviso && p.rules.damageMultiplier==0.3;
            }),"registered active shield and rejected negative multiplier");
        auto weapon=gun->GetWeapon(0)->WeaponType;
        auto probe=weapon->Projectile->CreateBullet(target,gun,100,weapon->Warhead,weapon->Speed,false);
        probe->SetWeaponType(weapon);
        auto center=guard->GetCoords();
        center.Z+=64; // PartialShield.HeightOffset is a quarter of a cell.
        auto left=center,right=center;left.X-=768;right.X+=768;
        check(ProjectileShield::first(probe,left,right).guard==guard &&
            !ProjectileShield::first(probe,center,right).guard,"crossing enters shield and outgoing shot passes");
        left.Z+=513;right.Z+=513;
        check(!ProjectileShield::first(probe,left,right).guard,"overhead shot clears sphere");
        probe->UnInit();
        weapon->Warhead->CellSpread=2.5f;
        splashHP=bystander->Health;
        stage=9;fire(0);
    } else if(stage==11 && f==started+2) {
        auto origin=gun->GetCoords();origin.Z+=48;
        stage=12;launch("PBInstantGun",target,origin,{200,0,0});
    } else if(stage==14 && f==started+2) {
        splashHP=bystander->Health;stage=15;fire(0);
    } else if(stage==15 && f==started+15) {
        check(!exploded && guard->Health==300 && !rearTestHits && !splashTestHits,
            "zero absorbs direct hit splash and detonation");
        check(shieldAnims==1,"absorbed physical shot plays one warhead animation");
        auto origin=gun->GetCoords();origin.Z+=48;
        stage=16;launch("PBInstantGun",target,origin,{200,0,0});
    } else if(stage==16 && f==started+15) {
        check(!exploded && guard->Health==300 && !rearTestHits && !splashTestHits,
            "zero absorbs Inviso");
        check(shieldAnims==1,"absorbed Inviso shot plays one warhead animation");
        savedGun=gun->GetWeapon(0)->WeaponType;
        savedGun->Warhead->CellSpread=0;
        stage=17;fireBeam(0,true);
    } else if(stage==17 && f==started+5) {
        check(!exploded && guard->Health==300 && !rearTestHits,"zero shield absorbs laser damage");
        guard->Health=guard->EstimatedHealth=1000;
        stage=18;started=f;
    } else if(stage==18 && f==started+2) {
        stage=19;fireBeam(0,true);
    } else if(stage>=19 && stage<=21 && f==started+5) {
        check(exploded && guard->Health==1000-30*(stage-18) && !rearTestHits,"beam damages guard once at configured multiplier");
        ++stage;
        if(stage<=21) fireBeam(stage-19,true);
        else {guard->Health=guard->EstimatedHealth=600;started=f;}
    } else if(stage==22 && f==started+2) {
        stage=23;fireBeam(0,false);
    } else if(stage>=23 && stage<=25 && f==started+5) {
        check(exploded && guard->Health==600 && rearTestHits==1,"unblocked beam damages original target");
        if(++stage<=25) {fireBeam(stage-23,false);return 0;}
        gun->GetWeapon(0)->WeaponType->IsRadBeam=false;
        guard->Health=guard->EstimatedHealth=1000;
        started=f;stage=26;
    } else if(stage==26 && f==started+2) {
        stage=27;orbitalBeam(0);
    } else if(stage>=27 && stage<=29 && f==started+5) {
        check(exploded && point==guard->GetCoords()+CoordStruct{0,0,576} &&
            guard->Health==1000-30*(stage-26),"overhead damage matches the shield contact");
        ++stage;
        if(stage<=29) {orbitalBeam(stage-27);return 0;}
        auto slant=WeaponTypeClass::Find("PBSlant");
        check(slant && FireOrigin::enabled(slant),"read slanted origin");
        gun->GetWeapon(0)->WeaponType=slant;
        gun->SetTarget(guard);gun->RearmTimer.Stop();
        auto missile=gun->Fire(guard,0);
        check(missile && missile->SourceCoords==guard->GetTargetCoords()+CoordStruct{-512,0,768} &&
            missile->Target==guard && missile->Owner==gun,"homing missile keeps original target and firer at slanted launch");
        missile->UnInit();
        guard->UnInit();guard=nullptr;
        auto ballistic=BulletTypeClass::Find("Ballistic");
        check(ballistic && ballistic->Arcing,"stock ballistic projectile exists");
        slant->Projectile=ballistic;
        fire(0);
        bool launched=false;
        for(auto shot:BulletClass::Array) if(shot->Owner==gun && shot->WeaponType==slant &&
            shot->Target==target && shot->SourceCoords==target->GetTargetCoords()+CoordStruct{-512,0,768})
            launched=true;
        check(launched,"stock arcing shot launches from slanted origin");
    } else if(stage==30 && exploded) {
        check(victim==target && target->Health<targetHP,"stock arcing shot reaches original aim");
        gun->GetWeapon(0)->WeaponType->Projectile=BulletTypeClass::Find("PBMissile");
        gun->GetWeapon(0)->WeaponType=savedGun;
        gun->GetWeapon(0)->WeaponType->Warhead->CellSpread=0;
        replace("PBSOLID",owner);
        target->Health=target->Type->Strength;
        auto origin=gun->GetCoords();origin.Z+=48;
        stage=7;launch("PBMissileGun",target,origin,{25,0,0});
    }
    if(stage==7 && !exploded && f==started+2) {
        std::ofstream("blocker-missile.txt")<<missileID;
        check(ScenarioClass::SaveGame("Saved Games\\Blocker.sav",L"Projectile blocker"),"saved homing missile");
        TerminateProcess(GetCurrentProcess(),0);
    }
    if(f-started>220) check(false,"timeout waiting for impact");
    return 0;
}
