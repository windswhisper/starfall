#include "../../../src/Starfall.cpp"
#include <DisplayClass.h>
#include <chrono>
namespace {
void check(bool ok,const char* label) {
    std::ofstream("sequence.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
#include "NearbyProbe.inc"
void testDamage(HouseClass* owner) {
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    auto spawn=[&](HouseClass* house,short x){
        auto u=static_cast<InfantryClass*>(InfantryTypeClass::Find("E1")->CreateObject(house));
        check(u && u->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{x,82})->GetCoords(),DirType::North),"infantry placed");
        return u;
    };
    auto self=spawn(owner,61), enemy=spawn(foe,62), neutral=spawn(HouseClass::FindNeutral(),63);
    Objects live;
    for(auto u:{self,enemy,neutral}) live.emplace(u->UniqueID,u);
    auto previous=activeObjects;activeObjects=&live;
    {
        Skill mark{};mark.id="ownerlessmark";skills[mark.id]=mark;
        auto wh=WarheadTypeClass::Find("AP");
        const auto previousRules=grantRules;
        WarheadGrantRule rule{};rule.ids={mark.id};rule.houses={"self"};grantRules[wh]=rule;
        PassiveAction blast{};blast.action="detonatewarhead";blast.target="victim";blast.warhead=wh;
        Event event{};event.owner=owner->ArrayIndex;event.victim=self->UniqueID;event.origin=self->Location;
        check(apply(blast,event,nullptr,live,0) && ownsPassive(self,mark.id),"ownerless detonation retains Self house");
        event.victim=enemy->UniqueID;
        check(apply(blast,event,nullptr,live,0),"ownerless enemy detonation");
        check(!ownsPassive(enemy,mark.id),"ownerless enemy excluded by Self filter");
        check(passiveBulletOwners.empty(),"ownerless projectile cleanup");
        removeHeldPassive(self,mark.id);skills.erase(mark.id);grantRules=previousRules;
    }
    auto hit=[&](const char* section,TechnoClass* victim){
        Skill skill{};skill.id=section;skill.trigger="onfire";
        auto fields=ActionSequence::fields({},section);
        check(readAction(skill,section,&fields),"INI damage parsed");
        auto event=eventSnapshot("onfire",self,victim,self,victim,0);
        return apply(skill.actions.front(),event,self,live,0);
    };
    int hp=enemy->Health,ownHp=self->Health;
    check(hit("sfdamagetest",enemy) && enemy->Health==hp-20 && self->Health==ownHp,"50 percent verses on one target");
    const auto allies=owner->Allies;owner->Allies.Add(foe);
    check(!hit("sfdamagetest",enemy) && enemy->Health==hp-20,"allies excluded");
    owner->Allies=allies;
    check(!hit("sfdamagetest",neutral) && !hit("sfflametest",enemy),"neutral and enemy flags");
    check(hit("sfdamagetest",self) && self->Health==ownHp-20,"owner override");
    auto death=[&](const char* section,InfantryClass* victim,AnimTypeClass* animation) {
        auto count=[&] {int n=0;for(auto a:AnimClass::Array) if(a->Type==animation) ++n;return n;};
        int before=count();victim->Health=victim->EstimatedHealth=1;
        return hit(section,victim) && animation && count()>before;
    };
    check(death("sfdamagetest",enemy,RulesClass::Instance->InfantryExplode),"InfDeath 3 explosion");
    check(death("sfflametest",self,RulesClass::Instance->FlamingInfantry),"InfDeath 4 flames");
    activeObjects=previous;
}
int watchedSound=-1;
vector<CoordStruct> sounds;
void testPresentation(TechnoClass* caster,TechnoClass* victim) {
    check(skills.contains("presentationprobe") && !skills.contains("invalidpresentation"),"presentation config");
    auto p=caster->GetMapCoords();++p.X;
    auto point=MapClass::Instance.GetCellAt(p)->GetCoordsWithBridge();
    int animCount=AnimClass::Array.Count,smokeCount=ParticleSystemClass::Array.Count;
    auto& localIni=CCINIClass::INI_RA2MD;
    bool hide=localIni.ReadBool("Phobos","HideShakeEffects",false);
    localIni.WriteBool("Phobos","HideShakeEffects",false);
    auto& screen=GScreenClass::Instance;
    screen.ScreenShakeX=screen.ScreenShakeY=0;
    auto& camera=TacticalClass::Instance->TacticalPos;
    auto previous=camera;camera={-10000,-10000};
    watchedSound=VocClass::FindIndex("GrizzlyTankAttack");sounds.clear();
    grantToUnit(caster,PassiveGrant{{"presentationprobe"},-1});
    capture("onfire",caster,victim,caster,victim,0);dispatch();
    camera=previous;watchedSound=-1;
    check(screen.ScreenShakeX==5 && screen.ScreenShakeY==7,"global shake and offscreen local shake");
    check(sounds.size()==2 && sounds.front()==victim->GetCoords() && sounds.back()==point,"native positional sound from INI and Lua");
    bool attached=false,onPoint=false;
    for(int i=animCount;i<AnimClass::Array.Count;++i) {
        auto a=AnimClass::Array.Items[i];
        if(a->Type!=AnimTypeClass::Find("EXPLOSML") || !a->IsInert || a->RemainingIterations!=1) continue;
        attached|=a->OwnerObject==caster;
        onPoint|=!a->OwnerObject && a->Location==point;
    }
    check(attached && onPoint,"one-shot unit and point animations");
    bool atVictim=false,atPoint=false;
    for(int i=smokeCount;i<ParticleSystemClass::Array.Count;++i) {
        auto system=ParticleSystemClass::Array.Items[i];
        if(system->Type!=ParticleSystemTypeClass::Find("SmallGreySSys") || system->OwnerHouse!=caster->Owner) continue;
        atVictim|=system->GetMapCoords()==CellClass::Coord2Cell(victim->GetCoords());
        atPoint|=system->GetMapCoords()==p;
    }
    check(atVictim && atPoint,"particle systems at both locations");
    auto shake=skills.at("presentationprobe").actions[3];
    shake.shakeLocal=true;
    camera=TacticalClass::CoordsToScreen(point);
    camera.X-=DSurface::ViewBounds.Width/2;camera.Y-=DSurface::ViewBounds.Height/2;
    screen.ScreenShakeX=screen.ScreenShakeY=0;
    Event e=eventSnapshot("onfire",caster,victim,caster,victim,0);
    Objects live{{caster->UniqueID,caster}};
    check(apply(shake,e,caster,live,0,std::nullopt,p) && screen.ScreenShakeY==7,"visible local shake");
    localIni.WriteBool("Phobos","HideShakeEffects",true);
    screen.ScreenShakeX=screen.ScreenShakeY=0;shake.shakeLocal=false;
    check(apply(shake,e,caster,live,0,std::nullopt,p) && !screen.ScreenShakeY,"local preference preserves action success");
    localIni.WriteBool("Phobos","HideShakeEffects",hide);camera=previous;
    const auto& flash=skills.at("presentationprobe").actions[0];
    check(apply(flash,e,nullptr,live,0) && !AnimClass::Array.Items[AnimClass::Array.Count-1]->OwnerObject &&
          AnimClass::Array.Items[AnimClass::Array.Count-1]->Location==e.origin,"death position without owner object");
    Skill directed{};directed.id="rail";directed.trigger="onfire";
    ScriptFields fields{{"action","spawnparticlesystem"},{"action.particlesystem","SmallRailgunSys"},{"action.target","victimlocation"}};
    check(readAction(directed,"rail",&fields),"directed particle config");
    auto rail=directed.actions.front();
    e.origin=point;e.impact=point;
    check(!apply(rail,e,nullptr,live,0),"zero-length particle path skipped");
    e.origin.X-=512;
    check(apply(rail,e,nullptr,live,0),"directed particle without caster");
    auto system=ParticleSystemClass::Array.Items[ParticleSystemClass::Array.Count-1];
    check(system->Type==rail.particleSystem && system->Target && system->OwnerHouse==caster->Owner,"directed particle target and owner");
    screen.ScreenShakeX=screen.ScreenShakeY=0;
}
void placeBuilding(const char* id,HouseClass* owner) {
    auto type=BuildingTypeClass::Find(id);
    auto building=static_cast<BuildingClass*>(type->CreateObject(owner));
    for(short y=76;y<88;y+=3) for(short x=47;x<65;x+=3) {
        CellStruct cell{x,y};
        if(!type->CanPlaceHere(&cell,owner)) continue;
        if(building->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoords(),DirType::North)) {
            building->QueueMission(Mission::Guard,false);
            return;
        }
    }
    check(false,id);
}
void selectPoint(SuperClass* sw,CellStruct point,const char* label) {
    Unsorted::CurrentSWType=sw->Type->ArrayIndex;
    auto cell=MapClass::Instance.GetCellAt(point);
    int before=EventClass::OutList.Count;
    DisplayClass::Instance.LeftMouseButtonUp(cell->GetCoords(),point,nullptr,sw->Type->Action,0);
    bool selected=false;
    for(int i=before;i<EventClass::OutList.Count;++i) {
        auto& event=EventClass::OutList[i];
        selected|=event.Type==EventType::SpecialPlace && event.SpecialPlace.ID==sw->Type->ArrayIndex && event.SpecialPlace.Location==point;
    }
    std::ofstream("sequence.log",std::ios::app)<<"queued frame "<<Unsorted::CurrentFrame<<" id "<<sw->Type->ArrayIndex<<" owner "<<sw->Owner->ArrayIndex<<" playable "<<ActiveSkills::playable(point)<<" house observer "<<sw->Owner->IsObserver()<<" defeated "<<sw->Owner->Defeated<<"\n";
    check(selected,label);
}
}
DEFINE_HOOK(0x7509E0,SF_TestPresentationSound,5) {
    if(watchedSound>=0 && R->ECX<int>()==watchedSound) sounds.push_back(*R->EDX<CoordStruct*>());
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestSequence,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int f=Unsorted::CurrentFrame;
    bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    auto h=HouseClass::CurrentPlayer;
    if(f<20) return 0;
    auto a=SuperWeaponTypeClass::Find("SF_Reinforcements"),b=SuperWeaponTypeClass::Find("SF_GravityWell");
    static int initialMoney=0;
    static bool firstChecked=false,saved=false;
    if(f==20 && !load) {
        for(auto weapon:WeaponTypeClass::Array) weapon->Damage=0;
        Research::ensureReady();
        std::ofstream("sequence.log",std::ios::app)<<"Ares "<<bool(GetModuleHandleA("Ares.dll"))<<" Phobos "<<bool(GetModuleHandleA("Phobos.dll"))<<" types "<<a<<" "<<b<<" definitions "<<ActionSequence::definitions.size()<<"\n";
        check(a && b && a!=b && a->Type==ActionSequence::type && b->Type==ActionSequence::type &&
            ActionSequence::definitions.at(lower(a->ID)).size()==6 && ActionSequence::definitions.at(lower(b->ID)).size()==1,"two sequence types");
        placeBuilding("GAPOWR",h);
        placeBuilding("GAPILE",h);
    }
    if(f==100 && !load) {
        for(auto type:{a,b}) {
            auto sw=h->Supers.Items[type->ArrayIndex];
            std::ofstream("sequence.log",std::ios::app)<<"provider "<<type->ID<<" present "<<sw->IsPresent<<" suspended "<<sw->IsSuspended<<" powered "<<type->IsPowered
                <<" slots "<<BuildingTypeClass::Find("GAPILE")->SuperWeapon<<","<<BuildingTypeClass::Find("GAPILE")->SuperWeapon2<<"\n";
            check(sw->IsPresent && !sw->IsSuspended,"native provider grant");
            sw->SetReadiness(true);
        }
        testDamage(h);
        MapClass::Instance.Reveal(h);
        initialMoney=h->Available_Money();
        selectPoint(h->Supers.Items[a->ArrayIndex],{42,74},"first selected index");
        selectPoint(h->Supers.Items[a->ArrayIndex],{42,74},"duplicate command queued");
    }
    if(!load && !firstChecked && (ActionSequence::lastCast>=1 || f==160)) {
        firstChecked=true;
        auto tank=MapClass::Instance.GetCellAt(CellStruct{42,74})->GetUnit(false);
        auto first=h->Supers.Items[a->ArrayIndex];
        std::ofstream("sequence.log",std::ios::app)<<"cast "<<ActionSequence::lastCast<<" money "<<initialMoney<<" -> "<<h->Available_Money()
            <<" tank "<<tank<<" present "<<first->IsPresent<<" ready "<<first->IsReady<<" suspended "<<first->IsSuspended<<"\n";
        check(tank && tank->Owner==h && tank->Type==UnitTypeClass::Find("MTNK") && h->Available_Money()==initialMoney+500,"reinforcement and credits");
        testPresentation(tank,h->Buildings.Items[0]);
        auto well=h->Supers.Items[b->ArrayIndex];
        check(!h->Supers.Items[a->ArrayIndex]->IsReady && well->IsReady,"independent charge");
        auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
        auto target=static_cast<UnitClass*>(UnitTypeClass::Find("MTNK")->CreateObject(foe));
        check(target->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{45,64})->GetCoords(),DirType::North),"target placed");
        target->Deactivate();target->Health=target->EstimatedHealth=2000;
        std::ofstream("sequence.id")<<target->UniqueID;
        selectPoint(well,{46,64},"second selected index");
    }
    if(!load && !saved && (ActionSequence::lastCast>=2 || f==280)) {
        saved=true;
        check(ActionSequence::casts.size()==1 && ActionSequence::timers.queue.size()==1,"one pending launch");
        const char* file=GetModuleHandleA("CnCNet-Spawner.dll")?"Sequence.sav":"Saved Games\\Sequence.sav";
        check(ScenarioClass::SaveGame(file,L"ActionSequence"),"saved after launch");
    }
    if(f==320) {
        DWORD id=0;std::ifstream("sequence.id")>>id;UnitClass* victim=nullptr;
        for(auto u:UnitClass::Array) if(u->UniqueID==id) victim=u;
        check(victim && victim->Health==1970,"delayed damage");
        check(ActionSequence::casts.empty() && ActionSequence::timers.queue.empty(),"retired cast");
        if(!load) testNearby(h);
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
