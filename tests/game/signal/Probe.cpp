#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* name) {
    std::ofstream("signal.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
}
DEFINE_HOOK(0x55B4EB,SF_TestSignal,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(!loading && frame>=25) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    if((!loading && frame!=20) || (loading && frame!=30)) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    int expected=0;DWORD receiverId=0;if(loading) std::ifstream("signal.id")>>receiverId>>expected;
    UnitClass *sender=nullptr,*receiver=nullptr,*enemy=nullptr;
    for(auto u:UnitClass::Array) {
        if(u->Owner==house && !_stricmp(u->Type->ID,"MTNK")) {
            if(loading && u->UniqueID==receiverId) receiver=u;
            else if(!sender) sender=u;
            else if(!loading && !receiver) receiver=u;
        }
        if(!enemy && u->Owner==foe) enemy=u;
    }
    check(sender && receiver && enemy,"units");
    if(!loading) {
        check(skills.contains("send") && skills.contains("receive") && skills.contains("loopstart") && skills.contains("loopreceive"),"configuration");
        grantToUnit(sender,PassiveGrant{{"send","loopstart"},-1});
        grantToUnit(receiver,PassiveGrant{{"receive","loopreceive"},-1});
        int balance=Research::balance(house->ArrayIndex);
        int reward=sender->Type->Cost+enemy->Type->Cost+20;
        capture("ondealdamage",sender,enemy,sender,enemy,100);dispatch();
        check(pending.size()==1 && pending[0].victim==enemy->UniqueID && pending[0].attacker==sender->UniqueID && pending[0].signalValue==20,"broadcast snapshots");
        std::ofstream("signal.id")<<receiver->UniqueID<<" "<<balance+reward;
        check(ScenarioClass::SaveGame("Saved Games\\Signal.sav",L"Signal"),"saved");
        dispatch();check(Research::balance(house->ArrayIndex)==balance+reward,"signal values");
        capture("ondealdamage",sender,enemy,sender,enemy,100);dispatch();
        int hp=enemy->Health;enemy->Health=0;dispatch();
        check(Research::balance(house->ArrayIndex)==balance+reward*2 && enemy->Health==0,"dead target snapshot");
        enemy->Health=hp;
        capture("ondealdamage",sender,enemy,sender,enemy,100);dispatch();
        hp=sender->Health;sender->Health=0;dispatch();
        check(Research::balance(house->ArrayIndex)==balance+reward*3 && sender->Health==0,"dead sender snapshot");
        sender->Health=hp;
        capture("onkill",sender,enemy,sender,enemy,0);dispatch();
        for(int i=0;i<20;++i) dispatch();
        check(pending.empty(),"limited chain");
    } else {
        dispatch();check(Research::balance(house->ArrayIndex)==expected,"signal values loaded");
        dispatch();check(Research::balance(house->ArrayIndex)==expected,"once after load");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
