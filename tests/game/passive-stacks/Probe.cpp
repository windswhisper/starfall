#include "../../../src/Starfall.cpp"
#include <WWMouseClass.h>
#include <CCToolTip.h>
#include <BitFont.h>
namespace {
void check(bool ok,const char* label) {
    std::ofstream("stacks.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void trigger(const char* name,TechnoClass* unit,TechnoClass* other) {
    capture(name,unit,other,unit,other,0);dispatch();
}
bool nativeHoverMatches(TechnoClass* unit,const std::wstring& expected) {
    auto tactical=TacticalClass::Instance;
    const auto camera=tactical->TacticalPos;
    const auto mouse=WWMouseClass::Instance->XY1;
    const auto view=DSurface::ViewBounds;
    auto location=unit->GetCoords();
    tactical->SetTacticalPosition(&location);
    // The engine builds the mouse hit list while drawing, including after a load.
    MapClass::Instance.MarkNeedsRedraw(2);
    GScreenClass::Instance.Render();
    const auto point=tactical->CoordsToClient(location).first;
    bool found=false;
    for(int y : {0,-6,-12,6}) {
        auto cursor=Point2D{view.X+point.X,view.Y+point.Y+y};
        WWMouseClass::Instance->XY1=cursor;
        const auto tip=MouseClass::Instance.GetToolTip(500);
        if(!tip || tip!=expected) continue;
        auto manager=CCToolTip::Instance;
        auto previous=manager->CurrentToolTip;
        auto data=manager->CurrentToolTipData;
        auto position=manager->CurrentMousePosition;
        ToolTip gadget;
        gadget.GadgetID=500;gadget.Text=nullptr;gadget.Bounds=view;
        manager->CurrentToolTip=&gadget;
        manager->CurrentMousePosition=cursor;
        bool processed=manager->Process();
        int width=0,height=0,nameHeight=0;
        BitFont::Instance->GetTextDimension(unit->GetUIName(),&width,&nameHeight,view.Width);
        BitFont::Instance->GetTextDimension(manager->CurrentToolTipData.HelpText,&width,&height,view.Width);
        found=processed && expected==manager->CurrentToolTipData.HelpText &&
              (expected.find(L'\n')==expected.npos || height>nameHeight);
        manager->CurrentToolTip=previous;
        manager->CurrentMousePosition=position;
        manager->CurrentToolTipData=data;
        break;
    }
    auto restore=camera;
    tactical->SetTacticalPosition(&restore);
    WWMouseClass::Instance->XY1=mouse;
    return found;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestPassiveStacks,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(frame!=20 && frame!=25 && frame!=28 && frame!=31 && frame!=46 && frame!=120 && frame!=172) return 0;
    if(loading && frame<=28) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    DWORD uid=0,pid=0,eid=0;
    if(frame!=20) {std::ifstream ids("stacks.ids");ids>>uid>>pid>>eid;}
    UnitClass *unit=nullptr,*peer=nullptr,*enemy=nullptr;
    for(auto item:UnitClass::Array) {
        if(item->Owner==house && !_stricmp(item->Type->ID,"MTNK")) {
            if(!unit && (!uid || item->UniqueID==uid)) unit=item;
            else if(!peer && (!pid || item->UniqueID==pid)) peer=item;
        }
        if(!enemy && item->Owner==foe && (!eid || item->UniqueID==eid)) enemy=item;
    }
    check(unit && peer && enemy,"units");
    const auto id=unit->UniqueID;int h=house->ArrayIndex;
    if(frame==20) {
        std::ofstream("stacks.ids")<<id<<" "<<peer->UniqueID<<" "<<enemy->UniqueID;
        HealthFormula formula;
        check(formula.compile("$(self.health/self.maxhealth+this.stack*5%)") &&
            std::abs(formula.evaluate(50,100,0,std::nullopt,2).value_or(-1)-0.6)<0.00001 &&
            !formula.compile("$(self.stack)"),"stack formula namespace");
        check(skills.contains("stackbuff") && !skills.contains("badstackmax") && !skills.contains("badstacktarget") && !skills.contains("badstackamount"),"configuration validation");
        Research::unlocked[h].insert("stackresearch");
        check(skills.contains("hoverrange") && skills.contains("hoverbonus") && !skills.contains("badhover"),"tooltip config validation");
        grantToUnit(unit,PassiveGrant{{"hoverrange"},150});
        changeStacks(id,"hoverrange",4,10);
        tickStates();
        const std::wstring name=unit->GetUIName();
        check(nativeHoverMatches(unit,name+L"\n射程+5"),"native hover five stacks");
        check(nativeHoverMatches(peer,peer->GetUIName()),"native hover peer unchanged");
        changeStacks(id,"hoverrange",-4,10);
        tickStates();
        check(nativeHoverMatches(unit,name),"native hover inactive condition");
        changeStacks(id,"hoverrange",2,10);
        grantToUnit(unit,PassiveGrant{{"hoverbonus"},5});
        tickStates();
        std::wstring text=unitHoverText(unit,name.c_str());
        check(text.starts_with(name+L"\n") && text.find(L"\n射程+3")!=text.npos &&
              text.find(L"\nBonus 100.00% / 0.50")!=text.npos && nativeHoverMatches(unit,text),"native hover combined formulas");
        grantToUnit(unit,PassiveGrant{{"stackbuff"},20});
        grantToUnit(unit,PassiveGrant{{"stackgain","stackvictim","stackreduce","stackstrip","selfgrowth"},-1});
        grantToUnit(peer,PassiveGrant{{"stackgain"},-1});
        check(unitStacks(unit,"stackbuff")==1 && unitStacks(peer,"stackbuff")==1,"initial one");
        for(int n=0;n<4;++n) trigger("onkill",unit,enemy);
        tickStates();
        check(unitStacks(unit,"stackbuff")==3 && unitStacks(peer,"stackbuff")==1 &&
            std::abs(unit->FirepowerMultiplier-1.15)<0.00001,"cap and unit isolation");
        check(unitStacks(enemy,"stackbuff")==0,"absent target skipped");
        grantToUnit(enemy,PassiveGrant{{"stackbuff"},5});
        trigger("onkill",unit,enemy);
        check(unitStacks(enemy,"stackbuff")==2 && unitStacks(unit,"stackbuff")==3,"modify another unit");
        grantToUnit(enemy,PassiveGrant{{"deathstack","markdeathstack"},-1});
        trigger("onfire",enemy,unit);trigger("onfire",enemy,unit);
        int beforeDeath=Research::balance(foe->ArrayIndex);
        queueDeath(enemy,unit);passiveStacks.erase(enemy->UniqueID);dispatch();
        check(Research::balance(foe->ArrayIndex)==beforeDeath+30,"death stack snapshot");
        trigger("ondeploy",unit,enemy);tickStates();
        check(unitStacks(unit,"stackbuff")==1 && std::abs(unit->FirepowerMultiplier-1.05)<0.00001,"decrement and formula");
        trigger("ondeploy",unit,enemy);tickStates();
        check(unitStacks(unit,"stackbuff")==1 && std::abs(unit->FirepowerMultiplier-1.05)<0.00001,"research layer survives excess reduction");
        trigger("onkill",unit,enemy);
        trigger("onkill",peer,enemy);
        check(unitStacks(peer,"stackbuff")==2,"peer stack");
        check(peer->SetOwningHouse(foe,false),"owner change");
        reconcileUnitStacks(peer);
        check(unitStacks(peer,"stackbuff")==0,"lost research clears stack");
        check(peer->SetOwningHouse(house,false),"owner restored");
        Research::points[h]=0;
        trigger("onfire",unit,enemy);trigger("onfire",unit,enemy);
        check(unitStacks(unit,"selfgrowth")==3 && Research::balance(h)==30 && std::abs(unit->ArmorMultiplier-1.03)<0.00001,"event stack snapshot");
    }
    if(frame==25) {
        Research::unlocked[h].erase("stackresearch");reconcileAllStacks();
        check(unitStacks(unit,"stackbuff")==2,"overlapping source retained");
        check(skills.contains("steallist") && skills.contains("striplist") &&
              !skills.contains("invalidstealcount") && !skills.contains("invalidsteallist"),"steal list validation");
        grantToUnit(enemy,PassiveGrant{{"loota","lootb","striplist"},15});
        changeStacks(enemy->UniqueID,"loota",4,4);
        grantToUnit(peer,PassiveGrant{{"steallist"},-1});
        trigger("ondealdamage",peer,enemy);
        auto receiver=peer->UniqueID,donor=enemy->UniqueID;
        check(unitStacks(peer,"loota")==2 && unitStacks(enemy,"loota")==2 &&
              ownsPassive(enemy,"loota") && ownsPassive(peer,"lootb") && !ownsPassive(enemy,"lootb"),"partial and single stack theft");
        check(std::any_of(pending.begin(),pending.end(),[&](const Event& e) {
            return e.holder==receiver && e.trigger=="onpassivegained" && e.stackSnapshot.at("loota")==2;
        }),"acquisition sees stolen stacks");
        changeStacks(receiver,"loota",1,4);
        trigger("ondealdamage",peer,enemy);
        trigger("ondealdamage",peer,enemy);
        check(unitStacks(peer,"loota")==4 && unitStacks(enemy,"loota")==1 &&
              triggerTotals[receiver]["steallist"]==2,"steal respects capacity");
        changeStacks(receiver,"loota",-1,4);
        trigger("ondealdamage",peer,enemy);
        check(unitStacks(peer,"loota")==4 && !ownsPassive(enemy,"loota") &&
              ownsPassive(enemy,"striplist"),"last stack removes only matching passive");
    }
    if(frame==28) {
        capture("onfire",unit,enemy,unit,enemy,0);
        passiveStacks[id]["selfgrowth"]=3;
        check(ScenarioClass::SaveGame("Saved Games\\PassiveStacks.sav",L"PassiveStacks"),"saved");
    }
    if(frame==31) {
        check(ownsPassive(peer,"loota") && ownsPassive(peer,"lootb") && unitStacks(peer,"loota")==4 &&
              !ownsPassive(enemy,"loota") && !ownsPassive(enemy,"lootb"),"transferred ownership retained");
        // Capture a defensive event with the opposing unit as its attacker.
        capture("ontakedamage",enemy,peer,peer,enemy,5);dispatch();
        check(!ownsPassive(peer,"loota") && !ownsPassive(peer,"lootb") &&
              unitStacks(peer,"loota")==0 && ownsPassive(peer,"steallist"),"batch remove skips absent and clears all matches");
        tickStates();
        check(unitStacks(unit,"stackbuff")==2 && std::abs(unit->FirepowerMultiplier-1.10)<0.00001,"stack retained");
        check(unitStacks(unit,"selfgrowth")==5 && Research::balance(h)==60 && std::abs(unit->ArmorMultiplier-1.06)<0.00001,"queued stack snapshot retained");
    }
    if(frame==46) {
        check(unitStacks(unit,"stackbuff")==0,"last source expired");
        grantToUnit(unit,PassiveGrant{{"stackbuff"},5});
        check(unitStacks(unit,"stackbuff")==1,"reacquire starts one");
        trigger("onkill",unit,enemy);
        check(unitStacks(unit,"stackbuff")==2,"reacquire can gain");
        trigger("ondeploy",unit,enemy);
        tickStates();
        check(!ownsPassive(unit,"stackbuff") && std::abs(unit->FirepowerMultiplier-1.0)<0.00001,"exact reduction removes passive and modifier");
        trigger("onkill",unit,enemy);
        check(!ownsPassive(unit,"stackbuff"),"stack gain cannot recreate a removed passive");
        grantToUnit(unit,PassiveGrant{{"stackbuff"},5});
        trigger("ondeploy",unit,enemy);
        check(!ownsPassive(unit,"stackbuff"),"excess reduction removes ordinary base layer");
        grantToUnit(unit,PassiveGrant{{"stackbuff"},5});
        trigger("onpromote",unit,enemy);
        check(unitStacks(unit,"stackbuff")==0,"explicit removal clears");
        grantToUnit(unit,PassiveGrant{{"stackbuff"},5});
        check(unitStacks(unit,"stackbuff")==1,"explicit reacquire starts one");
    }
    if(frame==120) {
        // The native tooltip is suppressed during the initial load-screen transition.
        check(nativeHoverMatches(unit,std::wstring(unit->GetUIName())+L"\n射程+3"),"native hover retained");
    }
    if(frame==172) {
        check(nativeHoverMatches(unit,unit->GetUIName()),"native hover expired");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
