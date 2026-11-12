#include "../../../src/Starfall.cpp"

namespace {
void check(bool ok,const char* label) {
    std::ofstream("gained.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void killEvent(TechnoClass* u,TechnoClass* other) {
    capture("onkill",u,other,u,other,0);
    dispatch();
    dispatch(); // grant actions can enqueue a separate acquisition event
}
}
DEFINE_HOOK(0x55B4EB,SF_TestPassiveGained,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int f=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(f==28 && loading) return 0;
    if(f!=20 && f!=25 && f!=28 && f!=31 && f!=46) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    UnitClass *u=nullptr,*other=nullptr;
    DWORD holder=0,victim=0;
    if(f!=20) {std::ifstream ids("gained.ids");ids>>holder>>victim;}
    for(auto unit:UnitClass::Array) {
        if(!u && unit->Owner==house && !_stricmp(unit->Type->ID,"MTNK") && (!holder || holder==unit->UniqueID)) u=unit;
        if(!other && unit->Owner==foe && (!victim || victim==unit->UniqueID)) other=unit;
    }
    check(u && other,"fixture units");
    int h=house->ArrayIndex,v=foe->ArrayIndex,base=u->Type->Strength;
    if(f==20) {
        std::ofstream("gained.ids")<<u->UniqueID<<" "<<other->UniqueID;
        check(!skills.contains("expiredkey") && !skills.contains("badrange") && !skills.contains("badgrant") && !skills.contains("badduration"),"invalid rules");
        check(UnitHealth::read(u).maximum==base+10,"native gain");
        Research::points[h]=100;Research::points[v]=0;
        check(Research::purchase(h,0,house->Type->ID),"purchase");
        tickGains();dispatch();
        check(UnitHealth::read(u).maximum==base+60,"existing unit");
        grantToUnit(u,PassiveGrant{{"chassis"},5});dispatch();
        check(UnitHealth::read(u).maximum==base+60,"overlapping research grant");
        check(spawnUnit(u->Type,house,u->GetCoords()),"spawn");
        tickGains();dispatch();
        for(auto unit:UnitClass::Array) if(unit->Owner==house && unit->Type==u->Type && unit->IsOnMap)
            check(UnitHealth::read(unit).maximum==base+60,"new and current units");
        u->InLimbo=true;observeGains(u);u->InLimbo=false;observeGains(u);dispatch();
        check(UnitHealth::read(u).maximum==base+60,"research and spawn");
        grantToUnit(u,PassiveGrant{{"killband"},-1});
        int mid=(base+60)/2;
        for(int n:{mid-1,mid,mid+1}) {
            UnitHealth::heal(u,n);
            tickStates();
            check(u->FirepowerMultiplier==(n==mid?2.0:1.0),"state boundary");
            killEvent(u,other);
        }
        check(Research::balance(h)==1,"inclusive thresholds");
        grantToUnit(u,PassiveGrant{{"trainer"},-1});
        killEvent(u,other);
        check(Research::balance(h)==6 && Research::balance(v)==5,"target house");
        tickStates();check(other->FirepowerMultiplier==1.25,"temporary modifier");
    }
    if(f==25) {
        killEvent(u,other);
        check(Research::balance(h)==11 && Research::balance(v)==7,"source renewal");
        grantToUnit(other,PassiveGrant{{"deathreward"},-1});
    }
    if(f==28) {
        grantToUnit(other,PassiveGrant{{"pulse"},-1}); // save an acquired but not yet dispatched event
        check(ScenarioClass::SaveGame("Saved Games\\PassiveGained.sav",L"PassiveGained"),"saved");
    }
    if(f==31) {
        check(UnitHealth::read(u).maximum==base+60 && Research::balance(h)==11,"no repeat after save");
        check(Research::balance(v)==18,"pending gain");
        check(other->FirepowerMultiplier==1.25,"renewed modifier");
        grantToUnit(other,PassiveGrant{{"pulse"},12});
        dispatch();check(Research::balance(v)==18,"permanent grant overlap");
    }
    if(f==46) {
        check(!warheadGrants.at(other->UniqueID).skills.contains("talent") && other->FirepowerMultiplier==1.0,"expired");
        killEvent(u,other);
        check(Research::balance(v)==23,"reacquisition");
        queueDeath(other,u);dispatch();
        check(Research::balance(v)==40 && Research::balance(h)==16,"death reward");
        grantToUnit(u,PassiveGrant{{"sharedamage"},-1});
        capture("ondealdamage",u,other,u,other,23);dispatch();
        check(Research::balance(h)==21,"actual damage points");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
