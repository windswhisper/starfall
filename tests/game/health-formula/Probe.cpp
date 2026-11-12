#include "../../../src/Starfall.cpp"
#pragma comment(lib,"ole32.lib")

namespace {
void check(bool ok,const char* label) {
    std::ofstream("formula.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
bool approximately(double value,double expected) {return std::abs(value-expected)<0.00001;}
void grammar() {
    HealthFormula f;
    check(f.compile("$( -(-2) + min(3,4)*clamp(8,1,5)/2 - 25% )") &&
        approximately(f.evaluate(1,2).value_or(0),9.25),"precedence");
    for(auto text:{"$(1+)", "$(foo)", "$(min(1))", "$(max(1,2,3))", "$(1)junk", "$()", "$(self.health%)", "$(self.losthealth)", "$(self.losthealthratio)", "$(target.losthealth)", "$(target.losthealthratio)"})
        check(!f.compile(text),"invalid syntax");
    check(f.compile("$(1 / self.health)") && !f.evaluate(0,2),"division by zero");
    check(f.compile("$(clamp(1,3,2))") && !f.evaluate(2,2),"reversed bounds");
    check(f.compile("$((self.maxhealth-self.health)/self.maxhealth)") && f.evaluate(-5,100)==1.0 && f.evaluate(110,100)==0.0,"grammar");
    check(f.compile("$((target.maxhealth-target.health)/target.maxhealth)") && f.usesTargetValues() &&
        !f.evaluate(1,2) && approximately(f.evaluate(1,2,0,std::nullopt,0,HealthFormula::Values{100,400}).value_or(-1),0.75) &&
        f.evaluate(1,2,0,std::nullopt,0,HealthFormula::Values{0,400})==1.0,"target health grammar");
    bool missing=false;
    check(f.compile("$(target.cost * 25%)") && f.usesTargetValues() &&
        f.evaluate(1,2,0,std::nullopt,0,HealthFormula::Values{0,100,800})==200 &&
        f.evaluate(1,2,0,std::nullopt,0,HealthFormula::Values{0,100,0})==0 &&
        !f.evaluate(1,2,0,std::nullopt,0,HealthFormula::Values{0,100},&missing) && missing,
        "target cost, zero and legacy missing value");
    check(f.compile("$(if(1,7,target.cost))") && f.evaluate(1,2)==7,"unused target cost branch");
    check(f.compile("$(self.cost)") && !f.usesTargetValues() &&
        f.evaluate(1,2,0,std::nullopt,0,std::nullopt,&missing,0)==0 && !missing &&
        !f.evaluate(1,2,0,std::nullopt,0,std::nullopt,&missing) && missing,
        "self cost zero and legacy missing value");
    check(f.compile("$(event.damage * 25%)") && f.usesDamage() && approximately(f.evaluate(1,2,43).value_or(0),10.75),"damage grammar");
    check(f.compile("$(event.distance * 2)") && f.usesDistance() && !f.evaluate(1,2) &&
        approximately(f.evaluate(1,2,0,2.5).value_or(0),5),"distance grammar");
    for(auto text:{"$(1<2)","$(1<=1)","$(2>1)","$(2>=2)","$(1==1)","$(1!=2)","$(1+2*3==7)"})
        check(f.compile(text) && f.evaluate(1,2)==1,"comparison true");
    for(auto text:{"$(1>2)","$(2<1)","$(1<1)","$(1>1)","$(1<=0)","$(1>=2)","$(1==2)","$(1!=1)"})
        check(f.compile(text) && f.evaluate(1,2)==0,"comparison false");
    for(auto text:{"$(1=1)","$(1<>2)","$(1<2<3)","$(if(1,2))","$(if(1,,2))","$(if(1,2,3,4))"})
        check(!f.compile(text),"invalid conditional syntax");
    check(f.compile("$(if(event.distance > 5, 1.3, 1))") &&
        approximately(f.evaluate(1,2,0,5).value_or(0),1) &&
        approximately(f.evaluate(1,2,0,5.01).value_or(0),1.3) && !f.evaluate(1,2),"if distance threshold");
    check(f.compile("$(if(0,1/0,if(2>=2,7,target.health)))") && f.evaluate(1,2)==7 &&
        f.compile("$(max(2,if(0,event.distance,3))+1)") && f.evaluate(1,2)==4 &&
        f.compile("$(if(1,1/0,3))") && !f.evaluate(1,2),"if lazy nested branches");
}
void checkLegacy() {
    // Construct a v0.26 passive stream with nonempty integer stats and both weapon maps.
    Writer old;
    for(int i=0;i<6;++i) old.word(0);
    constexpr DWORD id=42;
    old.word(1);old.word(id);
    for(int k=0;k<3;++k) {
        old.word(1);old.real(1.0);old.real(1.1);old.word(110+k);
    }
    for(int i=0;i<16;++i) old.word(0); // timers through saved projectiles
    for(int value:{125,135}) {old.word(1);old.word(id);old.word(value);}
    old.word(1);old.word(id); // immunity follows the weapon maps
    IStream* stream=nullptr;
    check(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)),"legacy stream");
    DWORD header[]={0x47504653,static_cast<DWORD>(old.bytes.size())};
    stream->Write(header,sizeof(header),nullptr);
    stream->Write(old.bytes.data(),header[1],nullptr);
    stream->Seek({},STREAM_SEEK_SET,nullptr);
    loadState(stream);
    stream->Release();
    check(applied.contains(id) && applied[id].percent==Factors{110,111,112} &&
        rangeBase[id]==125 && fireRateBase[id]==135 && immuneUnits.contains(id),"legacy integer stats");
}
void checkLegacyShieldRecords() {
    // Version 0x4D stored shield-grant records after passive acquisition data.
    Writer old;
    for(int i=0;i<6;++i) old.word(0); // counts, events, research and producers
    old.word(0); // applied state
    for(int i=0;i<16;++i) old.word(0); // timers through projectile scaling
    for(int i=0;i<4;++i) old.word(0); // weapon ranges, fire rates, immunity, refreshes
    old.word(1);old.word(1234); // global income state
    old.word(0); // health pools
    old.word(1);old.word(42);old.word(1);old.str("keptpassive");
    old.word(1);old.word(42);old.str("OldShield");old.word(75);old.word(90);
    IStream* stream=nullptr;
    check(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)),"legacy shield stream");
    DWORD header[]={0x4D504653,static_cast<DWORD>(old.bytes.size())};
    stream->Write(header,sizeof(header),nullptr);
    stream->Write(old.bytes.data(),header[1],nullptr);
    stream->Seek({},STREAM_SEEK_SET,nullptr);
    loadState(stream);
    stream->Release();
    check(gainSeen.contains(42) && includes(gainSeen[42],"keptpassive") &&
        Research::globalIncome.initialized && Research::globalIncome.lastFrame==1234,
        "legacy shield records skipped");
}
}

