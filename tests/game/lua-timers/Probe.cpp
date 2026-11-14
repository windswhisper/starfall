#include "../../../src/Starfall.cpp"
#include <sstream>
namespace {
DWORD aId=0,bId=0,enemyId=0,dead=0;
CellStruct center{};
struct Strike { unsigned frame;DWORD owner;CellStruct cell; };
vector<Strike> trace;
void check(bool ok,const char* label) {
    std::ofstream("lua-timers.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void restoreIds() {
    std::ifstream("casters.txt")>>aId>>bId>>enemyId>>center.X>>center.Y;
}
string suffix() {
    std::ostringstream out;
    for(const auto& hit:trace) if(hit.frame>49)
        out<<hit.frame<<" "<<hit.owner<<" "<<hit.cell.X<<" "<<hit.cell.Y<<"\n";
    return out.str();
}
bool series(unsigned first,int count) {
    if(trace.size()!=count*2u) return false;
    std::set<std::pair<short,short>> cells;
    for(int i=0;i<count*2;++i) {
        const auto& s=trace[i];
        int dx=s.cell.X-center.X,dy=s.cell.Y-center.Y;
        if(s.frame!=first+(i/2)*4 || s.owner!=(i%2?bId:aId) || dx*dx+dy*dy>9) return false;
        cells.emplace(s.cell.X,s.cell.Y);
    }
    return cells.size()>1;
}
}
DEFINE_HOOK(0x4690D4,SF_TestLuaTimerImpact,6) {
    auto b=R->ESI<BulletClass*>();
    if(applyingDamage && b->Owner && (b->Owner->UniqueID==aId || b->Owner->UniqueID==bId) &&
       b->WH==WarheadTypeClass::Find("HE"))
        trace.push_back({static_cast<unsigned>(Unsorted::CurrentFrame),b->Owner->UniqueID,CellClass::Coord2Cell(*R->Base<CoordStruct*>(8))});
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestLuaTimers,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    auto frame=Unsorted::CurrentFrame;
    if(load && !aId) restoreIds();
    if(!load && frame==20) {
        vector<UnitClass*> human;UnitClass* enemy=nullptr;
        auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
        for(auto u:UnitClass::Array) {
            if(u->Owner==house) human.push_back(u);
            if(!enemy && u->Owner==HouseClass::FindByPlayerAt(HouseClass::PlayerAtB)) enemy=u;
        }
        check(human.size()>=3 && enemy && skills.contains("sf_areabarrage"),"fixture");
        aId=human[0]->UniqueID;bId=human[1]->UniqueID;enemyId=enemy->UniqueID;
        center=enemy->GetMapCoords();
        std::ofstream("casters.txt")<<aId<<" "<<bId<<" "<<enemyId<<" "<<center.X<<" "<<center.Y;
        enemy->IronCurtain(300,enemy->Owner,false);enemy->Deactivate();
        for(int i=0;i<2;++i) {
            auto u=human[i];u->Deactivate();u->IronCurtain(300,house,false);
            grantToUnit(u,PassiveGrant{{"sf_areabarrage","sf_timercheck"},-1});
            capture("ondealdamage",u,enemy,u,enemy,4);
            capture("onfire",u,enemy,u,enemy,0);
        }
        dispatch();
        auto disposable=human[2];
        grantToUnit(disposable,PassiveGrant{{"sf_timercheck"},-1});
        capture("ondealdamage",disposable,enemy,disposable,enemy,1);dispatch();
        dead=disposable->UniqueID;
        check(scriptTimers.state.contains({dead,"sf_timercheck","task"}),"disposable scheduled");
        disposable->RegisterDestruction(nullptr);disposable->UnInit();
    }
    if(!load && frame==22) {
        check(!scriptTimers.state.contains({dead,"sf_timercheck","task"}) && scriptTimers.queue.size()==6,"owner cleanup");
    }
    if(!load && frame==49) {
        check(series(20,8),"eight strikes per caster");
        check(ScenarioClass::SaveGame("Saved Games\\LuaTimers.sav",L"Lua Timers"),"saved");
    }
    if(frame==100) {
        check(series(load?52:20,load?12:20),load?"twelve remaining strikes per caster":"twenty strikes per caster");
        auto& state=scriptTimers.state;
        for(auto id:{aId,bId}) {
            auto done=state.find({id,"sf_timercheck","done"});
            check(done!=state.end() && done->second.type==LuaTaskData::Type::True &&
                  !state.contains({id,"sf_timercheck","cancel"}) && !state.contains({id,"sf_timercheck","failed"}) &&
                  scriptTimers.queue.empty() && scriptTimers.byId.empty(),"timer data and cancellation");
        }
        if(load) {
            std::ifstream file("impacts.txt",std::ios::binary);
            string expected((std::istreambuf_iterator<char>(file)),{});
            check(suffix()==expected,"identical frames and impact cells");
        } else {std::ofstream("impacts.txt",std::ios::binary)<<suffix();}
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
