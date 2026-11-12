#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* label) {
    std::ofstream("aura.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
UnitClass* tank(HouseClass* house,CellStruct pos) {
    auto u=static_cast<UnitClass*>(UnitTypeClass::Find("MTNK")->CreateObject(house));
    auto c=MapClass::Instance.TryGetCellAt(pos);
    check(c && u && u->Unlimbo(c->GetCoordsWithBridge(),DirType::North),"placed tank");
    u->QueueMission(Mission::Guard,false);return u;
}
InfantryClass* infantry(const char* type,HouseClass* house,CellStruct cell) {
    auto u=static_cast<InfantryClass*>(InfantryTypeClass::Find(type)->CreateObject(house));
    auto at=MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge();
    check(u && u->Unlimbo(at,DirType::North),"placed infantry");
    u->QueueMission(Mission::Guard,false);return u;
}
void moveTo(UnitClass* u,CellStruct pos) {
    auto c=MapClass::Instance.TryGetCellAt(pos);
    check(c && u->Limbo() && u->Unlimbo(c->GetCoordsWithBridge(),DirType::North),"moved tank");
}
bool has(TechnoClass* u,const char* id) {
    vector<string> ids;extraSkills(u,&ids);return includes(ids,id);
}
void settle() {refreshAuras();tickGains();tickStates();dispatch();}
}
DEFINE_HOOK(0x55B4EB,SF_TestAura,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    const int frame=Unsorted::CurrentFrame;
    if(!loading && frame!=20) return 0;
    if(loading && frame!=24 && frame!=30 && frame!=45) return 0;
    auto owner=HouseClass::CurrentPlayer;
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    if(!loading) {
        check(skills.contains("beacon") && skills.contains("enemybeacon") && skills.contains("selfbeacon") &&
            !skills.contains("invalidnested") && !skills.contains("invalidrange") && !skills.contains("unknowngift") &&
            !skills.contains("invalidkind") && !skills.contains("invalidhouse") && !skills.contains("wrongtrigger"),"configuration");
        CellStruct anchor{};
        bool vacant=false;
        for(short y=52;!vacant && y<77;++y) for(short x=48;!vacant && x<70;++x) {
            vacant=true;
            for(short offset=0;offset<=7;++offset) {
                CellStruct p{short(x+offset),y};auto c=MapClass::Instance.TryGetCellAt(p);
                if(!c || !MapClass::Instance.IsWithinUsableArea(p,false) || c->FirstObject) {vacant=false;break;}
            }
            for(short dy:{short(-1),short(1)}) {
                CellStruct p{short(x+1),short(y+dy)};auto c=MapClass::Instance.TryGetCellAt(p);
                if(!c || c->FirstObject || !MapClass::Instance.IsWithinUsableArea(p,false)) vacant=false;
            }
            if(vacant) anchor={x,y};
        }
        check(vacant,"clear fixture area");
        auto rowCell=[&](int x) {return CellStruct{short(anchor.X+x),anchor.Y};};
        auto first=tank(owner,rowCell(0)),target=tank(owner,rowCell(3)),second=tank(owner,rowCell(6));
        CellStruct distant{};
        bool foundFar=false;
        for(short y=4;!foundFar && y<120;++y) for(short x=4;!foundFar && x<120;++x) {
            int dx=x-anchor.X,dy=y-anchor.Y;
            if(dx*dx+dy*dy<=35*35) continue;
            CellStruct p{x,y};auto c=MapClass::Instance.TryGetCellAt(p);
            if(c && MapClass::Instance.IsWithinUsableArea(p,false) && !c->FirstObject) {distant=p;foundFar=true;}
        }
        check(foundFar,"distant fixture cell");
        auto hostile=tank(foe,distant);
        auto gi=infantry("E1",owner,{short(anchor.X+1),short(anchor.Y-1)});
        auto conscript=infantry("E2",owner,{short(anchor.X+1),short(anchor.Y+1)});
        grantToUnit(first,PassiveGrant{{"beacon","enemybeacon","selfbeacon"},-1});
        grantToUnit(second,PassiveGrant{{"beacon"},-1});
        passiveStacks[first->UniqueID]["beacon"]=passiveStacks[second->UniqueID]["beacon"]=1;
        settle();
        check(has(target,"armorgift") && !has(first,"armorgift") && !has(hostile,"armorgift") &&
            auraGrants.at(target->UniqueID).at("armorgift")==2 && std::abs(target->ArmorMultiplier-1.2)<0.00001 &&
            triggerTotals[target->UniqueID]["welcome"]==1,"local range and overlap");
        check(has(gi,"armorgift") && !has(conscript,"armorgift"),"specific infantry in mixed target list");
        check(has(hostile,"enemymark") && !has(first,"enemymark") && !has(hostile,"firegift") &&
            has(first,"firegift") && has(target,"firegift"),"global filters");
        auto before=Research::balance(foe->ArrayIndex);
        capture("onfire",hostile,target,hostile,target,0);dispatch();
        check(Research::balance(foe->ArrayIndex)==before+1,"global event passive");
        passiveStacks[second->UniqueID].erase("beacon");settle();
        passiveStacks[first->UniqueID].erase("beacon");passiveStacks[second->UniqueID]["beacon"]=1;settle();
        check(has(target,"armorgift") && triggerTotals[target->UniqueID]["welcome"]==1,"atomic handoff");
        moveTo(target,rowCell(2)); // Outside source two, but still covered until the next 15-frame scan.
        check(has(target,"armorgift"),"membership cached");
        std::ofstream("aura.ids")<<second->UniqueID<<" "<<target->UniqueID<<" "<<anchor.X<<" "<<anchor.Y;
        check(ScenarioClass::SaveGame("Saved Games\\Aura.sav",L"Aura"),"saved");
        TerminateProcess(GetCurrentProcess(),0);
    } else {
        DWORD sourceID=0,targetID=0;CellStruct pos{};
        std::ifstream("aura.ids")>>sourceID>>targetID>>pos.X>>pos.Y;
        UnitClass *source=nullptr,*target=nullptr;
        for(auto u:UnitClass::Array) {if(u->UniqueID==sourceID) source=u;if(u->UniqueID==targetID) target=u;}
        check(source && target,"restored units");
        if(frame==24) check(has(target,"armorgift") && triggerTotals[targetID]["welcome"]==1,"loaded sample");
        if(frame==30) {
            check(!has(target,"armorgift") && target->ArmorMultiplier==1.0,"sampled exit");
            pos.X+=3;moveTo(target,pos);
        }
        if(frame==45) {
            check(has(target,"armorgift") && triggerTotals[targetID]["welcome"]==2,"real reacquisition");
            grantToUnit(target,PassiveGrant{{"armorgift"},-1});
            int damage=INT_MAX/4;
            source->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,nullptr,true,false,nullptr);
            check(!has(target,"welcome") && has(target,"armorgift"),"source destruction");
            TerminateProcess(GetCurrentProcess(),0);
        }
    }
    return 0;
}
