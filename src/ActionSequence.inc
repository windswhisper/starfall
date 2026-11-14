// Native SW types carry this marker through the game's own save stream.
namespace ActionSequence {
constexpr auto type=static_cast<SuperWeaponType>(-0x534651);
using Sections=std::map<string,ScriptFields>;
Sections mapFields;
std::map<string,vector<Skill>> definitions;
LuaTimers::Store timers;
struct Cast {string typeId;int owner;CellStruct point;};
std::map<int,string> mapScripts;
std::map<DWORD,Cast> casts;
DWORD lastCast=0;

bool fieldName(const string& key) {
    // The map overlay also persists warhead house flags needed by single-target damage.
    return key=="affectsowner" || key=="affectsenemies" || key=="affectsneutral" ||
        key=="starfall.actions" || key=="starfall.target" || key=="action" || key=="script" || key=="function" ||
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
bool monetary(const string& name) {return name=="givecredits" || name=="giveresearchpoints";}
// Translate the SW's point and house to the location/owner paths in the
// existing action executor. A unit action in Lua must carry a real unit handle.
bool prepare(ScriptFields& f,bool selected=false) {
    auto name=lower(f["action"]);
    string target=f.contains("action.target")?lower(f.at("action.target")):"";
    if(name=="runscript") return !selected && target.empty();
    if(includes({"replaceweapon","setdamage","blockdamage","selfdestruct","stealpassive","sendsignal"},name)) return false;
    if(selected) f["action.target"]="target";
    else if(positionalAction(name) && (target.empty() || target=="point" || target=="victimlocation"))
        f["action.target"]="victimlocation";
    else if(monetary(name) && (target.empty() || target=="owner"))
        f["action.target"]="self";
    else return false;
    if(auto it=f.find("action.owner");it!=f.end() && lower(it->second)=="owner") it->second="self";
    return true;
}
void load() {
    definitions.clear();
    mapScripts.clear();
    Sections rules;
    for(auto sw:SuperWeaponTypeClass::Array) {
        if(!sw || sw->Type!=type) continue;
        if(definitions.empty()) collect(CCINIClass::INI_Rules,rules);
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
void reset() {definitions.clear();mapScripts.clear();mapFields.clear();casts.clear();lastCast=0;timers={};}
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
