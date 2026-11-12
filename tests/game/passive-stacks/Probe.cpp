#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* label) {
    std::ofstream("stacks.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void trigger(const char* name,TechnoClass* unit,TechnoClass* other) {
    capture(name,unit,other,unit,other,0);dispatch();
}
}
DEFINE_HOOK(0x55B4EB,SF_TestPassiveStacks,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(frame!=20 && frame!=25 && frame!=28 && frame!=31 && frame!=46) return 0;
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
        grantToUnit(unit,PassiveGrant{{"stackbuff"},20});
        grantToUnit(unit,PassiveGrant{{"stackgain","stackvictim","stackreduce","stackstrip","selfgrowth"},-1});
        grantToUnit(peer,PassiveGrant{{"stackgain"},-1});
        check(stackCount(id,"stackbuff")==0 && stackCount(peer->UniqueID,"stackbuff")==0,"initial zero");
        for(int n=0;n<4;++n) trigger("onkill",unit,enemy);
        tickStates();
        check(stackCount(id,"stackbuff")==3 && stackCount(peer->UniqueID,"stackbuff")==0 &&
            std::abs(unit->FirepowerMultiplier-1.15)<0.00001,"cap and unit isolation");
        check(stackCount(enemy->UniqueID,"stackbuff")==0,"absent target skipped");
        grantToUnit(enemy,PassiveGrant{{"stackbuff"},5});
        trigger("onkill",unit,enemy);
        check(stackCount(enemy->UniqueID,"stackbuff")==1 && stackCount(id,"stackbuff")==3,"modify another unit");
        grantToUnit(enemy,PassiveGrant{{"deathstack","markdeathstack"},-1});
        trigger("onfire",enemy,unit);trigger("onfire",enemy,unit);
        int beforeDeath=Research::balance(foe->ArrayIndex);
        queueDeath(enemy,unit);passiveStacks.erase(enemy->UniqueID);dispatch();
        check(Research::balance(foe->ArrayIndex)==beforeDeath+20,"death stack snapshot");
        trigger("ondeploy",unit,enemy);tickStates();
        check(stackCount(id,"stackbuff")==1 && std::abs(unit->FirepowerMultiplier-1.05)<0.00001,"decrement and formula");
        trigger("ondeploy",unit,enemy);tickStates();
        check(stackCount(id,"stackbuff")==0 && std::abs(unit->FirepowerMultiplier-1.0)<0.00001,"minimum zero");
        trigger("onkill",unit,enemy);
        trigger("onkill",peer,enemy);
        check(stackCount(peer->UniqueID,"stackbuff")==1,"peer stack");
        check(peer->SetOwningHouse(foe,false),"owner change");
        reconcileUnitStacks(peer);
        check(stackCount(peer->UniqueID,"stackbuff")==0,"lost research clears stack");
        check(peer->SetOwningHouse(house,false),"owner restored");
        Research::points[h]=0;
        trigger("onfire",unit,enemy);trigger("onfire",unit,enemy);
        check(stackCount(id,"selfgrowth")==2 && Research::balance(h)==10 && std::abs(unit->ArmorMultiplier-1.01)<0.00001,"event stack snapshot");
    }
    if(frame==25) {
        Research::unlocked[h].erase("stackresearch");reconcileAllStacks();
        check(stackCount(id,"stackbuff")==1,"overlapping source retained");
        check(skills.contains("steallist") && skills.contains("striplist") &&
              !skills.contains("invalidstealcount") && !skills.contains("invalidsteallist"),"steal list validation");
        grantToUnit(enemy,PassiveGrant{{"loota","lootb","striplist"},15});
        changeStacks(enemy->UniqueID,"loota",4,4);
        grantToUnit(peer,PassiveGrant{{"steallist"},-1});
        trigger("ondealdamage",peer,enemy);
        auto receiver=peer->UniqueID,donor=enemy->UniqueID;
        check(stackCount(receiver,"loota")==2 && stackCount(donor,"loota")==2 &&
              ownsPassive(enemy,"loota") && ownsPassive(peer,"lootb") && !ownsPassive(enemy,"lootb"),"partial and zero stack theft");
        check(std::any_of(pending.begin(),pending.end(),[&](const Event& e) {
            return e.holder==receiver && e.trigger=="onpassivegained" && e.stackSnapshot.at("loota")==2;
        }),"acquisition sees stolen stacks");
        changeStacks(receiver,"loota",1,4);
        trigger("ondealdamage",peer,enemy);
        trigger("ondealdamage",peer,enemy);
        check(stackCount(receiver,"loota")==4 && stackCount(donor,"loota")==1 &&
              triggerTotals[receiver]["steallist"]==2,"steal respects capacity");
        changeStacks(receiver,"loota",-1,4);
        trigger("ondealdamage",peer,enemy);
        check(stackCount(receiver,"loota")==4 && !ownsPassive(enemy,"loota") &&
              ownsPassive(enemy,"striplist"),"last stack removes only matching passive");
    }
    if(frame==28) {
        capture("onfire",unit,enemy,unit,enemy,0);
        passiveStacks[id]["selfgrowth"]=3;
        check(ScenarioClass::SaveGame("Saved Games\\PassiveStacks.sav",L"PassiveStacks"),"saved");
    }
    if(frame==31) {
        check(ownsPassive(peer,"loota") && ownsPassive(peer,"lootb") && stackCount(peer->UniqueID,"loota")==4 &&
              !ownsPassive(enemy,"loota") && !ownsPassive(enemy,"lootb"),"transferred ownership retained");
        // Capture a defensive event with the opposing unit as its attacker.
        capture("ontakedamage",enemy,peer,peer,enemy,5);dispatch();
        check(!ownsPassive(peer,"loota") && !ownsPassive(peer,"lootb") &&
              stackCount(peer->UniqueID,"loota")==0 && ownsPassive(peer,"steallist"),"batch remove skips absent and clears all matches");
        tickStates();
        check(stackCount(id,"stackbuff")==1 && std::abs(unit->FirepowerMultiplier-1.05)<0.00001,"stack retained");
        check(stackCount(id,"selfgrowth")==4 && Research::balance(h)==30 && std::abs(unit->ArmorMultiplier-1.03)<0.00001,"queued stack snapshot retained");
    }
    if(frame==46) {
        check(stackCount(id,"stackbuff")==0,"last source expired");
        grantToUnit(unit,PassiveGrant{{"stackbuff"},5});
        check(stackCount(id,"stackbuff")==0,"reacquire starts zero");
        trigger("onkill",unit,enemy);
        check(stackCount(id,"stackbuff")==1,"reacquire can gain");
        trigger("onpromote",unit,enemy);
        check(stackCount(id,"stackbuff")==0,"explicit removal clears");
        grantToUnit(unit,PassiveGrant{{"stackbuff"},5});
        check(stackCount(id,"stackbuff")==0,"explicit reacquire starts zero");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
