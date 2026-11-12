#include "../../../src/Starfall.cpp"
#include <ScriptClass.h>

// Runs after the production frame hook in the isolated test DLL.
DEFINE_HOOK(0x55B4EB, SF_IntegrationTestFrame, 5) {
    static const bool enabled=GetFileAttributesA("Starfall.test")!=INVALID_FILE_ATTRIBUTES;
    if(!enabled) return 0;
    const auto f=Unsorted::CurrentFrame;
    static bool boot=true;
    if(boot) {std::ofstream("integration.log",std::ios::app)<<"boot "<<f<<"\n";boot=false;}
    if(f==12) {
        for(auto action:TActionClass::Array) if(static_cast<int>(action->ActionKind)==9601 && action->Param4==-1) {
            for(auto unit:UnitClass::Array) if(!_stricmp(unit->GetTechnoType()->ID,"HTNK")) {
                auto cell=unit->GetMapCoords();
                bool ok=action->Execute(unit->Owner,unit,nullptr,cell);
                std::ofstream("integration.log",std::ios::app)<<"object action "<<ok<<"\n";
                break;
            }
            break;
        }
    }
    if(f==60) {
        const bool ok=ScenarioClass::SaveGame("Saved Games\\ScriptGrantTest.sav",L"ScriptGrantTest");
        std::ofstream("integration.log",std::ios::app)<<"saved "<<ok<<"\n";
    }
    if(f==1 || f==25 || f==60 || f==110 || f==210 || f==510) {
        std::ofstream log("integration.log",std::ios::app);
        log<<"frame "<<f<<std::endl;
        std::map<string,int> acquired;
        for(auto unit:TechnoClass::Array) if(unit && unit->Owner) {
            auto g=warheadGrants.find(unit->UniqueID);
            if(g!=warheadGrants.end()) for(auto& [id,time]:g->second.skills)
                acquired[id+" "+unit->GetTechnoType()->ID+" "+std::to_string(unit->Owner->ArrayIndex)]++;
        }
        for(auto& [name,n]:acquired) log<<name<<" = "<<n<<"\n";
        for(auto tm:TeamClass::Array) if(tm && tm->Type && !strcmp(tm->Type->ID,"0F519001"))
            log<<"team line="<<(tm->CurrentScript?tm->CurrentScript->CurrentMission:-5)<<" success="<<tm->AchievedGreatSuccess<<" members="<<tm->TotalObjects<<"\n";
    }
    if(f>=510) TerminateProcess(GetCurrentProcess(),0);
    return 0;
}