DEFINE_HOOK(0x55B4EB,SF_TestHealthFormula,5) {
    static const bool enabled=GetFileAttributesA("Starfall.test")!=INVALID_FILE_ATTRIBUTES;
    if(!enabled) return 0;
    int frame=Unsorted::CurrentFrame;
    if(frame!=20 && frame!=30) return 0;
    if(frame==20 && GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES) return 0;
    UnitClass *holder=nullptr,*recipient=nullptr;
    for(auto unit:UnitClass::Array) {
        if(unit->Owner!=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA) || _stricmp(unit->Type->ID,"MTNK")) continue;
        if(frame==20) {
            if(!holder) holder=unit;else if(!recipient) recipient=unit;
        } else if(warheadGrants.contains(unit->UniqueID)) holder=unit;
        else if(rangeBase.contains(unit->UniqueID)) recipient=unit;
    }
    check(holder && recipient,"units");
    const int maximum=holder->Type->Strength;
    if(frame==20) {
        grammar();
        check(!skills.contains("removedshield"),"removed shield action rejected");
        check(skills.contains("dynamic") && skills.contains("growth") && !skills.contains("malformed"),"rejected invalid skill");
        check(skills.contains("damageformula") && !skills.contains("invaliddamagestate") && !skills.contains("invaliddamagekill"),"damage trigger validation");
        grantToUnit(holder,PassiveGrant{{"damageformula"},-1});
        holder->Health=maximum/2;
        int balance=Research::balance(holder->Owner->ArrayIndex);
        capture("ondealdamage",holder,recipient,holder,recipient,43);
        dispatch();
        check(holder->Health==maximum/2+10 && Research::balance(holder->Owner->ArrayIndex)==balance+21 &&
            approximately(recipient->FirepowerMultiplier,1.043),"damage formula results");
        // Remove this temporary test bonus before exercising the existing saved-state checks.
        auto& damageBonus=applied[recipient->UniqueID];
        damageBonus.percent[1]=100;updateStat(damageBonus,1,recipient->FirepowerMultiplier);
        if(damageBonus.state==identity && damageBonus.percent==defaultStatPercent) applied.erase(recipient->UniqueID);
        check(UnitHealth::change(recipient,100),"target maximum increased");
        int targetMaximum=UnitHealth::read(recipient).maximum;
        UnitHealth::heal(recipient,0);
        auto killTarget=eventSnapshot("onkill",holder,recipient,holder,recipient,0);
        check(killTarget.targetHp==0 && killTarget.targetMaxHp==targetMaximum && killTarget.targetCost==recipient->Type->Cost,"dead target snapshot");
        UnitHealth::heal(recipient,targetMaximum/2);
        grantToUnit(holder,PassiveGrant{{"targetevent"},-1});
        holder->Health=maximum/2;
        balance=Research::balance(holder->Owner->ArrayIndex);
        capture("ontakedamage",holder,recipient,recipient,holder,1);
        check(!pending.empty() && pending.back().targetHp==targetMaximum/2 && pending.back().targetMaxHp==targetMaximum,"target event snapshot");
        UnitHealth::heal(recipient,targetMaximum);dispatch();
        check(Research::balance(holder->Owner->ArrayIndex)==balance+targetMaximum-targetMaximum/2 &&
            holder->Health==maximum/2+(targetMaximum/2)/10 && approximately(holder->FirepowerMultiplier,1.05),"target event formula results");
        Research::points[holder->Owner->ArrayIndex]=balance;
        auto& targetBonus=applied[holder->UniqueID];targetBonus.percent[1]=100;updateStat(targetBonus,1,holder->FirepowerMultiplier);
        if(targetBonus.state==identity && targetBonus.percent==defaultStatPercent) applied.erase(holder->UniqueID);
        warheadGrants[holder->UniqueID].skills.erase("targetevent");
        grantToUnit(holder,PassiveGrant{{"dynamic","growth"},-1});
        for(int i=1;i<=2;++i) {
            holder->Health=maximum/2;
            capture("onkill",holder,recipient,holder,recipient,0);
            dispatch();
            check(holder->Health==maximum && approximately(recipient->FirepowerMultiplier,1+i*0.0075),"event snapshot");
        }
        holder->Health=maximum/2;
        tickStates();tickStates();
        check(approximately(holder->FirepowerMultiplier,1.75),"no frame stacking");
        grantToUnit(holder,PassiveGrant{{"targetstate"},-1});
        UnitHealth::heal(recipient,targetMaximum/2);
        auto priorTarget=holder->Target;holder->SetTarget(recipient);tickStates();
        check(approximately(holder->ArmorMultiplier,1.5+recipient->Type->Cost/10000.0),"target state current health and cost");
        UnitHealth::heal(recipient,targetMaximum);tickStates();
        check(approximately(holder->ArmorMultiplier,1+recipient->Type->Cost/10000.0),"target state live update");
        holder->SetTarget(nullptr);tickStates();
        check(approximately(holder->ArmorMultiplier,1),"target state no target");
        warheadGrants[holder->UniqueID].skills.erase("targetstate");holder->SetTarget(priorTarget);tickStates();
        grantToUnit(holder,PassiveGrant{{"conditionalstate","conditionaldistance"},-1});
        holder->SetTarget(nullptr);tickStates();
        check(approximately(fireRateState.at(holder->UniqueID),1.25*(1.25+holder->Type->Cost/10000.0)) && approximately(holder->ArmorMultiplier,1),"conditional state without target");
        holder->SetTarget(recipient);tickStates();
        auto actualDistance=distanceCells(holder->GetCenterCoords(),recipient->GetCenterCoords());
        check(approximately(holder->ArmorMultiplier,actualDistance>5?1.3:1),"conditional distance state");
        warheadGrants[holder->UniqueID].skills.erase("conditionalstate");
        warheadGrants[holder->UniqueID].skills.erase("conditionaldistance");holder->SetTarget(priorTarget);tickStates();
        grantToUnit(holder,PassiveGrant{{"conditionalevent"},-1});
        balance=Research::balance(holder->Owner->ArrayIndex);
        capture("onfire",holder,nullptr,holder,nullptr,0);dispatch();
        check(Research::balance(holder->Owner->ArrayIndex)==balance+holder->Type->Cost,"conditional event without target");
        Research::points[holder->Owner->ArrayIndex]=balance;
        warheadGrants[holder->UniqueID].skills.erase("conditionalevent");
        grantToUnit(holder,PassiveGrant{{"distancetest"},-1});
        auto e=eventSnapshot("ontakedamage",holder,recipient,recipient,holder,1);
        check(approximately(e.distance,distanceCells(holder->GetCenterCoords(),recipient->GetCenterCoords())) &&
            approximately(distanceCells({0,0,0},{768,1024,900}),5),"distance geometry and attacker");
        grantToUnit(holder,PassiveGrant{{"distancestate"},-1});
        auto previousTarget=holder->Target;
        holder->SetTarget(recipient);tickStates();
        check(approximately(holder->ArmorMultiplier,1+e.distance*0.01),"distance state target");
        holder->SetTarget(nullptr);tickStates();
        check(approximately(holder->ArmorMultiplier,1),"distance state no target");
        warheadGrants[holder->UniqueID].skills.erase("distancestate");
        holder->SetTarget(previousTarget);tickStates();
        balance=Research::balance(holder->Owner->ArrayIndex);
        capture("onfire",holder,nullptr,holder,nullptr,0);dispatch();
        check(Research::balance(holder->Owner->ArrayIndex)==balance,"distance missing target");
        grantToUnit(holder,PassiveGrant{{"targetpending"},-1});
        capture("onfire",holder,nullptr,holder,nullptr,0);dispatch();
        check(Research::balance(holder->Owner->ArrayIndex)==balance,"target missing skips action");
        grantToUnit(holder,PassiveGrant{{"conditionalsnapshot"},-1});
        UnitHealth::heal(recipient,targetMaximum/2);
        capture("onfire",holder,recipient,holder,recipient,0);
        check(!pending.empty() && approximately(pending.back().distance,e.distance) && pending.back().targetHp==targetMaximum/2 && pending.back().targetMaxHp==targetMaximum,"distance event snapshot");
        UnitHealth::heal(recipient,targetMaximum);
        pending.back().selfCost=400; // Distinct holder and target snapshots for the save/load check.
        pending.back().targetCost=1200; // A saved cost distinct from the live tank type.
        pending.back().distance=7.25; // Distinguish the saved snapshot from current positions.
        check(ScenarioClass::SaveGame("Saved Games\\HealthFormula.sav",L"HealthFormula"),"saved");
    } else {
        // The loaded event is dispatched by the game before this frame.
        check(Research::balance(holder->Owner->ArrayIndex)==846+120+40+UnitHealth::read(recipient).maximum/2,"distance result after load");
        auto id=recipient->UniqueID;
        for(auto value:applied.at(id).percent) check(approximately(value,101.5),"fractional stat");
        check(approximately(rangeBase.at(id),101.5) && approximately(fireRateBase.at(id),101.5) &&
            approximately(recipient->ArmorMultiplier,1.015),"retained fractional stats");
        check(approximately(holder->FirepowerMultiplier,1.75) && approximately(holder->SpeedMultiplier,2) &&
            approximately(holder->ArmorMultiplier,1) && approximately(rangeState.at(holder->UniqueID),1.5) &&
            approximately(fireRateState.at(holder->UniqueID),1.25),"retained state");
        holder->Health=maximum*3/4;tickStates();
        check(approximately(holder->FirepowerMultiplier,1+1.5*(maximum-holder->Health)/maximum),"partial healing");
        holder->Health=maximum;tickStates();
        check(approximately(holder->FirepowerMultiplier,1) && !rangeState.contains(holder->UniqueID),"healed state");
        checkLegacy();
        checkLegacyShieldRecords();
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
