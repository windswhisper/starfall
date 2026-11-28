#include "ActionSequence.h"
#include "ActiveSkills.h"
#include "ConfigText.h"
#include "PassiveRules.h"
#include <CCINIClass.h>
#include <HouseClass.h>
#include <SuperClass.h>
#include <Unsorted.h>
#include <WarheadTypeClass.h>

namespace ActionSequence {
using namespace PassiveRules;
using ConfigText::includes;
using ConfigText::lower;
using ConfigText::number;
using ConfigText::trim;

Sections mapFields;
std::map<string,vector<Skill>> definitions;
std::map<WarheadTypeClass*,const Skill*> warheadScripts;
LuaTimers::Store timers;
std::map<int,string> mapScripts;
std::map<DWORD,Cast> casts;
DWORD lastCast=0;

bool fieldName(const string& key) {
    // The saved map overlay also retains custom unit labels and warhead house flags.
    return key=="starfall.detonatescript" || key=="starfall.detonatefunction" ||
        key=="starfall.firescript" || key=="starfall.firefunction" || key=="affectsowner" || key=="affectsenemies" || key=="affectsneutral" ||
        key=="tipslabel" || key=="starfall.actions" || key=="starfall.target" || key=="action" || key=="script" || key=="function" ||
        key.starts_with("action.") || key.starts_with("script.param.");
}
void collect(CCINIClass* ini,Sections& out) {
    if(!ini) return;
    for(auto s=ini->Sections.First();s && s->IsValid();s=s->Next()) {
        if(!s->Name) continue;
        for(auto node=s->Entries.GenericList::First();node && node->IsValid();node=node->Next()) {
            auto entry=static_cast<INIClass::INIEntry*>(node);
            if(!entry->Key || !entry->Value) continue;
            auto key=lower(entry->Key);
            if(!fieldName(key) && lower(s->Name)!="starfall.mapscripts") continue;
            auto value=string(entry->Value);
            out[lower(s->Name)][key]=trim(value.substr(0,value.find(';')));
        }
    }
}
ScriptFields fields(const Sections& rules,const string& id) {
    ScriptFields values;
    if(auto found=rules.find(id);found!=rules.end()) values=found->second;
    char buffer[32768]{};
    GetPrivateProfileSectionA(id.c_str(),buffer,sizeof(buffer),configPath);
    for(char* line=buffer;*line;line+=strlen(line)+1) {
        string entry(line);auto eq=entry.find('=');
        if(eq==string::npos) continue;
        auto key=lower(trim(entry.substr(0,eq)));
        if(fieldName(key)) values[key]=trim(entry.substr(eq+1,entry.find(';',eq+1)-eq-1));
    }
    if(auto found=mapFields.find(id);found!=mapFields.end())
        for(const auto& [key,value]:found->second) values[key]=value;
    return values;
}
bool houseAction(const string& name) {return name=="changeresearchcounter" || name=="givecredits" || name=="giveresearchpoints" || name=="grantrandomresearch" || name=="grantfreeresearch" || name=="grantresearch";}
// Translate the SW's point and house to the location/owner paths in the
// existing action executor. A unit action in Lua must carry a real unit handle.
bool prepare(ScriptFields& f,bool selected) {
    auto name=lower(f["action"]);
    string target=f.contains("action.target")?lower(f.at("action.target")):"";
    if(name=="runscript") return !selected && target.empty();
    if(includes({"replaceweapon","setdamage","blockdamage","selfdestruct","stealpassive","sendsignal"},name)) return false;
    if(selected) f["action.target"]="target";
    else if(positionalAction(name) && (target.empty() || target=="point" || target=="victimlocation"))
        f["action.target"]="victimlocation";
    else if(houseAction(name) && (target.empty() || target=="owner"))
        f["action.target"]="self";
    else return false;
    if(auto it=f.find("action.owner");it!=f.end() && lower(it->second)=="owner") it->second="self";
    return true;
}
void load() {
    warheadScripts.clear();
    definitions.clear();
    mapScripts.clear();
    Sections rules;
    collect(CCINIClass::INI_Rules,rules);
    for(auto wh:WarheadTypeClass::Array) {
        if(!wh) continue;
        auto cfg=fields(rules,lower(wh->ID));
        auto file=cfg.find("starfall.detonatescript");
        if(file==cfg.end() || file->second.empty()) continue;
        auto function=cfg.find("starfall.detonatefunction");
        ScriptFields action{{"action","runscript"},{"action.script",file->second},
            {"action.function",function==cfg.end()?"onDetonate":function->second}};
        Skill step{};step.id="@warhead:"+lower(wh->ID);step.trigger="onwarheaddetonate";
        for(const auto& [key,value]:cfg) if(key.starts_with("script.param.") && key.size()>13)
            step.scriptParams[key.substr(13)]=value;
        if(!readAction(step,wh->ID,&action)) continue;
        auto& steps=definitions[step.id];
        steps.push_back(std::move(step));
        warheadScripts.emplace(wh,&steps.front());
    }
    for(auto sw:SuperWeaponTypeClass::Array) {
        if(!sw || sw->Type!=type) continue;
        auto id=lower(sw->ID);
        auto& steps=definitions[id];
        auto settings=fields(rules,id);
        const auto& raw=settings["starfall.actions"];
        vector<string> list;
        for(size_t start=0;start<=raw.size();) {
            auto end=raw.find(',',start);
            auto section=lower(trim(raw.substr(start,end-start)));
            if(section.empty()) {list.clear();break;}
            list.push_back(std::move(section));
            if(end==string::npos) break;
            start=end+1;
        }
        if(list.empty() || (settings.contains("starfall.target") && lower(settings["starfall.target"])!="point")) {
            invalidField(id,"ActionSequence requires Starfall.Actions and a Point target");continue;
        }
        for(const auto& section:list) {
            auto f=fields(rules,section);
            Skill step{};step.id=section;step.trigger="onsuperweapon";
            for(const auto& [key,value]:f) if(key.starts_with("script.param.")) step.scriptParams[key.substr(13)]=value;
            if(section.size()>127 || !prepare(f) || !readAction(step,section,&f)) {
                invalidField(id,"Starfall.Actions -> ["+section+"] invalid superweapon action or target");
                steps.clear();break;
            }
            steps.push_back(std::move(step));
        }
    }
    auto registry=sectionKeys("Starfall.MapScripts");
    if(auto map=mapFields.find("starfall.mapscripts");map!=mapFields.end())
        for(const auto& [key,value]:map->second) registry.insert(key);
    for(const auto& key:registry) {
        int index=number(key);
        if(index<0 || key!=std::to_string(index)) {invalidField("Starfall.MapScripts",key);continue;}
        auto section=read("Starfall.MapScripts",key.c_str());
        if(auto map=mapFields.find("starfall.mapscripts");map!=mapFields.end())
            if(auto value=map->second.find(key);value!=map->second.end()) section=value->second;
        if(section.empty() || section.size()>100) {invalidField("Starfall.MapScripts",key);continue;}
        auto name="@map:"+std::to_string(index);
        ScriptFields f{{"action","runscript"},{"action.script",read(section,"Script")},{"action.function",read(section,"Function")}};
        for(const auto& field:sectionKeys(section)) if(field.starts_with("script.param.") && field.size()>13)
            f[field]=readRaw(section,field.c_str());
        if(auto map=mapFields.find(lower(section));map!=mapFields.end()) for(const auto& [field,value]:map->second) {
            if(field=="script" || field=="function") f["action."+field]=value;
            else if(field.starts_with("script.param.")) f[field]=value;
        }
        Skill step{};step.id=name;step.trigger="onsuperweapon";
        for(const auto& [field,value]:f) if(field.starts_with("script.param.")) step.scriptParams[field.substr(13)]=value;
        if(!readAction(step,section,&f)) {invalidField(section,"invalid map script");continue;}
        definitions[name].push_back(std::move(step));mapScripts.emplace(index,name);
    }
}
const Skill* stepFor(const Cast& cast,const string& section) {
    auto name=lower(cast.typeId);
    if(name.starts_with("@map:")) {auto split=name.find(':',5);if(split!=string::npos) name.resize(split);}
    auto it=definitions.find(name);
    if(it!=definitions.end()) for(const auto& step:it->second) if(step.id==section) return &step;
    return nullptr;
}
void retire(DWORD id) {
    for(const auto& [order,task]:timers.queue) if(task.owner==id) return;
    timers.forget(id);casts.erase(id);
}
void reset() {warheadScripts.clear();definitions.clear();mapScripts.clear();mapFields.clear();casts.clear();lastCast=0;timers={};}

static bool fireSequence(SuperClass* sw,CellStruct point) {
    if(!sw->Owner || sw->Owner->Defeated || !ActiveSkills::playable(point)) return false;
    Research::ensureReady();
    auto steps=definitions.find(lower(sw->Type->ID));
    if(steps==definitions.end() || steps->second.empty() || lastCast==UINT32_MAX) return false;
    auto id=++lastCast;
    auto& cast=casts.emplace(id,Cast{sw->Type->ID,sw->Owner->ArrayIndex,point}).first->second;
    executeSteps(id,cast,steps->second);
    retire(id);
    return true;
}
bool consumeCommand(int sender,int index,CellStruct point) {
    if(index<0 || index>=SuperWeaponTypeClass::Array.Count ||
       SuperWeaponTypeClass::Array.Items[index]->Type!=ActionSequence::type) return false;
    if(sender<0 || sender>=HouseClass::Array.Count) return true;
    auto house=HouseClass::Array.Items[sender];
    if(!house || house->IsObserver() || index>=house->Supers.Count) return true;
    auto sw=house->Supers.Items[index];
    if(!sw || !sw->IsPresent || !sw->IsReady || sw->IsSuspended || !fireSequence(sw,point)) return true;
    sw->Reset();
    if(sw->IsOneTime) {sw->IsOneTime=false;sw->Lose();}
    if(house==HouseClass::CurrentPlayer && Unsorted::CurrentSWType==index) Unsorted::CurrentSWType=-1;
    return true;
}
}

