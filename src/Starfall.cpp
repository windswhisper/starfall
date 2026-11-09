#include "Research.h"
#include <algorithm>
#include <fstream>
#include <random>
#include <Unsorted.h>
#include <charconv>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <TechnoClass.h>
#include <HouseClass.h>
#include <ScenarioClass.h>
#include <RulesClass.h>
#include <BuildingClass.h>
#include <InfantryClass.h>
#include <TActionClass.h>
#include <CCINIClass.h>

using std::string;
using std::vector;

namespace {
const char* registry = "Starfall.PassiveTypes";
char configPath[MAX_PATH]{};
string trim(string s) {
    auto start = s.find_first_not_of(" \r\n\t");
    return start == string::npos ? "" : s.substr(start, s.find_last_not_of(" \r\n\t") - start + 1);
}
string lower(string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
vector<string> split(const string& s) {
    vector<string> out;
    size_t p = 0;
    while (p < s.size()) {
        auto end = s.find(',', p);
        auto item = lower(trim(s.substr(p, end - p)));
        if (!item.empty() && std::find(out.begin(), out.end(), item) == out.end()) out.push_back(item);
        if (end == string::npos) break;
        p = end + 1;
    }
    return out;
}
string read(const string& section, const char* key, const char* fallback = "") {
    char buf[2048]{};
    GetPrivateProfileStringA(section.c_str(), key, "", buf, sizeof(buf), configPath);
    if (!buf[0] && !CCINIClass::INI_Rules) return lower(fallback);
    if (!buf[0])
        CCINIClass::INI_Rules->ReadString(section.c_str(), key, fallback, buf, sizeof(buf));
    return lower(trim(string(buf).substr(0,string(buf).find(';'))));
}
int number(string s, bool percent = false) {
    if (percent && !s.empty() && s.back() == '%') s.pop_back();
    int n = 0;
    auto r = std::from_chars(s.data(), s.data()+s.size(), n);
    return r.ec == std::errc{} && r.ptr == s.data()+s.size() && n >= 0 ? n : -1;
}
bool includes(const vector<string>& list, const string& s) {
    return std::find(list.begin(),list.end(),s) != list.end();
}
struct Skill {
    string id, trigger, action, target;
    vector<string> houses, kinds;
    int count, chance, cooldown, health, amount, damagePercent, minDamage;
};
std::map<string, Skill> skills;
std::map<string, vector<string>> bindings;
bool ready = false;
std::wstring displayText(const string& value) {
    if(value.empty()) return {};
    UINT cp=CP_UTF8;
    int n=MultiByteToWideChar(cp,MB_ERR_INVALID_CHARS,value.c_str(),-1,nullptr,0);
    if(n==0) { cp=CP_ACP; n=MultiByteToWideChar(cp,0,value.c_str(),-1,nullptr,0); }
    if(n<1) return {};
    std::wstring result(n,0);
    MultiByteToWideChar(cp,0,value.c_str(),-1,result.data(),n);
    result.pop_back();
    return result;
}
// A reference to an unknown ID leaves its project blocked. A topological walk
// also detects loops and projects that depend on a broken chain without recursion.
void validateResearchChains() {
    auto& options=Research::options;
    std::map<string,size_t> index;
    for(size_t i=0;i<options.size();++i) index.emplace(options[i].id,i);
    vector<vector<size_t>> dependents(options.size());
    vector<size_t> remaining(options.size()), roots;
    for(size_t i=0;i<options.size();++i) {
        auto& option=options[i];
        option.validChain=false;
        remaining[i]=option.prerequisites.size();
        if(remaining[i]==0) roots.push_back(i);
        for(const auto& id:option.prerequisites) {
            auto parent=index.find(id);
            if(parent!=index.end()) dependents[parent->second].push_back(i);
        }
    }
    for(size_t head=0;head<roots.size();++head) {
        auto i=roots[head]; options[i].validChain=true;
        for(auto next:dependents[i]) if(--remaining[next]==0) roots.push_back(next);
    }
    for(const auto& o:options) if(!o.validChain)
        OutputDebugStringA(("Starfall: missing or cyclic research prerequisite: "+o.id+"\n").c_str());
}
void readResearch() {
    using Research::Option;
    const Option defaults[]={
        {"sf_repair","sf_researchrepair","sfrheal.pcx",{"mtnk"},L"战场维修",L"击杀时修复自身",L"击杀敌人时，额外恢复 {amount} 点生命"},
        {"sf_drain","sf_lifesteal","sfrdrain.pcx",{"mtnk"},L"伤害吸血",L"造成伤害时吸取生命",L"按实际伤害的 {percent}% 恢复生命"},
        {"sf_riposte","sf_counter","sfrripst.pcx",{"mtnk"},L"受击反击",L"受击时反击攻击者",L"对攻击者造成 {amount} 点基础伤害"}
    };
    // Preserve the original bytes/case of UI text; Windows profile API can mix code pages.
    std::ifstream file(configPath,std::ios::binary);
    std::map<string,std::map<string,string>> fields;
    string line,section;
    while(std::getline(file,line)) {
        if(line.starts_with("\xEF\xBB\xBF")) line.erase(0,3);
        line=trim(line.substr(0,line.find(';')));
        if(line.size()>2 && line.front()=='[' && line.back()==']') {
            section=lower(trim(line.substr(1,line.size()-2))); fields.try_emplace(section); continue;
        }
        auto eq=line.find('=');
        if(eq!=string::npos) fields[section][lower(trim(line.substr(0,eq)))]=trim(line.substr(eq+1));
    }
    auto field=[&](const string& sec,const char* key,const string& fallback=string{}) {
        auto it=fields.find(sec);
        if(it!=fields.end()) {
            auto item=it->second.find(lower(key));
            if(item!=it->second.end()) return item->second;
        }
        char buf[2048]{};
        if(CCINIClass::INI_Rules) CCINIClass::INI_Rules->ReadString(sec.c_str(),key,"",buf,sizeof(buf));
        return buf[0]?trim(buf):fallback;
    };
    Research::options.clear();
    string heading=field("starfall.research","Title");
    Research::title=heading.empty()?L"请选择升级":displayText(heading);
    const string list="starfall.researchtypes";
    std::map<int,string> entries;
    auto addEntry=[&](const string& key,const string& value) {
        int index=number(trim(key));
        if(index>=0) entries[index]=lower(trim(value));
    };
    bool registered=fields.contains(list);
    if(auto* ini=CCINIClass::INI_Rules; ini && ini->GetSection(list.c_str())) {
        registered=true;
        for(int i=0;i<ini->GetKeyCount(list.c_str());++i) {
            auto key=ini->GetKeyName(list.c_str(),i);
            char value[256]{};
            ini->ReadString(list.c_str(),key,"",value,sizeof(value));
            addEntry(key,value);
        }
    }
    vector<string> ids;
    if(registered) {
        for(const auto& [key,value]:fields[list]) addEntry(key,value);
        std::set<string> seen;
        for(const auto& [key,id]:entries)
            if(!id.empty() && seen.insert(id).second) ids.push_back(id);
    } else {
        ids=split(field("starfall.research","Options","SF_Repair,SF_Drain,SF_Riposte"));
    }
    for(const auto& id:ids) {
        if(Research::options.size()>SHRT_MAX) break;
        if(id.size()>127) continue;
        Option o{}; o.id=id; o.name=displayText(id);
        for(const auto& d:defaults) if(id==d.id) o=d;
        const auto effect=lower(field(id,"Effect","GrantPassive"));
        using E=Research::Effect;
        o.effect=effect=="grantpassive"?E::Passive:effect=="givecredits"?E::Credits:
            effect=="unlocktech"?E::Technology:effect=="convertinfantry"?E::ConvertInfantry:E::Invalid;
        o.credits=number(field(id,"Credits","0"));
        o.technology=lower(field(id,"Technology"));
        o.from=lower(field(id,"FromType")); o.to=lower(field(id,"ToType"));
        o.passive=lower(field(id,"Passive",o.passive));
        o.icon=field(id,"Icon",o.icon);
        o.countries=split(field(id,"Countries"));
        o.prerequisites=split(field(id,"Prerequisites"));
        o.types=split(field(id,"TargetTypes",o.types.empty()?"":"MTNK"));
        auto label=field(id,"Name"),brief=field(id,"Summary"),detail=field(id,"Detail");
        if(!label.empty()) o.name=displayText(label);
        if(!brief.empty()) o.summary=displayText(brief);
        if(!detail.empty()) o.detail=displayText(detail);
        Research::options.emplace_back(std::move(o));
    }
    validateResearchChains();
}
void readSkills() {
    GetFullPathNameA("Starfall.ini", MAX_PATH, configPath, nullptr);
    std::set<string> ids;
    if (CCINIClass* ini = CCINIClass::INI_Rules) {
        for (int i=0; i<ini->GetKeyCount(registry); ++i) {
            char id[128]{};
            ini->ReadString(registry,ini->GetKeyName(registry,i),"",id,sizeof(id));
            ids.insert(lower(trim(id)));
        }
    }
    char keys[32768]{};
    GetPrivateProfileStringA(registry,nullptr,"",keys,sizeof(keys),configPath);
    for (char* k=keys; *k; k+=strlen(k)+1) ids.insert(read(registry,k));
    for (auto& id : ids) {
        if (id.empty() || id.size()>127) continue;
        Skill s{};
        s.id=id; s.trigger=read(id,"Trigger"); s.action=read(id,"Action");
        s.target=read(id,"Action.Target","Self");
        s.houses=split(read(id,"Filter.TargetHouse","All"));
        s.kinds=split(read(id,"Filter.TargetKind","All"));
        s.count=number(read(id,"Condition.Count","1"));
        s.cooldown=number(read(id,"Cooldown","0"));
        s.chance=number(read(id,"Chance","100%"),true);
        auto threshold=read(id,"Condition.SelfHealthBelow");
        s.health=threshold.empty()?101:number(threshold,true);
        s.amount=number(read(id,"Action.Amount","0"));
        s.damagePercent=number(read(id,"Action.PercentOfDamage","0"),true);
        s.minDamage=number(read(id,"Condition.MinDamage","0"));
        if (s.count<=0 || s.cooldown<0 || s.chance<0 || s.chance>100 || s.health<0 ||
            (!threshold.empty() && s.health>100) || s.minDamage<0 || s.amount<0 || s.damagePercent<0 ||
            (s.amount>0)==(s.damagePercent>0) ||
            (s.action!="heal" && s.action!="damage" && s.action!="givecredits") ||
            (s.target!="self" && s.target!="attacker" && s.target!="victim") ||
            (s.trigger!="onkill" && s.trigger!="ondealdamage" && s.trigger!="ontakedamage")) {
            OutputDebugStringA(("Starfall: invalid passive "+id+"\n").c_str());
            continue;
        }
        skills.emplace(id,s);
    }
    readResearch();
    ready=true;
}

struct Progress { int count=0; unsigned next=0; };
using Counts = std::map<DWORD,std::map<string,Progress>>;
Counts counts;
struct Event {
    string trigger, kind, relation;
    DWORD holder, attacker, victim;
    int damage, hp, maxHp;
};
vector<Event> pending;
bool applyingDamage=false;
bool alive(TechnoClass* t) { return t && t->IsAlive && t->Health>0; }
const char* kind(TechnoClass* t) {
    switch(t->WhatAmI()) {
        case AbstractType::Infantry: return "infantry";
        case AbstractType::Unit: return "vehicle";
        case AbstractType::Aircraft: return "aircraft";
        case AbstractType::Building: return "building";
        default: return "other";
    }
}
const vector<string>& boundSkills(const char* typeID) {
    if (!ready) readSkills();
    auto [it, fresh] = bindings.try_emplace(typeID);
    if (fresh) it->second=split(read(typeID,"Starfall.Passives"));
    return it->second;
}
bool researchSkills(TechnoClass* unit,vector<string>* active=nullptr) {
    if(!unit->Owner) return false;
    auto unlocked=Research::unlocked.find(unit->Owner->ArrayIndex);
    if(unlocked==Research::unlocked.end()) return false;
    const auto type=lower(unit->GetTechnoType()->ID);
    bool found=false;
    for(const auto& o:Research::options) {
        if(o.effect!=Research::Effect::Passive || !unlocked->second.contains(o.id) || (!includes(o.types,"*") && !includes(o.types,type))) continue;
        found=true;
        if(!active) break;
        if(!includes(*active,o.passive)) active->push_back(o.passive);
    }
    return found;
}
void capture(const char* event, TechnoClass* holder, TechnoClass* other, TechnoClass* attacker, TechnoClass* victim, int damage) {
    if (applyingDamage || !alive(holder)) return;
    auto type = holder->GetTechnoType();
    if (boundSkills(type->ID).empty() && !researchSkills(holder)) return;
    const char* rel="none";
    if(other && other->Owner && holder->Owner)
        rel=other->Owner==holder->Owner?"self":holder->Owner->IsAlliedWith(other->Owner)?"allies":other->Owner->IsNeutral()?"neutral":"enemies";
    pending.push_back({event,other?kind(other):"none",rel,holder->UniqueID,attacker?attacker->UniqueID:0,
                       victim->UniqueID,damage,holder->Health,type->Strength});
}
using Objects = std::map<DWORD, TechnoClass*>;
TechnoClass* findLive(const Objects& objects,DWORD id) {
    auto it=objects.find(id);
    // Destructors invalidate entries during secondary damage.
    return it!=objects.end() && alive(it->second) ? it->second : nullptr;
}
Objects* activeObjects=nullptr;
bool apply(const Skill& s, const Event& e, TechnoClass* holder, const Objects& objects) {
    auto target=findLive(objects,s.target=="attacker"?e.attacker:s.target=="victim"?e.victim:e.holder);
    if (!target) return false;
    int amount=s.amount?s.amount:static_cast<int>(std::min<long long>(INT_MAX,1LL*e.damage*s.damagePercent/100));
    if (!amount) return false;
    if(s.action=="givecredits") {
        if(!target->Owner) return false;
        target->Owner->GiveMoney(amount);
    } else if(s.action=="heal") {
        int gain=std::min(amount,target->GetTechnoType()->Strength-target->Health);
        if(gain<=0) return false;
        target->Health+=gain;
        target->EstimatedHealth=target->Health;
        target->MarkForRedraw();
    } else {
        int before=target->Health;
        DWORD id=target->UniqueID;
        applyingDamage=true;
        target->ReceiveDamage(&amount,0,RulesClass::Instance->C4Warhead,holder,false,false,holder->Owner);
        applyingDamage=false;
        auto after=findLive(objects,id);
        return !after || after->Health<before;
    }
    return true;
}
void dispatch() {
    if(pending.empty()) return;
    Objects objects;
    for(auto* t : TechnoClass::Array) if(alive(t)) objects.emplace(t->UniqueID,t);
    activeObjects=&objects;
    auto events=std::move(pending);
    for(const auto& e:events) {
        auto holder=findLive(objects,e.holder);
        if(!holder) continue;
        auto active=boundSkills(holder->GetTechnoType()->ID);
        researchSkills(holder,&active);
        for(const auto& id:active) {
            auto it=skills.find(id);
            if(it==skills.end()) continue;
            const Skill& s=it->second;
            if(s.trigger!=e.trigger || e.damage<s.minDamage || (s.health<=100 && 100LL*e.hp>=1LL*s.health*e.maxHp) ||
               (!includes(s.houses,"all") && !includes(s.houses,e.relation)) ||
               (!includes(s.kinds,"all") && !includes(s.kinds,e.kind))) continue;
            auto& p=counts[e.holder][id];
            unsigned frame=Unsorted::CurrentFrame;
            if(frame<p.next || ++p.count<s.count) continue;
            p.count=0;
            if(s.chance==0 || (s.chance<100 && ScenarioClass::Instance->Random.RandomRanged(1,100)>s.chance)) continue;
            bool success=apply(s,e,holder,objects);
            holder=findLive(objects,e.holder);
            if(!holder) break;
            if(success) counts[e.holder][id].next=frame+s.cooldown;
        }
    }
    activeObjects=nullptr;
}
struct Income { int amount,interval; };
std::map<BuildingTypeClass*,Income> incomeTypes;
bool eligibleHouse(HouseClass* h) {
    return h && h->ArrayIndex>=0 && !h->Defeated && !h->IsObserver() && !h->IsNeutral();
}
void tickBuildings() {
    Research::ensureReady();
    for(auto* b:BuildingClass::Array) {
        if(!b || !b->Type) continue;
        auto [it,fresh]=incomeTypes.try_emplace(b->Type);
        if(fresh) it->second={number(read(b->Type->ID,"Starfall.ResearchPoints","0")),
                              number(read(b->Type->ID,"Starfall.ResearchInterval","150"))};
        auto income=it->second;
        if(income.amount<=0 || income.interval<=0) continue;
        auto id=b->UniqueID;
        if(!eligibleHouse(b->Owner) || !alive(b) || !b->IsOnMap || b->InLimbo ||
            b->CurrentMission==Mission::Selling || b->CurrentMission==Mission::Construction) {
            Research::producers.erase(id); continue;
        }
        int owner=b->Owner->ArrayIndex;
        auto [entry,added]=Research::producers.try_emplace(id,Research::Producer{owner,0});
        auto& timer=entry->second;
        if(timer.owner!=owner) timer={owner,0};
        if(b->IsUnderEMP() || !b->IsPowerOnline() || b->BeingWarpedOut) continue;
        if(++timer.elapsed>=income.interval) {
            Research::award(owner,income.amount); timer.elapsed=0;
        }
    }
}
void reset() { incomeTypes.clear(); Research::reset(); counts.clear(); pending.clear(); bindings.clear(); skills.clear(); ready=false; }
} // namespace

void Research::ensureReady() { if(!ready) readSkills(); }
// This engine build ships with Ares 3.0p1. Match both the PE metadata and
// entry bytes before calling its in-place conversion routine (Phobos AresAddressInit).
namespace {
using ConvertInfantryFn=bool(__stdcall*)(TechnoClass*,TechnoTypeClass*);
ConvertInfantryFn converter() {
    static const auto fn=[]()->ConvertInfantryFn {
        auto base=reinterpret_cast<const BYTE*>(GetModuleHandleW(L"Ares.dll"));
        if(!base) return nullptr;
        auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
        const BYTE entry[]={0x83,0xec,0x28,0x56,0x57,0x8b,0x7c,0x24,0x34};
        if(nt->FileHeader.TimeDateStamp!=0x61daa114 || nt->OptionalHeader.SizeOfImage!=0xdb000
            || memcmp(base+0x44130,entry,sizeof(entry))!=0) return nullptr;
        return reinterpret_cast<ConvertInfantryFn>(reinterpret_cast<uintptr_t>(base)+0x44130);
    }();
    return fn;
}
bool* technologyFlag(HouseClass* h,const string& tech) {
    if(tech=="allied") return &h->Side0TechInfiltrated;
    if(tech=="soviet") return &h->Side1TechInfiltrated;
    if(tech=="yuri") return &h->Side2TechInfiltrated;
    return nullptr;
}
bool matches(InfantryClass* infantry,HouseClass* h,TechnoTypeClass* type) {
    return infantry && infantry->Owner==h && alive(infantry) && infantry->Type==type;
}
}
bool Research::available(int option) {
    ensureReady();
    if(option<0 || option>=static_cast<int>(options.size())) return false;
    const auto& o=options[option];
    switch(o.effect) {
        case Effect::Passive: return !o.types.empty() && skills.contains(o.passive);
        case Effect::Credits: return o.credits>0;
        case Effect::Technology: return o.technology=="allied" || o.technology=="soviet" || o.technology=="yuri";
        case Effect::ConvertInfantry: {
            auto from=TechnoTypeClass::Find(o.from.c_str()),to=TechnoTypeClass::Find(o.to.c_str());
            return converter() && from && to && from!=to && from->WhatAmI()==AbstractType::InfantryType
                && to->WhatAmI()==AbstractType::InfantryType;
        }
        default: return false;
    }
}
bool Research::canApply(int house,int option) {
    const auto& o=options[option];
    if(o.effect==Effect::Passive) return true;
    if(house<0 || house>=HouseClass::Array.Count) return false;
    auto h=HouseClass::Array.Items[house]; if(!eligibleHouse(h)) return false;
    if(o.effect==Effect::Technology) {
        auto flag=technologyFlag(h,o.technology);return flag && !*flag;
    }
    if(o.effect==Effect::ConvertInfantry) {
        auto from=TechnoTypeClass::Find(o.from.c_str());
        for(auto infantry:InfantryClass::Array) if(matches(infantry,h,from)) return true;
        return false;
    }
    return o.effect==Effect::Credits;
}
bool Research::executeEffect(int house,int option) {
    if(!canApply(house,option)) return false;
    const auto& o=options[option];
    if(o.effect==Effect::Passive) return true;
    auto h=HouseClass::Array.Items[house];
    if(o.effect==Effect::Credits) {h->GiveMoney(o.credits);return true;}
    if(o.effect==Effect::Technology) {
        *technologyFlag(h,o.technology)=true; h->RecheckTechTree=true; return true;
    }
    auto from=TechnoTypeClass::Find(o.from.c_str()),to=TechnoTypeClass::Find(o.to.c_str());
    vector<InfantryClass*> targets;
    for(auto infantry:InfantryClass::Array) if(matches(infantry,h,from)) targets.push_back(infantry);
    int changed=0;
    for(auto infantry:targets) if(matches(infantry,h,from) && converter()(infantry,to)) ++changed;
    return changed>0;
}bool Research::meetsPrerequisites(int house,int option) {
    const auto& project=options[option];
    return project.validChain && std::all_of(project.prerequisites.begin(),project.prerequisites.end(),
        [house](const auto& id){return owns(house,id);});
}
// Opening the local UI must not consume the simulation RNG. Use the shared match
// seed plus the owner's index, and persist the chosen IDs rather than registry positions.
namespace {
struct OfferCache { string country; vector<int> rows; };
std::map<int,OfferCache> offerCache;
}
void Research::clearOfferCache() { offerCache.clear(); }
const vector<int>& Research::offers(int house,const char* country) {
    ensureReady();
    const string countryId=lower(country?country:"");
    auto existing=offerCache.find(house);
    if(existing!=offerCache.end() && existing->second.country==countryId) return existing->second.rows;
    auto& result=offerCache[house];
    result.country=countryId;
    result.rows.clear();
    vector<int> pool;
    for(int i=0;i<static_cast<int>(options.size());++i) {
        const auto& allowed=options[i].countries;
        if(!owns(house,options[i].id) && available(i) && meetsPrerequisites(house,i) && (allowed.empty() || includes(allowed,"*") || includes(allowed,countryId))) pool.push_back(i);
    }
    auto remember=[&](int option) {
        if(result.rows.size()<choices && std::find(pool.begin(),pool.end(),option)!=pool.end()
            && std::find(result.rows.begin(),result.rows.end(),option)==result.rows.end()) result.rows.push_back(option);
    };
    auto& ids=savedOffers[house];
    for(const auto& id:ids)
        for(int i:pool) if(options[i].id==id) { remember(i); break; }
    if(pool.size()>choices) {
        std::mt19937 rng(static_cast<unsigned>(Game::Seed) ^ (0x9E3779B9u*(house+1u)));
        std::shuffle(pool.begin(),pool.end(),rng);
    }
    for(int i:pool) { if(result.rows.size()==choices) break; remember(i); }
    ids.clear();
    for(int i:result.rows) ids.push_back(options[i].id);
    return result.rows;
}
std::wstring Research::describe(int option,wchar_t (&conditions)[96]) {
    const auto& o=options[option];
    if(o.effect!=Effect::Passive) {
        swprintf_s(conditions,L"即时生效  ·  每位玩家限购一次");
        auto detail=o.detail;
        if(detail.empty()) detail=o.effect==Effect::Credits?L"立即获得 {credits} 资金":
            o.effect==Effect::Technology?L"解锁本方指定类别的渗透科技":L"转换当前全部符合条件的己方步兵";
        const auto value=std::to_wstring(o.credits);
        for(size_t at=0;(at=detail.find(L"{credits}",at))!=std::wstring::npos;at+=value.size())
            detail.replace(at,9,value);
        return detail;
    }
    const auto& s=skills.at(o.passive);
    swprintf_s(conditions,L"每 %d 次判定  ·  概率 %d%%  ·  冷却 %d 帧",s.count,s.chance,s.cooldown);
    auto text=o.detail;
    if(text.empty()) {
        text=s.action=="heal"?L"恢复生命":s.action=="damage"?L"造成伤害":L"获得资金";
        text+=s.amount?L"：{amount}":L"：实际伤害的 {percent}%";
    }
    auto replace=[&](const std::wstring& key,int value) {
        const auto number=std::to_wstring(value); size_t pos=0;
        while((pos=text.find(key,pos))!=std::wstring::npos) {
            text.replace(pos,key.size(),number); pos+=number.size();
        }
    };
    replace(L"{amount}",s.amount); replace(L"{percent}",s.damagePercent);
    replace(L"{count}",s.count); replace(L"{chance}",s.chance); replace(L"{cooldown}",s.cooldown);
    return text;
}DEFINE_HOOK(0x683549, SF_ScenarioStart, 9) { reset(); return 0; }
DEFINE_HOOK(0x6F4500, SF_UnitDestroyed, 5) {
    auto id=R->ECX<TechnoClass*>()->UniqueID;
    counts.erase(id); Research::producers.erase(id);
    if(activeObjects) activeObjects->erase(id);
    return 0;
}
DEFINE_HOOK(0x702E4E, SF_Kill, 6) {
    auto killer=R->EDI<TechnoClass*>(), victim=R->ECX<TechnoClass*>();
    if(victim) capture("onkill",killer,victim,killer,victim,0);
    return 0;
}
// These two points straddle ObjectClass::ReceiveDamage. Armor and overkill
// are accounted for by measuring the actual reduction in hit points.
namespace { std::map<DWORD,int> healthBefore; }
DEFINE_HOOK(0x701DCC, SF_BeforeDamage, 7) {
    healthBefore[R->ESP()]=R->ESI<TechnoClass*>()->Health;
    return 0;
}
DEFINE_HOOK(0x701DFF, SF_AfterDamage, 7) {
    auto i=healthBefore.find(R->ESP());
    if(i==healthBefore.end()) return 0;
    auto victim=R->ESI<TechnoClass*>(), attacker=R->Stack<TechnoClass*>(0xD4);
    int damage=i->second-std::max(victim->Health,0);
    healthBefore.erase(i);
    if(damage>0) {
        capture("ondealdamage",attacker,victim,attacker,victim,damage);
        capture("ontakedamage",victim,attacker,attacker,victim,damage);
    }
    return 0;
}
DEFINE_HOOK(0x55B4E1, SF_Frame, 5) { tickBuildings(); dispatch(); return 0; }
// 9600,0,0,amount,recipient,0,0,A. Recipient 0 means the trigger house.
DEFINE_HOOK(0x6DD8B0, SF_AwardResearchPoints, 6) {
    auto action=R->ECX<TActionClass*>();
    if(static_cast<int>(action->ActionKind)!=9600) return 0;
    int target=action->Param4;
    auto house=target==0 ? R->Stack<HouseClass*>(4) :
        (HouseClass::Index_IsMP(target)?HouseClass::FindByPlayerAt(target):nullptr);
    if(eligibleHouse(house)) Research::award(house->ArrayIndex,action->Param3);
    R->AL(1); return 0x6DD910;
}


// Private stream in the save container, independent of the game compression stream.
// No process address or unordered-container iteration order is persisted.
namespace {
constexpr wchar_t stateStreamName[]=L"Starfall.Passives";
constexpr DWORD saveTag=0x31504653; // SFP1
struct Writer {
    vector<BYTE> bytes;
    void word(DWORD n) {
        const BYTE* p=reinterpret_cast<const BYTE*>(&n);
        bytes.insert(bytes.end(),p,p+sizeof(n));
    }
    void str(const string& s) {
        word(static_cast<DWORD>(s.size()));
        bytes.insert(bytes.end(),s.begin(),s.end());
    }
};
struct Reader {
    const vector<BYTE>& bytes;
    size_t pos=0;
    bool ok=true;
    DWORD word() {
        if(bytes.size()-pos<4) { ok=false; return 0; }
        DWORD n; memcpy(&n,bytes.data()+pos,4); pos+=4; return n;
    }
    string str() {
        DWORD n=word();
        if(n>127 || n>bytes.size()-pos) { ok=false; return {}; }
        string s(reinterpret_cast<const char*>(bytes.data()+pos),n);
        pos+=n; return s;
    }
};
bool saveState(IStream* stream) {
    if(!stream) return false;
    Writer w;
    w.word(static_cast<DWORD>(counts.size()));
    for(const auto& [id, entries]:counts) {
        w.word(id); w.word(static_cast<DWORD>(entries.size()));
        for(const auto& [skill,p]:entries) { w.str(skill); w.word(p.count); w.word(p.next); }
    }
    w.word(static_cast<DWORD>(pending.size()));
    for(const Event& e:pending) {
        w.str(e.trigger); w.str(e.kind); w.str(e.relation);
        w.word(e.holder); w.word(e.attacker); w.word(e.victim);
        w.word(e.damage); w.word(e.hp); w.word(e.maxHp);
    }
    w.word(static_cast<DWORD>(Research::unlocked.size()));
    for(const auto& [house,ids]:Research::unlocked) {
        w.word(house); w.word(static_cast<DWORD>(ids.size()));
        for(const auto& id:ids) w.str(id);
    }
    w.word(static_cast<DWORD>(Research::savedOffers.size()));
    for(const auto& [house,ids]:Research::savedOffers) {
        w.word(house); w.word(static_cast<DWORD>(ids.size()));
        for(const auto& id:ids) w.str(id);
    }
    w.word(static_cast<DWORD>(Research::points.size()));
    for(auto [house,points]:Research::points) { w.word(house); w.word(points); }
    w.word(static_cast<DWORD>(Research::producers.size()));
    for(auto [id,p]:Research::producers) { w.word(id); w.word(p.owner); w.word(p.elapsed); }
    DWORD header[]={0x34504653,static_cast<DWORD>(w.bytes.size())};
    ULONG written=0;
    return SUCCEEDED(stream->Write(header,sizeof(header),&written)) && written==sizeof(header)
        && SUCCEEDED(stream->Write(w.bytes.data(),header[1],&written)) && written==header[1];
}
void loadState(IStream* stream) {
    reset();
    if(!stream) return;
    DWORD header[2]{};
    ULONG got=0;
    if(FAILED(stream->Read(header,sizeof(header),&got)) || got!=sizeof(header) || (header[0]!=saveTag && header[0]!=0x32504653 && header[0]!=0x33504653 && header[0]!=0x34504653)) return;
    if(header[1]>64*1024*1024) return;
    vector<BYTE> data(header[1]);
    if(FAILED(stream->Read(data.data(),header[1],&got)) || got!=header[1]) return;
    Reader r{data};
    Counts restored;
    vector<Event> events;
    for(DWORD n=r.word(); n && r.ok; --n) {
        DWORD id=r.word();
        for(DWORD m=r.word(); m && r.ok; --m) {
            string skill=r.str(); int count=static_cast<int>(r.word()); DWORD next=r.word();
            if(count<0 || count==INT_MAX || skill.empty()) r.ok=false;
            else restored[id][skill]={count,next};
        }
    }
    for(DWORD n=r.word(); n && r.ok; --n) {
        Event e{r.str(),r.str(),r.str(),r.word(),r.word(),r.word(),
            static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
        events.push_back(std::move(e));
    }
    Research::Unlocks upgrades;
    if(r.ok && r.pos<data.size()) {
        DWORD n=r.word();
        if(n>256) r.ok=false;
        while(r.ok && n--) {
            int house=static_cast<int>(r.word());
            if(house<0 || house>127 || upgrades.contains(house)) { r.ok=false; break; }
            DWORD size=header[0]==0x34504653?r.word():1;
            auto& ids=upgrades[house];
            while(r.ok && size--) {
            string id;
            if(header[0]==saveTag) {
                DWORD index=r.word(); if(index<Research::choices) id=Research::legacyIds[index];
            } else id=r.str();
            if(id.empty() || !ids.insert(id).second) r.ok=false;
            }
        }
    }
    std::map<int,vector<string>> offers;
    if(header[0]>=0x33504653) {
        DWORD houses=r.word();
        if(houses>128) r.ok=false;
        while(r.ok && houses--) {
            DWORD house=r.word(), size=r.word();
            if(house>127 || size>Research::choices || offers.contains(house)) { r.ok=false; break; }
            auto& ids=offers[house];
            while(r.ok && size--) {
                auto id=r.str(); if(id.empty() || includes(ids,id)) r.ok=false;
                else ids.push_back(std::move(id));
            }
        }
    }
    decltype(Research::points) points;
    decltype(Research::producers) producers;
    if(header[0]==0x34504653) {
        for(DWORD n=r.word();r.ok && n--;){
            int house=r.word(), amount=r.word();
            if(house<0 || house>127 || amount<0 || !points.emplace(house,amount).second) r.ok=false;
        }
        for(DWORD n=r.word();r.ok && n--;){
            DWORD id=r.word(); int owner=r.word(), elapsed=r.word();
            if(owner<0 || owner>127 || elapsed<0 || elapsed==INT_MAX || !producers.emplace(id,Research::Producer{owner,elapsed}).second) r.ok=false;
        }
    }
    if(r.ok && r.pos==data.size()) { Research::points.swap(points); Research::producers.swap(producers); Research::savedOffers.swap(offers); counts.swap(restored); pending.swap(events); Research::unlocked.swap(upgrades); }
}
}
// gamemd 1.001: EAX is the open IStorage immediately before CreateStream("CONTENTS").
DEFINE_HOOK(0x67D074, SF_SaveStorage, 6) {
    auto storage=R->EAX<IStorage*>();
    IStream* stream=nullptr;
    if(FAILED(storage->CreateStream(stateStreamName,STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,0,&stream)))
        return 0x67D08F;
    const bool ok=saveState(stream) && SUCCEEDED(stream->Commit(STGC_DEFAULT));
    stream->Release();
    return ok ? 0 : 0x67D08F;
}

// Object load and scenario resets have finished; the source IStorage is still open.
DEFINE_HOOK(0x67E67B, SF_LoadStorage, 5) {
    auto storage=R->Stack<IStorage*>(0x18);
    IStream* stream=nullptr;
    if(storage && SUCCEEDED(storage->OpenStream(stateStreamName,nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&stream))) {
        loadState(stream);
        stream->Release();
    } else {
        reset();
    }
    return 0;
}
SYRINGE_HANDSHAKE(info) {
    if(!info || info->cbSize<sizeof(SyringeHandshakeInfo)) return E_INVALIDARG;
    if(info->exeCRC!=0x54CC0A13) return E_FAIL;
    if(info->Message && info->cchMessage>0)
        _snprintf_s(info->Message,info->cchMessage,_TRUNCATE,"Starfall 0.5 - research effects");
    return S_OK;
}

