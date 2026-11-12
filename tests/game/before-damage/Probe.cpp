#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* name) {
    std::ofstream("before-damage.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
int hit(UnitClass* unit,TechnoClass* attacker,int amount,bool forced=false) {
    int before=UnitHealth::read(unit).current;
    unit->ReceiveDamage(&amount,0,WarheadTypeClass::Find("AP"),attacker,forced,false,attacker?attacker->Owner:nullptr);
    return before-UnitHealth::read(unit).current;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestBeforeDamage,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(!loading && frame>=25) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    if((!loading && frame!=20) || (loading && frame!=30)) return 0;
    std::vector<UnitClass*> friendly;
    UnitClass* enemy=nullptr;
    auto human=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    for(auto u:UnitClass::Array) {
        if(u->Owner==human && !_stricmp(u->Type->ID,"MTNK")) friendly.push_back(u);
        if(!enemy && u->Owner==HouseClass::FindByPlayerAt(HouseClass::PlayerAtB)) enemy=u;
    }
    check(friendly.size()>=10 && enemy,"units");
    if(!loading) {
        check(skills.contains("blockonce") && skills.contains("halfdamage") && skills.contains("damagechain") &&
              skills.contains("countedblock") && !skills.contains("invalidblock"),"config");
        auto shield=friendly[0],base=friendly[1],half=friendly[2],chain=friendly[3],counted=friendly[4],invalid=friendly[5];
        grantToUnit(shield,PassiveGrant{{"blockonce","damagenotice"},-1});
        hit(shield,enemy,0);
        check(!triggerTotals[shield->UniqueID].contains("blockonce"),"zero damage ignored");
        dispatch();int points=Research::balance(human->ArrayIndex);
        int hp=UnitHealth::read(shield).current;
        check(hit(shield,enemy,5000)==0 && UnitHealth::read(shield).current==hp,"lethal blocked");
        dispatch();
        check(Research::balance(human->ArrayIndex)==points,"blocked no damage event");
        int baseline=hit(base,enemy,80);
        check(baseline>0,"baseline");
        check(hit(shield,enemy,80)==baseline,"second same frame hits");
        dispatch();
        check(Research::balance(human->ArrayIndex)==points+1,"unblocked damage event");
        grantToUnit(half,PassiveGrant{{"halfdamage"},-1});
        check(hit(half,enemy,80)==baseline/2,"formula after armor");
        grantToUnit(chain,PassiveGrant{{"damagechain"},-1});
        check(hit(chain,enemy,80)==baseline/2+7,"formula chain");
        grantToUnit(counted,PassiveGrant{{"countedblock"},-1});
        check(hit(counted,base,80)==baseline,"filter ignored friendly");
        check(hit(counted,enemy,80)==baseline && hit(counted,enemy,80)==0,"second enemy hit blocked");
        grantToUnit(invalid,PassiveGrant{{"invalidformula"},-1});
        check(hit(invalid,enemy,80)==baseline,"invalid formula skipped");
        auto forced=friendly[6];
        grantToUnit(forced,PassiveGrant{{"blockonce"},-1});
        check(hit(forced,enemy,15,true)>0 && !triggerTotals[forced->UniqueID].contains("blockonce"),"forced damage bypasses");
        check(hit(forced,enemy,80)==0,"forced hit preserves charge");
        grantToUnit(forced,PassiveGrant{{"cooldownblock"},-1});
        check(hit(forced,enemy,80)==0 && hit(forced,enemy,80)==baseline,"block cooldown");
        auto boosted=friendly[7];
        check(UnitHealth::change(boosted,100),"extra health fixture");
        grantToUnit(boosted,PassiveGrant{{"blockonce"},-1});
        int boostedHp=UnitHealth::read(boosted).current;
        check(hit(boosted,enemy,5000)==0 && UnitHealth::read(boosted).current==boostedHp,"extra health lethal blocked");
        check(hit(boosted,enemy,80)>0,"extra health next hit");
        auto targetFormula=friendly[8];
        grantToUnit(targetFormula,PassiveGrant{{"attackerformula"},-1});
        check(hit(targetFormula,nullptr,80)==baseline,"missing attacker skips formula");
        check(hit(targetFormula,enemy,80)==1,"attacker health formula");
        auto stacked=friendly[9];
        grantToUnit(stacked,PassiveGrant{{"stackdamage"},-1});
        passiveStacks[stacked->UniqueID]["stackdamage"]=1;
        check(hit(stacked,enemy,80)==baseline/2,"stack damage formula");
        auto type=BuildingTypeClass::Find("GAPOWR");
        auto building=type?static_cast<BuildingClass*>(type->CreateObject(human)):nullptr;
        CellStruct buildingAt{50,65};auto cell=MapClass::Instance.TryGetCellAt(buildingAt);
        check(building && cell && building->Unlimbo(cell->GetCoordsWithBridge(),DirType::North),"building fixture");
        grantToUnit(building,PassiveGrant{{"blockonce"},-1});
        int amount=5000,buildingHp=building->Health;
        building->ReceiveDamage(&amount,0,WarheadTypeClass::Find("AP"),enemy,false,false,enemy->Owner);
        check(building->Health==buildingHp && triggerTotals[building->UniqueID]["blockonce"]==1,"building lethal blocked");
        check(skills.contains("finisher") && !skills.contains("invalidoutgoingtarget"),"outgoing config");
        auto wh=WarheadTypeClass::Find("AP");
        float spread=wh->CellSpread;
        wh->CellSpread=6;
        auto blast=[&] { MapClass::DamageArea(base->GetCoords(),12,enemy,wh,false,enemy->Owner); };
        const int boundary=UnitHealth::read(base).maximum/2;
        const int wounded=UnitHealth::read(invalid).maximum/2-1;
        UnitHealth::heal(base,boundary);
        UnitHealth::heal(invalid,wounded);
        blast();
        const int atHalf=boundary-UnitHealth::current(base);
        const int belowHalf=wounded-UnitHealth::current(invalid);
        check(atHalf>0 && belowHalf>1,"area baseline");
        grantToUnit(enemy,PassiveGrant{{"finisher"},-1});
        UnitHealth::heal(base,boundary);
        UnitHealth::heal(invalid,wounded);
        blast();
        wh->CellSpread=spread;
        check(boundary-UnitHealth::current(base)==atHalf && wounded-UnitHealth::current(invalid)==belowHalf*3/2,"area per victim threshold");
        UnitHealth::heal(chain,UnitHealth::read(chain).maximum/2-1);
        int normal=hit(base,nullptr,20);
        check(hit(chain,enemy,20)==(normal*3/2)/2+7,"outgoing before incoming");
        check(hit(invalid,nullptr,20)==normal,"no attacker skips finisher");
        std::ofstream("before-damage.id")<<shield->UniqueID;
        check(ScenarioClass::SaveGame("Saved Games\\BeforeDamage.sav",L"BeforeDamage"),"saved");
    } else {
        DWORD id=0;std::ifstream("before-damage.id")>>id;
        UnitClass* shield=nullptr;
        for(auto unit:friendly) if(unit->UniqueID==id) shield=unit;
        check(shield && triggerTotals[id]["blockonce"]==1,"trigger count loaded");
        check(hit(shield,enemy,80)>0,"blocked charge remains spent");
        auto target=friendly[1];
        UnitHealth::heal(target,UnitHealth::read(target).maximum/2-1);
        int normal=hit(target,nullptr,12);
        check(normal>0 && hit(target,enemy,12)==normal*3/2,"outgoing grant loaded");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
