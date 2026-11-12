#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* name) {
    std::ofstream("hp.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void nativeHit(UnitClass* unit,int amount,UnitClass* other) {
    unit->ReceiveDamage(&amount,0,RulesClass::Instance->C4Warhead,other,true,false,other->Owner);
}
void trigger(const char* event,UnitClass* unit,UnitClass* other) {
    capture(event,unit,other,unit,other,0);
    dispatch();
}
void expect(UnitClass* unit,int hp,int maximum,const char* label) {
    auto value=UnitHealth::read(unit);
    std::ofstream("hp.log",std::ios::app)<<label<<" "<<value.current<<"/"<<value.maximum<<" native "<<unit->Health<<"\n";
    check(value.current==hp && value.maximum==maximum,label);
}
}
DEFINE_HOOK(0x55B4EB,SF_TestMaxHealth,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int f=Unsorted::CurrentFrame;
    if(f!=20 && f!=30) return 0;
    if(f==20 && GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES) return 0;
    UnitClass *u=nullptr,*peer=nullptr;
    for(auto unit:UnitClass::Array) if(unit->Owner==HouseClass::FindByPlayerAt(HouseClass::PlayerAtA) && !_stricmp(unit->Type->ID,"MTNK")) {
        if((f==20 && !u) || (f!=20 && UnitHealth::find(unit->UniqueID) && !triggerTotals.contains(unit->UniqueID))) u=unit;
        else if(!peer) peer=unit;
    }
    check(u && peer && !skills.contains("invalidamount"),"fixture");
    int base=u->Type->Strength;
    if(f==20) {
        grantToUnit(u,PassiveGrant{{"grow","shrink","recover","track","observe"},-1});
        u->Health=base-150;
        trigger("onkill",u,peer);
        expect(u,base-50,base+100,"first grant");
        trigger("onkill",u,peer);
        expect(u,base+50,base+200,"stacked grant");
        expect(peer,base,base,"grant and isolation");
        nativeHit(u,31,peer);nativeHit(u,1,peer);
        expect(u,base+18,base+200,"exact damage");
        check(!pending.empty() && pending.back().damage==1 && pending.back().hp==base+18 &&
            pending.back().maxHp==base+200,"actual damage event snapshot");
        int ordinary=30,control=ordinary;
        auto wh=WarheadTypeClass::Find("AP");
        u->ReceiveDamage(&ordinary,0,wh,nullptr,false,false,nullptr);
        peer->ReceiveDamage(&control,0,wh,nullptr,false,false,nullptr);
        int lost=base-peer->Health;
        check(lost>0 && UnitHealth::read(u).current==base+18-lost,"ordinary damage matches peer");
        nativeHit(u,-lost,peer);
        tickStates();
        check(std::abs(u->FirepowerMultiplier-(1+182.0/(base+200)))<0.00001,"health formula");
        nativeHit(u,-11,peer);
        expect(u,base+29,base+200,"native repair");
        trigger("onfire",u,peer);
        expect(u,base+46,base+200,"passive heal");
        trigger("ondeploy",u,peer);
        expect(u,base+46,base+100,"no damage on reduction");
        grantToUnit(peer,PassiveGrant{{"engine"},-1});
        int expected=base;
        for(int i=0;i<19;++i) {
            expected+=(expected+5)/10;
            trigger("onkill",peer,u);
        }
        check(UnitHealth::read(peer).maximum==expected && triggerTotals[peer->UniqueID]["engine"]==19 &&
            VisualScale::base[peer->UniqueID]==195 && projectileBase[peer->UniqueID]==195,"engine growth 19");
        check(ScenarioClass::SaveGame("Saved Games\\MaxHealth.sav",L"MaxHealth"),"saved");
    } else {
        check(triggerTotals[peer->UniqueID]["engine"]==19 && VisualScale::base[peer->UniqueID]==195 &&
            projectileBase[peer->UniqueID]==195,"engine count retained");
        int previous=UnitHealth::read(peer).maximum;
        trigger("onkill",peer,u);
        check(UnitHealth::read(peer).maximum==previous+(previous+5)/10 && triggerTotals[peer->UniqueID]["engine"]==20 &&
            VisualScale::base[peer->UniqueID]==200 && projectileBase[peer->UniqueID]==200,"engine growth 20");
        previous=UnitHealth::read(peer).maximum;
        trigger("onkill",peer,u);
        check(UnitHealth::read(peer).maximum==previous && triggerTotals[peer->UniqueID]["engine"]==20 &&
            VisualScale::base[peer->UniqueID]==200 && projectileBase[peer->UniqueID]==200,"engine growth capped");
        expect(u,base+46,base+100,"retained points");
        nativeHit(u,-200,peer);
        expect(u,base+100,base+100,"repair to new cap");
        trigger("ondeploy",u,peer);
        expect(u,base,base,"clamp to stock");
        check(!UnitHealth::find(u->UniqueID),"removed neutral record");
        UnitHealth::change(u,INT_MIN);
        expect(u,1,1,"minimum and clamp");
        UnitHealth::change(u,base+100);
        nativeHit(u,base+100,peer);
        expect(u,1,base+101,"survived one point");
        DWORD id=u->UniqueID;
        nativeHit(u,1,peer);
        bool survived=false;
        for(auto unit:UnitClass::Array) if(unit->UniqueID==id && alive(unit)) survived=true;
        check(!survived,"lethal");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