// Read the custom name at the native Type parser. Apart from this effect
// marker and a point cursor, the SW remains an ordinary per-house instance.
DEFINE_HOOK(0x6CEC32,SF_ActionSequenceType,7) {
    if(_stricmp(R->lea_Stack<const char*>(0x58),"ActionSequence")) return 0;
    auto sw=R->EBP<SuperWeaponTypeClass*>();
    sw->Type=ActionSequence::type;sw->Action=Action::ParaDrop;
    return 0x6CEC84;
}
// At this site native ClickFire has checked availability. Rejoin its normal
// charge/one-shot bookkeeping immediately after the built-in effect call.
DEFINE_HOOK(0x6CBAF9,SF_ActionSequenceClick,8) {
    auto sw=R->ESI<SuperClass*>();
    if(sw->Type->Type!=ActionSequence::type) return 0;
    auto point=R->Stack<CellStruct*>(0x24);
    if(!sw->IsPresent || !sw->IsReady || sw->IsSuspended || !point || !ActionSequence::fireSequence(sw,*point)) return 0x6CBC94;
    if(sw->Owner==HouseClass::CurrentPlayer && Unsorted::CurrentSWType==sw->Type->ArrayIndex)
        Unsorted::CurrentSWType=-1;
    return 0x6CBB0A;
}
// Direct engine launches (e.g. scripted launches) bypass ClickFire.
DEFINE_HOOK(0x6CC390,SF_ActionSequenceLaunch,6) {
    auto sw=R->ECX<SuperClass*>();
    if(sw->Type->Type!=ActionSequence::type) return 0;
    if(auto point=R->Stack<CellStruct*>(4)) ActionSequence::fireSequence(sw,*point);
    return 0x6CC482;
}
DEFINE_HOOK(0x6CEEB0,SF_SelectedSequence,8) {
    int index=Unsorted::CurrentSWType;
    if(index>=0 && index<SuperWeaponTypeClass::Array.Count) {
        auto sw=SuperWeaponTypeClass::Array.Items[index];
        if(sw && sw->Type==ActionSequence::type && sw->Action==R->ECX<Action>()) {
            R->EAX(sw);return 0x6CEEE5;
        }
    }
    return 0;
}
