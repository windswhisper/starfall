#pragma once
#include "LuaTimers.h"
#include "PassiveTypes.h"

class CCINIClass;

namespace ActionSequence {
using PassiveRules::ScriptFields;
using PassiveRules::Skill;
using std::string;
using std::vector;

// The engine persists this type marker in its own superweapon stream.
constexpr auto type=static_cast<SuperWeaponType>(-0x534651);
using Sections=std::map<string,ScriptFields>;
struct Cast {string typeId;int owner;CellStruct point;};

extern Sections mapFields;
extern std::map<string,vector<Skill>> definitions;
extern std::map<WarheadTypeClass*,const Skill*> warheadScripts;
extern LuaTimers::Store timers;
extern std::map<int,string> mapScripts;
extern std::map<DWORD,Cast> casts;
extern DWORD lastCast;

bool fieldName(const string& key);
void collect(CCINIClass* ini,Sections& out);
ScriptFields fields(const Sections& rules,const string& id);
bool prepare(ScriptFields& fields,bool selected=false);
void load();
void reset();
const Skill* stepFor(const Cast& cast,const string& section);
void retire(DWORD id);
bool consumeCommand(int sender,int index,CellStruct point);

// Implemented by the Starfall action executor.
void executeSteps(DWORD id,const Cast& cast,const vector<Skill>& steps);

template<class W> void write(W& w) {
    w.word(lastCast);w.word(static_cast<DWORD>(casts.size()));
    for(const auto& [id,c]:casts) {w.word(id);w.str(c.typeId);w.word(c.owner);w.word(c.point.X);w.word(c.point.Y);}
    timers.write(w);
    w.word(static_cast<DWORD>(mapFields.size()));
    for(const auto& [id,fields]:mapFields) {
        w.str(id);w.word(static_cast<DWORD>(fields.size()));
        for(const auto& [key,value]:fields){w.str(key);w.str(value);}
    }
}
// Keep this block staged until the enclosing save stream has also been validated.
struct SavedState {
    decltype(casts) casts;
    LuaTimers::Store timers;
    Sections mapFields;
    DWORD lastCast=0;
    void commit() {
        ActionSequence::casts.swap(casts);
        ActionSequence::timers=std::move(timers);
        ActionSequence::mapFields.swap(mapFields);
        ActionSequence::lastCast=lastCast;
    }
};
template<class R> SavedState readSaved(R& r) {
    auto last=r.word();decltype(casts) restored;
    for(auto n=r.word();n && r.ok;--n) {
        auto id=r.word();auto typeId=r.str();auto house=r.word(),x=r.word(),y=r.word();
        bool noPoint=x==UINT32_MAX && y==UINT32_MAX && typeId.starts_with("@map:");
        if(!id || id>last || typeId.empty() || house>127 || (!noPoint && (x>=512 || y>=512)) ||
           !restored.emplace(id,Cast{typeId,static_cast<int>(house),{static_cast<short>(x),static_cast<short>(y)}}).second) r.ok=false;
    }
    LuaTimers::Store pending;if(r.ok) pending.read(r);
    for(const auto& entry:pending.queue) if(!restored.contains(entry.second.owner)) r.ok=false;
    Sections map;
    for(auto n=r.word();n && r.ok;--n) {
        auto id=r.str(2047);auto count=r.word();
        if(id.empty() || map.contains(id)) {r.ok=false;break;}
        auto& f=map[id];
        while(count-- && r.ok) {
            auto key=r.str(2047),value=r.str(32767);
            if((!fieldName(key) && id!="starfall.mapscripts") || !f.emplace(key,value).second) r.ok=false;
        }
    }
    return {std::move(restored),std::move(pending),std::move(map),last};
}
}
