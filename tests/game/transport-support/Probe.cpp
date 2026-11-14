#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* name) {
    std::ofstream("transport-support.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
FootClass* addPassenger(UnitClass* carrier,InfantryTypeClass* type) {
    auto passenger=static_cast<FootClass*>(type->CreateObject(carrier->Owner));
    if(!passenger) return nullptr;
    carrier->EnterAsPassenger(passenger);passenger->Transporter=carrier;
    return passenger;
}
void sample() {refreshTransportGrants();tickGains();tickStates();dispatch();}
bool approx(double x,double y) {return std::abs(x-y)<0.001;}
void checkTheft(UnitClass* donor,TechnoClass* receiver) {
    auto type=InfantryTypeClass::Find("E1");
    Skill loot{};loot.id="theftloot";loot.stackMax=10;skills[loot.id]=loot;
    Skill support{};support.id="theftsupport";support.trigger="whilestate";
    support.transportPassives={loot.id};support.transportStack=true;skills[support.id]=support;
    auto a=addPassenger(donor,type),b=addPassenger(donor,type);
    check(a && b,"theft passengers");
    for(auto p:{a,b}) grantToUnit(p,PassiveGrant{{support.id},-1});
    refreshTransportGrants();
    PassiveAction action{};action.passiveList={loot.id};action.amount=1;
    check(stackCount(donor->UniqueID,loot.id)==2 && !stealPassives(receiver,donor,action) &&
        !stealPassives(receiver,donor,action) && stackCount(receiver->UniqueID,loot.id)==0,"support layers cannot be duplicated");
    changeStacks(donor->UniqueID,loot.id,2,10);
    check(stealPassives(receiver,donor,action) && stackCount(donor->UniqueID,loot.id)==3 &&
        stackCount(receiver->UniqueID,loot.id)==1,"mixed source manual theft");
    check(stealPassives(receiver,donor,action) && !stealPassives(receiver,donor,action) &&
        stackCount(donor->UniqueID,loot.id)==2 && ownsPassive(donor,loot.id) &&
        stackCount(receiver->UniqueID,loot.id)==2,"support survives manual theft");
    refreshTransportGrants();
    check(!stealPassives(receiver,donor,action) && stackCount(receiver->UniqueID,loot.id)==2,"refresh does not replenish stolen inventory");
    for(int i=0;i<2;++i) {auto p=donor->RemoveFirstPassenger();p->Transporter=nullptr;p->UnInit();}
    refreshTransportGrants();
    check(stackCount(donor->UniqueID,loot.id)==0 && stackCount(receiver->UniqueID,loot.id)==2,"withdrawal preserves transferred manual layers");
    removeHeldPassive(receiver,loot.id);
    skills.erase(support.id);skills.erase(loot.id);
}
int fullRearm(FootClass* passenger) {
    auto random=ScenarioClass::Instance->Random;
    int oldBurst=passenger->CurrentBurstIndex;
    passenger->CurrentBurstIndex=passenger->GetWeapon(0)->WeaponType->Burst;
    int frames=passenger->GetROF(0);
    passenger->CurrentBurstIndex=oldBurst;
    ScenarioClass::Instance->Random=random;
    return frames;
}
void checkCombat(FootClass* unit,FootClass* peer) {
    std::ofstream("transport-support.log",std::ios::app)<<"combat hp "<<unit->Health<<"/"<<unit->GetTechnoType()->Strength
        <<" skill "<<ownsPassive(unit,"personalcombat")<<" firepower "<<unit->FirepowerMultiplier<<"/"<<peer->FirepowerMultiplier
        <<" range "<<unit->GetWeaponRange(0)<<"/"<<peer->GetWeaponRange(0)<<" rearm "<<fullRearm(unit)<<"/"<<fullRearm(peer)<<"\n";
    check(approx(unit->FirepowerMultiplier,2) && approx(peer->FirepowerMultiplier,1) &&
        unit->GetWeaponRange(0)==std::lround(peer->GetWeaponRange(0)*1.5) &&
        fullRearm(unit)==std::lround(double(fullRearm(peer))*unit->Health/unit->GetTechnoType()->Strength),"passenger combat multipliers");
}
}
DEFINE_HOOK(0x55B4EB,SF_TestTransportSupport,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(!loading && frame>=25) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    if((!loading && frame!=20) || (loading && frame!=30)) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    DWORD id=0;int savedPoints=0;if(loading) std::ifstream("transport-support.id")>>id>>savedPoints;
    UnitClass *u=nullptr,*enemy=nullptr;
    for(auto unit:UnitClass::Array) {
        if(!u && unit->Owner==house && !_stricmp(unit->Type->ID,"MTNK") && (!id || id==unit->UniqueID)) u=unit;
        if(!enemy && unit->Owner==foe) enemy=unit;
    }
    check(u && enemy,"units");
    if(!loading) {
        checkTheft(u,enemy);
        check(skills.contains("support") && skills.contains("boost") && skills.contains("nonstacksupport") && !skills.contains("badcycle"),"configuration");
        auto type=InfantryTypeClass::Find("E1");check(type!=nullptr,"infantry type");
        auto a=addPassenger(u,type),b=addPassenger(u,type);
        check(a && b,"passengers");
        grantToUnit(a,PassiveGrant{{"support","nonstacksupport","conditionalsupport"},-1});
        grantToUnit(b,PassiveGrant{{"support","nonstacksupport"},-1});
        int points=Research::balance(house->ArrayIndex);
        sample();
        check(stackCount(u->UniqueID,"boost")==2 && approx(u->FirepowerMultiplier,1.6) && Research::balance(house->ArrayIndex)==points+7,"two passengers stack");
        capture("onkill",u,enemy,u,enemy,0);dispatch();check(Research::balance(house->ArrayIndex)==points+27,"snapshot layers");
        u->Passengers.RemoveFirstPassenger();b->Transporter=nullptr;sample();
        check(stackCount(u->UniqueID,"boost")==1 && approx(u->FirepowerMultiplier,1.3),"passenger withdrawal");
        check(changeStacks(u->UniqueID,"boost",1,2),"add manual stack");sample();
        check(stackCount(u->UniqueID,"boost")==2 && approx(u->FirepowerMultiplier,1.6),"separate layers");
        a->Health=type->Strength/2;sample();check(approx(u->SpeedMultiplier,1.5),"conditional support");
        a->Health=type->Strength;sample();check(approx(u->SpeedMultiplier,1.0),"conditional withdrawal");
        check(approx(u->ArmorMultiplier,1.2) && stackCount(u->UniqueID,"armorboost")==0,"no stack");

        u->EnterAsPassenger(b);b->Transporter=u;sample();
        check(stackCount(u->UniqueID,"boost")==2 && Research::balance(house->ArrayIndex)==points+27,"overlapping gain stays once");
        grantToUnit(b,PassiveGrant{{"personalcombat"},-1});
        b->Health=type->Strength/2;sample();
        checkCombat(b,a);
        check(approx(b->ArmorMultiplier,1) && approx(u->FirepowerMultiplier,1.6),"passenger stats isolated");
        b->Health=type->Strength;sample();
        check(approx(b->FirepowerMultiplier,1) && b->GetWeaponRange(0)==a->GetWeaponRange(0) &&
              fullRearm(b)==fullRearm(a),"passenger healing clears combat bonus");
        b->Health=type->Strength/2;sample();
        std::ofstream("transport-support.id")<<u->UniqueID<<" "<<Research::balance(house->ArrayIndex);
        check(ScenarioClass::SaveGame("Saved Games\\TransportSupport.sav",L"Transport Support"),"saved");
        // Exercise the native open-topped shot with identical gunners and weapon settings.
        u->Type->OpenTopped=true;
        u->EnteredOpenTopped(a);u->EnteredOpenTopped(b);
        a->SetLocation(u->GetCoords());b->SetLocation(u->GetCoords());
        auto aimCell=u->GetMapCoords();aimCell.Y-=2;
        auto dummy=static_cast<UnitClass*>(u->Type->CreateObject(foe));
        check(dummy && dummy->Unlimbo(MapClass::Instance.GetCellAt(aimCell)->GetCoordsWithBridge(),DirType::North),"placed shooting target");
        bool farther=false;
        for(int n=1;n<=16;++n) {
            auto cell=u->GetMapCoords();cell.X+=n;
            auto aim=MapClass::Instance.GetCellAt(cell);
            if(!a->IsCloseEnough(aim,0) && b->IsCloseEnough(aim,0)) farther=true;
        }
        check(farther,"open topped passenger range boundary");
        auto weapon=a->GetWeapon(0)->WeaponType;
        auto original=weapon->Projectile;
        weapon->Projectile=BulletTypeClass::Find("Cannon"); // Keep both rounds observable after firing.
        a->SetTarget(dummy);b->SetTarget(dummy);
        auto normal=a->Fire(dummy,0),boosted=b->Fire(dummy,0);
        for(auto bullet:BulletClass::Array) {
            if(bullet->Owner==a) normal=bullet;
            if(bullet->Owner==b) boosted=bullet;
        }
        weapon->Projectile=original;
        std::ofstream("transport-support.log",std::ios::app)<<"rounds "<<(normal?normal->Health:-1)<<" "<<(boosted?boosted->Health:-1)
            <<" shooter flags "<<a->InOpenToppedTransport<<" "<<b->InOpenToppedTransport<<" fire error "<<int(a->GetFireError(dummy,0,true))<<"\n";
        check(normal && boosted && normal->Health>0 && boosted->Health==2*normal->Health,"open topped shot damage");
    } else {
        sample();check(u->Passengers.NumPassengers==2 && stackCount(u->UniqueID,"boost")==2 && approx(u->FirepowerMultiplier,1.6),"load sources");
        check(Research::balance(house->ArrayIndex)==savedPoints,"load no repeated gain");
        auto first=u->Passengers.GetFirstPassenger();
        auto peer=static_cast<FootClass*>(first->NextObject);
        check(peer!=nullptr,"loaded passenger peer");
        auto shooter=ownsPassive(first,"personalcombat")?first:peer;
        auto other=shooter==first?peer:first;
        checkCombat(shooter,other);
        auto removed=u->Passengers.RemoveFirstPassenger();check(removed!=nullptr,"loaded passenger");removed->Transporter=nullptr;sample();
        check(approx(removed->FirepowerMultiplier,1),"detached limbo clears combat bonus");
        auto cell=u->GetMapCoords();cell.Y+=4;
        check(removed->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge(),DirType::North),"unload passenger");
        sample();checkCombat(removed,peer);
        check(approx(removed->ArmorMultiplier,1.75),"unloaded passenger uses normal state");
        check(stackCount(u->UniqueID,"boost")==2 && approx(u->FirepowerMultiplier,1.6),"load removal");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
