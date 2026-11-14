#include "../../../src/Starfall.cpp"
#include <TacticalClass.h>
#include <Surface.h>
#include <WWMouseClass.h>
#include "../WindowMouse.h"
EXPORT_FUNC(SF_ResearchDraw);
namespace {
int strikes=0;
std::set<DWORD> shotOwners;
void check(bool ok,const char* label) {
    std::ofstream("active.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
bool input(Point2D p,int flags) {
    return WindowMouse::button(p,flags);
}
void click(Point2D p) {check(input(p,1) && input(p,4),"click captured");}
Point2D button(int slot) {
    auto v=DSurface::ViewBounds;return {v.X+25+slot*70,v.Y+v.Height-30};
}
Point2D screen(CoordStruct at) {
    auto tactical=TacticalClass::Instance;
    auto screen=tactical->CoordsToScreen(at);
    auto view=DSurface::ViewBounds;
    tactical->TacticalPos={screen.X-view.Width/2,screen.Y-view.Height/2};
    auto p=tactical->CoordsToClient(at).first;
    return {view.X+p.X,view.Y+p.Y};
}
}
DEFINE_HOOK(0x4690D4,SF_TestActiveImpact,6) {
    auto b=R->ESI<BulletClass*>();
    if(applyingDamage && b->WH==WarheadTypeClass::Find("HE")) ++strikes;
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestActiveOrders,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(frame<18 || (frame>100 && !loading)) return 0;
    auto h=HouseClass::CurrentPlayer;UnitClass *tank=nullptr,*v3=nullptr;
    for(auto u:UnitClass::Array) if(u->Owner==h) {
        if(!strcmp(u->Type->ID,"MTNK") && u->GetMapCoords().X==59) tank=u;
        if(!strcmp(u->Type->ID,"V3") && u->GetMapCoords().Y==61) v3=u;
    }
    if(!tank || !v3) check(false,"casters present");
    const auto& specs=ActiveSkills::specs;
    auto cd=[&](UnitClass* u,int slot){return ActiveSkills::remaining(u,specs.at(slot));};
    if(loading) {
        if(frame==65) check(cd(tank,0)>0 && !cd(v3,0) && cd(tank,1)>0 && cd(v3,1)>0,"cooldown after load");
        if(frame==124) {
            int before=0;std::ifstream("hits.txt")>>before;
            check(before>0 && strikes>0 && before+strikes==20,"resumed barrage");
            TerminateProcess(GetCurrentProcess(),0);
        }
        return 0;
    }
    if(frame==18) {
        Research::ensureReady();
        check(!ownsPassive(tank,"test_point") && !skills.contains("z_invalidcast"),"INI binding overrides legacy");
        grantToUnit(tank,PassiveGrant{{"test_point"},-1});
        check(specs.size()==6 && ActiveSkills::held(tank).size()==5 && ActiveSkills::held(v3).size()==2,"both skills bound");
        check(specs[0].name==L"区域轰炸" && specs[0].radius==3 && specs[0].range==16 &&
              skills.at("sf_activebarrage").scriptParams.at("warhead")=="HE","INI display and parameters");
        grantToUnit(v3,PassiveGrant{{"z_pointvariant"},-1});
        check(ActiveSkills::execute(h->ArrayIndex,5,v3->UniqueID,0,v3->GetMapCoords()) &&
              cd(v3,5)==77 && scriptTimers.state.at({v3->UniqueID,"z_pointvariant","value"}).integer==23,"shared script variant");
        removeHeldPassive(v3,"z_pointvariant");
        ObjectClass::CurrentObjects.Clear();v3->Select();tank->Select();
        check(ObjectClass::CurrentObjects.Count==2,"mixed selection");
        MapClass::Instance.Reveal(h);
        tank->IronCurtain(600,h,false);v3->IronCurtain(600,h,false);
        SF_ResearchDraw(nullptr);
        click(button(0));
        auto location=screen(tank->GetCoords());
        check(input(location,0x10) && input(location,0x40) && !cd(tank,0),"canceled without orders");
        check(!ActiveSkills::execute(h->ArrayIndex,0,tank->UniqueID,0,{76,80}),"outside cast range rejected");
        check(!ActiveSkills::execute(h->ArrayIndex,2,tank->UniqueID,0,{}),"missing unit rejected");
    }
    if(frame==22) {
        click(button(0));
        auto cell=tank->GetMapCoords();cell.X+=3;cell.Y+=2;
        auto p=screen(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge());click(p);
    }
    if(frame==36) {
        check(cd(tank,0)>0 && !cd(v3,0) && strikes>0,"single nearest caster");
        click(button(1));
        CellStruct aim{65,66};click(screen(MapClass::Instance.GetCellAt(aim)->GetCoordsWithBridge()));
    }
    if(frame>36 && frame<48) for(auto b:BulletClass::Array) {
        if(b->Owner && b->WeaponType==WeaponTypeClass::Find("120mm") && passiveBullets.contains(b->UniqueID) && b->Target) {
            CellStruct endpoint{};
            check(ActiveSkills::aim(specs[1],b->Owner,nullptr,{65,66},endpoint) &&
                CellClass::Coord2Cell(b->Target->GetCoords())==endpoint,"projectile endpoint");
            shotOwners.insert(b->Owner->UniqueID);
        }
    }
    if(frame==48) {
        check(cd(v3,1)>0 && cd(tank,1)>0 && shotOwners.contains(tank->UniqueID) && shotOwners.contains(v3->UniqueID) && ObjectClass::CurrentObjects.Count==2,"both directional shots");
        click(button(2));click(screen(v3->GetCenterCoords()));
    }
    if(frame==52) {
        check(cd(tank,2)>0,"unit target");
        click(button(3));
        click(screen(MapClass::Instance.GetCellAt(CellStruct{70,70})->GetCoordsWithBridge()));
        click(screen(MapClass::Instance.GetCellAt(CellStruct{61,64})->GetCoordsWithBridge()));
        click(button(4));click(button(4));
    }
    if(frame==60) {
        check(scriptTimers.state.at({tank->UniqueID,"test_point","value"}).integer==11,"independent skill parameters");
        const auto& point=scriptTimers.state.at({tank->UniqueID,"test_point","point"});
        check(point.fields.at("x").integer==61 && point.fields.at("y").integer==64,"exact clicked cell");
        check(scriptTimers.state.at({tank->UniqueID,"test_pulse","pulses"}).integer==1 && cd(tank,4)>0,"immediate cast once");
        std::ofstream("hits.txt")<<strikes;
        check(ScenarioClass::SaveGame("Saved Games\\Active.sav",L"Active Skills"),"saved");
        // Ordinary terrain must still accept a real move order after exiting skill mode.
        auto ground=screen(MapClass::Instance.GetCellAt(CellStruct{64,64})->GetCoordsWithBridge());
        int before=EventClass::OutList.Count;
        bool forwarded=!input(ground,1);
        forwarded &= !input(ground,4);
        if(EventClass::OutList.Count==before) {input(ground,16);input(ground,64);}
        bool mission=false;
        for(int i=before;i<EventClass::OutList.Count;++i) {
            auto type=EventClass::OutList[i].Type;
            mission|=type==EventType::MegaMission || type==EventType::MegaMissionF;
        }
        check(forwarded && mission,"ordinary terrain issues a native order");
        check(specs[0].interruptOrders && !specs[5].interruptOrders,"interrupt settings parsed");
        grantToUnit(v3,PassiveGrant{{"z_pointvariant"},-1});
        counts[v3->UniqueID][specs[5].id].next=0;
        v3->SetTarget(tank);
        check(!ActiveSkills::execute(h->ArrayIndex,5,v3->UniqueID,0,{0,0}) && v3->Target==tank,"invalid cast preserves orders");
        check(ActiveSkills::execute(h->ArrayIndex,5,v3->UniqueID,0,v3->GetMapCoords()) && v3->Target==tank,"configured cast preserves orders");
        counts[v3->UniqueID][specs[5].id].next=0;
        ActiveSkills::specs[5].interruptOrders=true;
        v3->SetDestination(MapClass::Instance.GetCellAt(CellStruct{63,65}),true);
        check(ActiveSkills::execute(h->ArrayIndex,5,v3->UniqueID,0,v3->GetMapCoords()) && !v3->Target && !v3->Destination && !v3->NavQueue.Count,"default cast interrupts attack and movement");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
