#include "HealthFormula.h"
#include "UnitHealth.h"
#include "Research.h"
#include "VisualScale.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <functional>
#include <random>
#include <Unsorted.h>
#include <charconv>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <TechnoClass.h>
#include <AnimClass.h>
#include <unordered_map>
#include <HouseClass.h>
#include <FactoryClass.h>
#include <ScenarioClass.h>
#include <SessionClass.h>
#include <RulesClass.h>
#include <BuildingClass.h>
#include <BulletClass.h>
#include <AircraftTrackerClass.h>
#include <AircraftClass.h>
#include <SpawnManagerClass.h>
#include <BulletTypeClass.h>
#include <WeaponTypeClass.h>
#include <WarheadTypeClass.h>
#include <MapClass.h>
#include <Powerups.h>
#include <InfantryClass.h>
#include <UnitClass.h>
#include <TActionClass.h>
#include <TagClass.h>
#include <TriggerClass.h>
#include <TeamClass.h>
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
using Factors=std::array<double,3>; // speed, firepower, armor
constexpr Factors identity={1.0,1.0,1.0};
constexpr Factors defaultStatPercent={100,100,100};
constexpr std::array<std::string_view,7> percentActions={"changespeed","changefirepower","changearmor","changescale","changeprojectilescale","changeweaponrange","changefirerate"};
constexpr size_t scaleIndex=3, projectileScaleIndex=4, weaponRangeIndex=5, fireRateIndex=6;
std::map<DWORD,int> projectileBase;
std::map<DWORD,double> projectileState;
std::map<DWORD,double> rangeBase;
std::map<DWORD,double> rangeState;
std::map<DWORD,double> fireRateBase;
std::map<DWORD,double> fireRateState;

double unitFactor(DWORD id,const std::map<DWORD,double>& bases,const std::map<DWORD,double>& states) {
    const auto base=bases.find(id);
    const auto state=states.find(id);
    return (base==bases.end()?1.0:base->second/100.0)*(state==states.end()?1.0:state->second);
}

int adjustedRearmDelay(const TechnoClass* unit,int frames) {
    if(frames<=0 || (fireRateBase.empty() && fireRateState.empty())) return frames;
    const double rate=unitFactor(unit->UniqueID,fireRateBase,fireRateState);
    if(rate==1.0) return frames;
    return static_cast<int>(std::lround(std::clamp(frames/rate,1.0,static_cast<double>(INT_MAX))));
}

int adjustedWeaponRange(const TechnoClass* unit,int range) {
    if(range<=0 || (rangeBase.empty() && rangeState.empty())) return range;
    const double factor=unitFactor(unit->UniqueID,rangeBase,rangeState);
    if(factor==1.0) return range;
    // Guard scans double this result using signed int arithmetic.
    return static_cast<int>(std::lround(std::clamp(range*factor,1.0,static_cast<double>(INT_MAX/2))));
}

// The virtual getter supplies targeting and pursuit distances. Chain the
// installed getter so replacement weapons and other extensions resolve first.
using RangeGetter=int(__thiscall*)(const TechnoClass*,int);
template<class T> RangeGetter originalRangeGetter=nullptr;
template<class T> int __fastcall queryWeaponRange(const TechnoClass* unit,void*,int slot) {
    return adjustedWeaponRange(unit,originalRangeGetter<T>(unit,slot));
}
template<class T> void installRangeGetter() {
    if(originalRangeGetter<T>) return;
    auto slot=reinterpret_cast<RangeGetter*>(T::AbsVTable+0x168);
    DWORD protection;
    if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&protection)) {
        OutputDebugStringA("Starfall: could not install weapon range query\n");return;
    }
    originalRangeGetter<T> = *slot;
    *slot=reinterpret_cast<RangeGetter>(&queryWeaponRange<T>);
    VirtualProtect(slot,sizeof(*slot),protection,&protection);
}
struct PercentRange {
    int min=1,max=300;
    template<class T> T clamp(T value) const {
        return std::clamp<T>(value,min,max);
    }
};
std::array<PercentRange,percentActions.size()> percentLimits;
constexpr int unknownState=-1;
struct SlotRule {
    bool disabled=false;
    WeaponTypeClass* replacement=nullptr;
};
using WeaponRules=std::array<SlotRule,2>;
// CollectCrate rolls the game's weighted reward table for overlay data 19.
constexpr Powerup randomCrate=static_cast<Powerup>(19);
struct NearbyRule {
    int range=0,least=0,most=INT_MAX,interval=10;
    vector<string> houses,kinds;
};
struct AuraRule {
    int range=0;
    bool includeSelf=false;
    vector<string> passives,houses,types;
};
struct ConfigFormula {
    HealthFormula expression;
    string label;
    mutable bool reported=false;

    bool compile(const string& section,const string& field,const string& value) {
        label="["+section+"] "+field+"="+value;
        return expression.compile(value);
    }
    void report() const {
        if(reported) return;
        reported=true;
        std::ofstream("Starfall.log",std::ios::app)<<"Starfall: skipped invalid formula result: "<<label<<"\n";
    }
};
struct PassiveAction {
    ConfigFormula formula;
    ConfigFormula amountFormula;
    ConfigFormula signalFormula;
    string signal,signalTarget;
    vector<string> signalHouses;
    int signalRange=0;
    double signalValue=0;
    bool signalIncludeSelf=false;
    int percentDelta=0;
    size_t percentIndex=percentActions.size();
    Powerup crateType=randomCrate;
    TechnoTypeClass* unitType=nullptr;
    string owner;
    string action, target, passive, weapon, warhead;
    vector<string> passiveList;
    bool targetIsEventTarget=false,stackOnReapply=false;
    int amount=0, damagePercent=0, maxHealthPercent=0, duration=-1;
};
enum class DeployPosture { None, Undeployed, Deployed };

constexpr std::array<std::string_view,6> stateMultiplierKeys={"modifier.speedmultiplier","modifier.firepowermultiplier",
    "modifier.armormultiplier","modifier.weaponrangemultiplier","modifier.fireratemultiplier",
    "modifier.powermultiplier"};
constexpr size_t powerMultiplierIndex=5;
bool passengerCombatMultiplier(size_t index) {
    return index==1 || index==3 || index==4; // Firepower, weapon range, fire rate.
}
bool validStateMultiplier(size_t index,double value) {
    return value<=100 && (value>0 || (index==powerMultiplierIndex && value==0));
}
struct Skill {
    DeployPosture deployPosture=DeployPosture::None;
    int visualScale=100,projectileScale=100;
    std::array<double,stateMultiplierKeys.size()> multipliers={1,1,1,1,1,1};
    ConfigFormula conditionFormula;
    bool passengerCombat=false;
    vector<std::pair<size_t,ConfigFormula>> stateFormulas;
    NearbyRule nearby;
    AuraRule aura;
    AnimTypeClass* anim=nullptr;
    AnimTypeClass* triggerAnim=nullptr;
    bool animAlways=false;
    string animTarget;
    vector<string> spawnPassives;
    vector<string> transportPassives;
    bool transportStack=false;
    bool berserk=false, mirage=false, immune=false, mindImmune=false;
    string id, trigger;
    string signal;
    vector<string> senderHouses,senderKinds;
    vector<PassiveAction> actions;
    vector<string> houses, kinds;
    int count, chance, cooldown, minDamage, maxTriggers=0, stackMax=INT_MAX;
    int requiredMask=0,requiredBits=0;
    unsigned stateDelayFrames=0;
    int interval=0;
    WeaponRules slots{};
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
void configLog(const string& message) {
    const string line="Starfall: "+message+"\n";
    OutputDebugStringA(line.c_str());
    std::ofstream("Starfall.log",std::ios::app)<<line;
}
using ResearchWarning=std::function<void(const string&,const char*,const string&)>;
using EffectWarning=std::function<void(const char*,const string&)>;
bool validResearchEffect(const Research::EffectSpec&,const EffectWarning& warn={});
Research::EffectSpec singleEffect(const Research::Option&);

// A reference to an unknown ID leaves its project blocked. A topological walk
// also detects loops and projects that depend on a broken chain without recursion.
void validateResearchChains(const ResearchWarning& warn) {
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
            else warn(option.id,"Prerequisites","未注册前置项目 ["+id+"]，本项目无法抽取");
        }
    }
    for(size_t head=0;head<roots.size();++head) {
        auto i=roots[head]; options[i].validChain=true;
        for(auto next:dependents[i]) if(--remaining[next]==0) roots.push_back(next);
    }
    for(const auto& o:options) if(!o.validChain) {
        // Missing references were reported above. Identify the blocked direct parents, too.
        for(const auto& id:o.prerequisites) {
            auto parent=index.find(id);
            if(parent!=index.end() && !options[parent->second].validChain)
                warn(o.id,"Prerequisites","前置链在 ["+id+"] 处未通过（循环或依赖无效前置），本项目无法抽取");
        }
    }
}
Research::CostChange readCostChange(string value,const string& category) {
    using C=Research::CostCategory;
    Research::CostChange result;
    result.category=category=="building"?C::Building:category=="defense"?C::Defense:
        category=="infantry"?C::Infantry:category=="vehicle"?C::Vehicle:C::Invalid;
    result.percent=value.ends_with('%');
    if(result.percent) value.pop_back();
    const bool negative=value.starts_with('-');
    if(negative || value.starts_with('+')) value.erase(0,1);
    const int magnitude=number(value);
    if(magnitude>0) result.delta=negative?-magnitude:magnitude;
    return result;
}
void installProductionCheck();
void readResearch() {
    using Research::Option;
    const Option defaults[]={
        {"sf_repair","sf_researchrepair",{},{"mtnk"},L"战场维修",L"击杀时修复自身",L"击杀敌人时，额外恢复 {amount} 点生命"},
        {"sf_drain","sf_lifesteal",{},{"mtnk"},L"伤害吸血",L"造成伤害时吸取生命",L"按实际伤害的 {percent}% 恢复生命"},
        {"sf_riposte","sf_counter",{},{"mtnk"},L"受击反击",L"受击时反击攻击者",L"对攻击者造成 {amount} 点基础伤害"}
    };
    // Preserve the original bytes/case of UI text; Windows profile API can mix code pages.
    std::ifstream file(configPath,std::ios::binary);
    std::map<string,std::map<string,string>> fields;
    std::map<std::pair<string,string>,unsigned> lineNumbers;
    const string path=configPath;
    const string filename=path.substr(path.find_last_of("/\\")+1);
    unsigned lineNumber=0;
    string line,section;
    while(std::getline(file,line)) {
        ++lineNumber;
        if(line.starts_with("\xEF\xBB\xBF")) line.erase(0,3);
        line=trim(line.substr(0,line.find(';')));
        if(line.empty() || line.front()=='#' || line.starts_with("//")) continue;
        auto syntax=[&](const char* reason) {
            configLog(filename+":"+std::to_string(lineNumber)+" ["+section+"] "+line+" - "+reason);
        };
        if(line.front()=='[') {
            if(line.back()!=']' || trim(line.substr(1,line.size()-2)).empty()) {
                syntax("段标题应写成 [段名]");continue;
            }
            section=lower(trim(line.substr(1,line.size()-2)));fields.try_emplace(section);continue;
        }
        auto eq=line.find('=');
        if(eq==string::npos) {syntax("缺少等号，应写成 字段=值");continue;}
        auto key=lower(trim(line.substr(0,eq)));
        if(key.empty() || section.empty()) {syntax("字段名或所属段名为空");continue;}
        fields[section][key]=trim(line.substr(eq+1));
        lineNumbers[{section,key}]=lineNumber;
    }
    // The engine's INI index uses the original spelling of section names.
    // Project and effect references are normalized by Starfall.
    std::map<string,const char*> ruleSections;
    if(auto ini=CCINIClass::INI_Rules)
        for(auto node=ini->Sections.First();node && node->IsValid();node=node->Next())
            if(node->Name) ruleSections.try_emplace(lower(node->Name),node->Name);
    auto rulesSection=[&](const string& sec) {
        auto it=ruleSections.find(sec);
        return it==ruleSections.end()?sec.c_str():it->second;
    };
    auto field=[&](const string& sec,const char* key,const string& fallback=string{}) {
        auto it=fields.find(sec);
        if(it!=fields.end()) {
            auto item=it->second.find(lower(key));
            if(item!=it->second.end()) return item->second;
        }
        char buf[2048]{};
        if(CCINIClass::INI_Rules) CCINIClass::INI_Rules->ReadString(rulesSection(sec),key,"",buf,sizeof(buf));
        return buf[0]?trim(buf):fallback;
    };
    auto hasSection=[&](const string& sec) {
        return fields.contains(sec) || (CCINIClass::INI_Rules && CCINIClass::INI_Rules->GetSection(rulesSection(sec)));
    };
    ResearchWarning warn=[&](const string& sec,const char* key,const string& reason) {
        auto where=lineNumbers.find({sec,lower(key)});
        auto value=field(sec,key);
        string location;
        if(where!=lineNumbers.end()) location=filename+":"+std::to_string(where->second)+" ";
        else if(!value.empty()) location="合并规则 ";
        configLog(location+"["+sec+"] "+key+"="+value+" - "+reason);
    };
    std::set<string> checkedKeys;
    auto warnUnknown=[&](const string& sec,const std::set<string>& allowed) {
        if(!checkedKeys.insert(sec).second) return;
        std::map<string,string> unknown;
        auto check=[&](const string& key) {
            auto normalized=lower(key);
            if(!allowed.contains(normalized)) unknown.try_emplace(normalized,key);
        };
        if(auto ini=CCINIClass::INI_Rules;ini && ini->GetSection(rulesSection(sec)))
            for(int i=0;i<ini->GetKeyCount(rulesSection(sec));++i) check(ini->GetKeyName(rulesSection(sec),i));
        if(auto it=fields.find(sec);it!=fields.end()) for(const auto& entry:it->second) check(entry.first);
        for(const auto& entry:unknown) warn(sec,entry.second.c_str(),"科研配置不读取此字段，请核对拼写");
    };
    warnUnknown("starfall.research",{"enabled","allowcardrefresh","title","initialpoints","periodicpoints","periodicintervalseconds",
        "rarityweight.common","rarityweight.rare","rarityweight.epic"});
    warnUnknown("starfall.researchsounds",{"button","cards","refresh","select","available"});
    auto enabled=lower(field("starfall.research","Enabled","yes"));
    Research::enabled=enabled!="no" && enabled!="false" && enabled!="0";
    if(!Research::enabled) {
        Research::expanded=false;
        Research::rewardPanel=0;
        Research::pressed=Research::pressedOption=-1;
    }
    Research::options.clear();
    constexpr std::array<const char*,5> soundKeys={"Button","Cards","Refresh","Select","Available"};
    for(size_t i=0;i<soundKeys.size();++i) Research::soundFiles[i]=field("starfall.researchsounds",soundKeys[i]);
    auto refresh=lower(trim(field("starfall.research","AllowCardRefresh","yes")));
    Research::allowCardRefresh=refresh=="yes" || refresh=="true" || refresh=="1";
    for(const auto key:{"Enabled","AllowCardRefresh"}) {
        auto value=lower(field("starfall.research",key,"yes"));
        if(!includes({"yes","true","1","no","false","0"},value))
            warn("starfall.research",key,string("无效开关值，当前按 ")+(string(key)=="Enabled"?"yes":"no")+" 处理");
    }
    string heading=field("starfall.research","Title");
    Research::title=heading.empty()?L"请选择升级":displayText(heading);
    auto& income=Research::globalIncome;
    income.initialPoints=number(field("starfall.research","InitialPoints","0"));
    income.periodicPoints=number(field("starfall.research","PeriodicPoints","0"));
    int seconds=number(field("starfall.research","PeriodicIntervalSeconds","0"));
    if(income.initialPoints<0) warn("starfall.research","InitialPoints","应为非负整数，按 0 处理");
    if(income.periodicPoints<0) warn("starfall.research","PeriodicPoints","应为非负整数，按 0 处理");
    if(seconds<0 || seconds>INT_MAX/15) warn("starfall.research","PeriodicIntervalSeconds","无效的周期秒数，停止周期发放");
    if(income.initialPoints<0) income.initialPoints=0;
    if(income.periodicPoints<0) income.periodicPoints=0;
    income.intervalFrames=seconds>=0 && seconds<=INT_MAX/15?static_cast<unsigned>(seconds)*15u:0;
    constexpr std::array<const char*,3> weightKeys={"RarityWeight.Common","RarityWeight.Rare","RarityWeight.Epic"};
    constexpr std::array<int,3> defaultWeights={5,4,1};
    for(size_t i=0;i<weightKeys.size();++i) {
        int weight=number(field("starfall.research",weightKeys[i],std::to_string(defaultWeights[i])));
        if(weight<0) {
            warn("starfall.research",weightKeys[i],"无效权重，使用默认值 "+std::to_string(defaultWeights[i]));
            weight=defaultWeights[i];
        }
        Research::rarityWeights[i]=weight;
    }
    const string list="starfall.researchtypes";
    std::map<int,string> entries;
    auto addEntry=[&](const string& key,const string& value) {
        int index=number(trim(key));
        if(index<0) {warn(list,key.c_str(),"注册编号应为非负整数，此条目已忽略");return;}
        entries[index]=lower(trim(value));
    };
    if(auto* ini=CCINIClass::INI_Rules; ini && ini->GetSection(rulesSection(list))) {
        for(int i=0;i<ini->GetKeyCount(rulesSection(list));++i) {
            auto key=ini->GetKeyName(rulesSection(list),i);
            char value[256]{};
            ini->ReadString(rulesSection(list),key,"",value,sizeof(value));
            addEntry(key,value);
        }
    }
    vector<string> ids;
    if(auto it=fields.find(list);it!=fields.end())
        for(const auto& [key,value]:it->second) addEntry(key,value);
    std::set<string> seen;
    for(const auto& [key,id]:entries) {
        if(id.empty() || id.size()>127) warn(list,std::to_string(key).c_str(),"项目 ID 为空或超过 127 字节，条目已忽略");
        else if(seen.insert(id).second) ids.push_back(id);
    }
    for(const auto& id:ids) {
        if(Research::options.size()>SHRT_MAX) break;
        if(!hasSection(id)) configLog("["+list+"] "+id+" - 找不到同名科研项目段");
        Option o{}; o.id=id; o.name=displayText(id);
        for(const auto& d:defaults) if(id==d.id) o=d;
        using E=Research::Effect;
        auto parseEffect=[](const string& effect) {
            return effect=="grantpassive"?E::Passive:effect=="givecredits"?E::Credits:
                (effect=="unlocktech" || effect=="unlockteck")?E::Technology:effect=="convertinfantry"?E::ConvertInfantry:
                effect=="convertvehicle"?E::ConvertVehicle:effect=="modifytypecost"?E::TypeCost:
                effect=="modifycategorycost"?E::CategoryCost:E::Invalid;
        };
        auto costFields=[&](auto& effect,const string& section) {
            if(effect.effect==E::TypeCost || effect.effect==E::CategoryCost)
                effect.cost=readCostChange(field(section,"CostModifier"),lower(field(section,"Category")));
        };
        o.effect=parseEffect(lower(field(id,"Effect","GrantPassive")));
        costFields(o,id);
        o.credits=number(field(id,"Credits","0"));
        o.technology=lower(field(id,"Technology"));
        o.from=lower(field(id,"FromType")); o.to=lower(field(id,"ToType"));
        o.passive=lower(field(id,"Passive",o.passive));
        o.icon=field(id,"Icon");
        o.countries=split(field(id,"Countries"));
        o.prerequisites=split(field(id,"Prerequisites"));
        auto rarity=lower(field(id,"Rarity","Common"));
        o.rarity=rarity=="common"?Research::Rarity::Common:rarity=="rare"?Research::Rarity::Rare:
            rarity=="epic"?Research::Rarity::Epic:Research::Rarity::Invalid;
        if(o.rarity==Research::Rarity::Invalid) warn(id,"Rarity","应为 Common、Rare 或 Epic，项目无法抽取");
        o.types=split(field(id,"TargetTypes",o.types.empty()?"":"MTNK"));
        auto listValue=field(id,"Effects");
        bool hasList=fields.contains(id) && fields[id].contains("effects");
        if(auto* ini=CCINIClass::INI_Rules;ini && ini->GetSection(rulesSection(id))) {
            for(int i=0;i<ini->GetKeyCount(rulesSection(id));++i)
                if(!_stricmp(ini->GetKeyName(rulesSection(id),i),"Effects")) {hasList=true;break;}
        }
        static const std::set<string> projectKeys={"name","summary","detail","icon","countries","prerequisites","rarity","effects","effect",
            "costmodifier","category","credits","technology","fromtype","totype","passive","targettypes"};
        if(hasList && !skills.contains(id) && !TechnoTypeClass::Find(id.c_str())) warnUnknown(id,projectKeys);
        auto auditEffect=[&](const string& sec,const Research::EffectSpec& spec) {
            if(!skills.contains(sec) && !TechnoTypeClass::Find(sec.c_str())) warnUnknown(sec,projectKeys);
            if(spec.effect==E::Passive) for(const auto& type:spec.types)
                if(type!="*" && !type.starts_with('@') && !TechnoTypeClass::Find(type.c_str()))
                    warn(sec,"TargetTypes","未注册的单位类型 ["+type+"] 无法获得此被动");
            validResearchEffect(spec,[&](const char* key,const string& reason) {
                warn(sec,key,reason+"；科研项目 ["+id+"] 无法抽取");
            });
        };
        if(hasList) {
            o.effectList=true;
            if(split(listValue).empty()) warn(id,"Effects","效果列表为空，项目无法抽取");
            for(const auto& sectionId:split(listValue)) {
                Research::EffectSpec spec;
                spec.effect=parseEffect(lower(field(sectionId,"Effect")));
                costFields(spec,sectionId);
                spec.passive=lower(field(sectionId,"Passive"));spec.types=split(field(sectionId,"TargetTypes"));
                spec.credits=number(field(sectionId,"Credits","0"));
                spec.technology=lower(field(sectionId,"Technology"));
                spec.from=lower(field(sectionId,"FromType"));spec.to=lower(field(sectionId,"ToType"));
                if(!hasSection(sectionId)) warn(id,"Effects","找不到效果段 ["+sectionId+"]，项目无法抽取");
                else auditEffect(sectionId,spec);
                o.effects.push_back(std::move(spec));
            }
        } else auditEffect(id,singleEffect(o));
        auto label=field(id,"Name"),brief=field(id,"Summary"),detail=field(id,"Detail");
        if(!label.empty()) o.name=displayText(label);
        if(!brief.empty()) o.summary=displayText(brief);
        if(!detail.empty()) o.detail=displayText(detail);
        Research::options.emplace_back(std::move(o));
    }
    validateResearchChains(warn);
    Research::clearOfferCache();
    for(const auto& project:Research::options) {
        const bool grantsTech=project.effectList ? std::any_of(project.effects.begin(),project.effects.end(),
            [](const auto& effect){return effect.effect==Research::Effect::Technology;}) : project.effect==Research::Effect::Technology;
        if(grantsTech) {installProductionCheck();break;}
    }
}
bool anyAuras=false;
constexpr unsigned auraInterval=15;
using AuraKey=std::pair<DWORD,string>;
struct AuraSource {
    TechnoClass* unit=nullptr;
    int owner=-1;
    vector<string> grants;
    std::set<DWORD> targets;
};
std::map<AuraKey,AuraSource> auraSources;
// Count sources independently; expose each passive ID to the unit only once.
std::map<DWORD,std::map<string,unsigned>> auraGrants;
std::map<DWORD,std::set<string>> auraLosses;
bool discoverAuras=true;
bool anySpawnPassives=false;
bool anyTransportPassives=false;
struct TransportGrant {
    unsigned sources=0, layers=0;
    bool operator==(const TransportGrant&) const = default;
};
std::map<DWORD,std::map<string,TransportGrant>> transportGrants;
std::set<std::pair<DWORD,string>> transportStateKeys;
struct SpawnGrant {
    DWORD parent=0;
    vector<string> ids;
    bool operator==(const SpawnGrant&) const = default;
};
std::map<DWORD,SpawnGrant> spawnGrants;
bool anyGainSkills=false;
std::map<DWORD,vector<string>> gainSeen;
// Research unlocks are additive. Re-check bindings when their inputs change;
// timed grants and explicit removals update ownership directly.
struct GainInputs {
    TechnoTypeClass* type=nullptr;
    HouseClass* owner=nullptr;
    size_t unlocked=0;
    bool onMap=false;
    bool operator==(const GainInputs&) const = default;
};
std::unordered_map<DWORD,GainInputs> gainInputs;
bool anyIntervals=false;
bool anyBeforeDealDamage=false;
bool anyBeforeTakeDamage=false;
bool beforeHealthLoss(const string& trigger) {
    return trigger=="onbeforedealdamage" || trigger=="onbeforetakedamage";
}
bool damageEvent(const string& trigger) {
    return beforeHealthLoss(trigger) || trigger=="ondealdamage" || trigger=="ontakedamage";
}
struct IncomingDamage { TechnoClass* unit; int* amount; bool nonpositive; };
std::map<DWORD,IncomingDamage> incomingDamage;
bool anyStateSkills=false, observePromotions=false, observeDeployments=false;
bool needUnitPower=false,needMotion=false;
bool invalidField(const string& id,const string& field) {
    auto detail=field;
    if(!field.empty() && field.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.")==string::npos)
        detail+="="+read(id,field.c_str());
    configLog("disabled ["+id+"] - invalid field or combination: "+detail);
    return false;
}
// Parse decimal seconds as an integer ratio. The engine changes the floating-point
// control word, so floating-point rounding near a frame boundary is unsuitable.
bool parseStateDelay(const string& value,unsigned& frames) {
    const auto dot=value.find('.');
    auto whole=value.substr(0,dot);
    auto fraction=dot==string::npos ? string{} : value.substr(dot+1);
    if((whole.empty() && fraction.empty()) || fraction.size()>3 ||
       (dot!=string::npos && fraction.empty())) return false;
    auto seconds=whole.empty()?0:number(whole);
    int millis=fraction.empty()?0:number(fraction);
    if(seconds<0 || millis<0) return false;
    for(size_t n=fraction.size();n<3;++n) millis*=10;
    auto total=15LL*seconds+(15*millis+999)/1000;
    if(total>INT_MAX) return false;
    frames=static_cast<unsigned>(total);
    return true;
}
std::set<string> sectionKeys(const string& section) {
    std::set<string> keys;
    if(CCINIClass* ini=CCINIClass::INI_Rules)
        for(int i=0;i<ini->GetKeyCount(section.c_str());++i) keys.insert(lower(ini->GetKeyName(section.c_str(),i)));
    char names[32768]{};
    GetPrivateProfileStringA(section.c_str(),nullptr,"",names,sizeof(names),configPath);
    for(auto key=names;*key;key+=strlen(key)+1) keys.insert(lower(key));
    return keys;
}
int scalePercent(const string& text,double minimum) {
    double value=0;
    auto r=std::from_chars(text.data(),text.data()+text.size(),value);
    if(r.ec!=std::errc{} || r.ptr!=text.data()+text.size() || !std::isfinite(value) || value<minimum || value>2.0) return -1;
    return static_cast<int>(std::lround(value*100.0));
}
bool actionField(const string& key) {
    static const std::set<string> fields={"action","action.amount","action.maxhealthpercent","action.percentofdamage","action.target","action.weapon",
        "action.warhead","action.passive","action.duration","action.stackonreapply","action.cratetype","action.owner","action.unittype","action.percent",
        "action.signal","action.signal.target","action.signal.range","action.signal.houses","action.signal.includeself","action.signal.value"};
    return fields.contains(key);
}
bool parseState(Skill& s,const std::set<string>& keys) {
    const bool continuous=s.trigger=="whilestate";
    auto condition=[&](const char* key,int mask,int shift,const vector<string>& values) {
        auto value=read(s.id,key);
        if(value.empty()) return true;
        auto it=std::find(values.begin(),values.end(),value);
        if(it==values.end()) return invalidField(s.id,string(key)+"="+value);
        s.requiredMask|=mask;
        s.requiredBits|=static_cast<int>(it-values.begin())<<shift;
        return true;
    };
    if(!condition("Condition.OwnerPower",1,0,{"low","sufficient"}) ||
       !condition("Condition.SelfPowered",2,1,{"no","yes"}) ||
       !condition("Condition.SelfMoving",16,4,{"no","yes"}) ||
       !condition("Condition.SelfCloak",12,2,{"uncloaked","cloaking","cloaked","uncloaking"})) return false;
    bool modifier=false;
    for(const auto& key:keys) {
        if(key=="anim" || key=="anim.always" || key=="triggeranim" || key=="triggeranim.target" || key=="stack.max") continue;
        if(key=="condition.formula") {
            auto value=read(s.id,key.c_str());
            if(!s.conditionFormula.compile(s.id,key,value)) return invalidField(s.id,key+"="+value);
            if(s.conditionFormula.expression.usesDamage() &&
               !damageEvent(s.trigger)) return invalidField(s.id,key+" uses Event.Damage");
        } else if(includes({"condition.selfhealthatleast","condition.selfhealthatmost","condition.selfhealthbelow",
                            "condition.ammoatleast","condition.ammoatmost","condition.passengersatleast","condition.passengersatmost"},key)) {
            return invalidField(s.id,key+" removed; use Condition.Formula");
        } else if(key.starts_with("condition.nearby.")) {
            if(!continuous || !includes({"condition.nearby.range","condition.nearby.houses","condition.nearby.kinds","condition.nearby.countatleast","condition.nearby.countatmost","condition.nearby.scaninterval"},key))
                return invalidField(s.id,key);
            s.nearby.range=-1;
        } else if(key=="condition.selfdeployed") {
            const auto value=read(s.id,key.c_str());
            if(!continuous || (value!="yes" && value!="no")) return invalidField(s.id,key);
            s.deployPosture=value=="yes"?DeployPosture::Deployed:DeployPosture::Undeployed;
        } else if(key=="condition.statedurationseconds") {
            if(!continuous || !parseStateDelay(read(s.id,key.c_str()),s.stateDelayFrames)) return invalidField(s.id,key);
        } else if(key=="modifier.visualscale" || key=="modifier.projectilescale") {
            auto& scale=key=="modifier.visualscale"?s.visualScale:s.projectileScale;
            scale=scalePercent(read(s.id,key.c_str()),key=="modifier.visualscale"?0.01:0.5);
            if(!continuous || scale<0) return invalidField(s.id,key);
            modifier=true;
        } else if(key.starts_with("modifier.aura.")) {
            if(!continuous || !includes({"modifier.aura.range","modifier.aura.houses","modifier.aura.targettypes",
                "modifier.aura.passives","modifier.aura.includeself"},key)) return invalidField(s.id,key);
            modifier=true;
            s.aura.range=1; // Read the grouped fields together after checking their names.
        } else if(key=="modifier.spawnpassives") {
            s.spawnPassives=split(read(s.id,key.c_str()));
            if(!continuous || s.spawnPassives.empty()) return invalidField(s.id,key);
            modifier=true;
        } else if(key=="modifier.transportpassives") {
            s.transportPassives=split(read(s.id,key.c_str()));
            if(!continuous || s.transportPassives.empty()) return invalidField(s.id,key);
            modifier=true;
        } else if(key=="modifier.transportpassives.stack") {
            auto value=read(s.id,key.c_str());
            if(!continuous || (value!="yes" && value!="no")) return invalidField(s.id,key);
            s.transportStack=value=="yes";
        } else if(key=="modifier.berserk") {
            auto value=read(s.id,key.c_str());
            if(!continuous || (value!="yes" && value!="no")) return invalidField(s.id,key);
            s.berserk=value=="yes";
            modifier|=s.berserk;
        } else if(key=="modifier.mirage" || key=="modifier.immune" || key=="modifier.immunetomindcontrol") {
            auto value=read(s.id,key.c_str());
            if(!continuous || (value!="yes" && value!="no")) return invalidField(s.id,key);
            auto& enabled=key=="modifier.immune"?s.immune:key=="modifier.immunetomindcontrol"?s.mindImmune:s.mirage;
            enabled=value=="yes";
            modifier|=enabled;
        } else if(key=="modifier.disableprimary" || key=="modifier.disablesecondary" ||
                  key=="modifier.primaryweapon" || key=="modifier.secondaryweapon") {
            if(!continuous) return invalidField(s.id,key);
            int slot=key.find("primary")!=string::npos?0:1;
            auto value=read(s.id,key.c_str());
            if(key.starts_with("modifier.disable")) {
                if(value!="yes" && value!="no") return invalidField(s.id,key);
                s.slots[slot].disabled=value=="yes";
                modifier|=s.slots[slot].disabled;
            } else {
                s.slots[slot].replacement=WeaponTypeClass::Find(value.c_str());
                if(!s.slots[slot].replacement) return invalidField(s.id,key+"="+value);
                modifier=true;
            }
        } else if(key.starts_with("modifier.")) {
            if(key=="modifier.poweroutputmultiplier" || key=="modifier.powerdrainmultiplier")
                return invalidField(s.id,key+" removed; use Modifier.PowerMultiplier");
            auto it=std::find(stateMultiplierKeys.begin(),stateMultiplierKeys.end(),key);
            if(!continuous || it==stateMultiplierKeys.end()) return invalidField(s.id,key);
            const size_t index=it-stateMultiplierKeys.begin();
            s.passengerCombat|=passengerCombatMultiplier(index);
            const auto str=read(s.id,key.c_str());
            if(str.starts_with("$(")) {
                auto& field=s.stateFormulas.emplace_back(index,ConfigFormula{}).second;
                if(!field.compile(s.id,key,str) || field.expression.usesDamage()) return invalidField(s.id,key+"="+str);
                modifier=true;
                continue;
            }
            double value=1.0;
            auto parsed=std::from_chars(str.data(),str.data()+str.size(),value);
            if(parsed.ec!=std::errc{} || parsed.ptr!=str.data()+str.size() ||
               !validStateMultiplier(index,value)) return invalidField(s.id,key);
            s.multipliers[index]=value;
            modifier=true;
        } else if(continuous && key!="trigger" && key!="condition.ownerpower" &&
                  key!="condition.selfmoving" && key!="condition.selfpowered" && key!="condition.selfcloak")
            return invalidField(s.id,key);
        else if(!continuous) {
            static const std::set<string> eventKeys={"trigger","actions","interval","maxtriggers","chance","cooldown",
                "signal","filter.senderhouse","filter.senderkind","filter.targethouse","filter.targetkind","condition.mindamage","condition.count","condition.ownerpower",
                "condition.selfmoving","condition.selfpowered","condition.selfcloak"};
            if(!eventKeys.contains(key) && !actionField(key))
                configLog("["+s.id+"] "+key+"="+read(s.id,key.c_str())+" - 事件被动不读取此字段，请核对拼写");
        }
    }
    if(s.aura.range) {
        auto& aura=s.aura;
        auto range=read(s.id,"Modifier.Aura.Range");
        if(range=="-1") aura.range=-1;
        else if((aura.range=number(range))<1 || aura.range>32) return invalidField(s.id,"Modifier.Aura.Range="+range+" (1..32 or -1)");
        auto list=[&](const char* key,const char* fallback,vector<string>& out) {
            auto raw=read(s.id,key,fallback);
            size_t start=0;
            for(;;) {
                auto end=raw.find(',',start);
                if(trim(raw.substr(start,end-start)).empty()) return invalidField(s.id,string(key)+" (empty list item)");
                if(end==string::npos) break;
                start=end+1;
            }
            out=split(raw);return true;
        };
        if(!list("Modifier.Aura.Passives","",aura.passives) || !list("Modifier.Aura.TargetTypes","*",aura.types) ||
           !list("Modifier.Aura.Houses","Self",aura.houses)) return false;
        for(const auto& h:aura.houses) if(!includes({"all","self","allies","enemies","neutral"},h))
            return invalidField(s.id,"Modifier.Aura.Houses="+h);
        for(const auto& type:aura.types) if(type!="*" && !Research::isTargetCategory(type) && !TechnoTypeClass::Find(type.c_str()))
            return invalidField(s.id,"Modifier.Aura.TargetTypes="+type);
        auto self=read(s.id,"Modifier.Aura.IncludeSelf","no");
        if(self!="yes" && self!="no") return invalidField(s.id,"Modifier.Aura.IncludeSelf="+self);
        aura.includeSelf=self=="yes";
    }
    auto& query=s.nearby;
    if(query.range) {
        query.range=number(read(s.id,"Condition.Nearby.Range"));
        query.interval=number(read(s.id,"Condition.Nearby.ScanInterval","10"));
        auto min=read(s.id,"Condition.Nearby.CountAtLeast"),max=read(s.id,"Condition.Nearby.CountAtMost");
        query.least=min.empty()?(max.empty()?1:0):number(min);
        query.most=max.empty()?INT_MAX:number(max);
        query.houses=split(read(s.id,"Condition.Nearby.Houses","All"));
        query.kinds=split(read(s.id,"Condition.Nearby.Kinds","All"));
        if(query.range<1 || query.range>32 || query.interval<1 || query.least<0 || query.most<query.least)
            return invalidField(s.id,"Condition.Nearby range/count/interval");
        for(auto& h:query.houses) if(!includes({"all","self","allies","enemies","neutral"},h)) return invalidField(s.id,"Condition.Nearby.Houses");
        for(auto& k:query.kinds) if(!includes({"all","infantry","vehicle","aircraft","building"},k)) return invalidField(s.id,"Condition.Nearby.Kinds");
    }
    if(continuous && !modifier && !s.triggerAnim && !s.anim) return invalidField(s.id,"Modifier.*, Anim or TriggerAnim required");
    if(keys.contains("modifier.transportpassives.stack") && s.transportPassives.empty()) return invalidField(s.id,"Modifier.TransportPassives.Stack requires Modifier.TransportPassives");
    if(!s.transportPassives.empty() && (query.range || s.conditionFormula.expression.usesDistance() || s.conditionFormula.expression.usesTargetValues()))
        return invalidField(s.id,"transport support cannot use spatial or target conditions");
    if(continuous && !query.range && !s.requiredMask && s.deployPosture==DeployPosture::None && s.conditionFormula.expression.empty() && s.transportPassives.empty()) return invalidField(s.id,"state condition required");
    return true;
}
bool readAction(Skill& skill,const string& section) {
    PassiveAction a;
    auto invalid=[&](const string& field) {
        return invalidField(skill.id,section==skill.id ? field : "Actions -> ["+section+"] "+field);
    };
    a.action=read(section,"Action");
    if(a.action!="sendsignal") for(const auto& key:sectionKeys(section))
        if(key=="action.signal" || key.starts_with("action.signal.")) return invalid(key+" requires SendSignal");
    if(a.action=="grantpassive") {
        auto duration=read(section,"Action.Duration","-1");
        if(duration!="-1" && (a.duration=number(duration))<=0) return invalid("Action.Duration="+duration);
    } else if(!read(section,"Action.Duration").empty()) return invalid("Action.Duration");
    auto reapply=read(section,"Action.StackOnReapply");
    if(!reapply.empty()) {
        if(a.action!="grantpassive" || (reapply!="yes" && reapply!="no")) return invalid("Action.StackOnReapply="+reapply);
        a.stackOnReapply=reapply=="yes";
    }
    const bool outgoing=skill.trigger=="onbeforedealdamage";
    a.target=read(section,"Action.Target",outgoing || a.action=="stealpassive"?"Target":"Self");
    a.targetIsEventTarget=a.target=="target";
    if(a.target=="target") {
        if(skill.trigger=="ontakedamage" || skill.trigger=="onbeforetakedamage" || skill.trigger=="ondeath") a.target="attacker";
        else if(skill.trigger=="ondeploy" || skill.trigger=="onpromote" || skill.trigger=="onpromoteveteran" || skill.trigger=="onpromoteelite" || skill.trigger=="onpassivegained") a.target="self";
        else a.target="victim";
    }
    bool positional=a.action=="fireweapon" || a.action=="detonatewarhead" || a.action=="spawncrate" || a.action=="spawnunit";
    if(a.target!="self" && a.target!="attacker" && a.target!="victim" && !(a.target=="sender" && skill.trigger=="onsignal") && !(positional && a.target=="victimlocation"))
        return invalid("Action.Target="+a.target);
    if(a.action=="setdamage" || a.action=="blockdamage") {
        if(!beforeHealthLoss(skill.trigger)) return invalid(a.action+" requires OnBeforeDealDamage or OnBeforeTakeDamage");
        if(a.target!=(outgoing?"victim":"self")) return invalid(a.action+" requires "+(outgoing?"Target or Victim":"Self"));
    }
    bool setDamage=a.action=="setdamage";
    bool numeric=a.action=="heal" || a.action=="damage" || a.action=="givecredits" || a.action=="giveresearchpoints" || setDamage;
    auto amount=read(section,"Action.Amount",a.action=="stealpassive"?"1":"0");
    bool formulaAmount=amount.starts_with("$(");
    if(formulaAmount && (!numeric || !a.amountFormula.compile(section,"Action.Amount",amount)))
        return invalid("Action.Amount="+amount);
    if(formulaAmount) amount="0";
    auto healthPercent=read(section,"Action.MaxHealthPercent");
    if(!healthPercent.empty()) {
        a.maxHealthPercent=number(healthPercent,true);
        if(a.action!="changemaxhealth" || a.maxHealthPercent<=0 || a.maxHealthPercent>10000 || amount!="0")
            return invalid("Action.MaxHealthPercent="+healthPercent);
    }
    if(a.action=="changeammo" || a.action=="changemaxhealth" || a.action=="changepassivestack") {
        if(a.maxHealthPercent) amount="1"; // Reuse fixed-amount validation; percent takes precedence when applied.
        auto first=amount.data(),last=first+amount.size();
        if(first!=last && *first=='+') {
            ++first;
            if(first!=last && *first=='-') return invalid("Action.Amount="+amount);
        }
        auto r=std::from_chars(first,last,a.amount);
        if(r.ec!=std::errc{} || r.ptr!=last || a.amount==0 || (amount.front()=='+' && a.amount<0))
            return invalid("Action.Amount="+amount);
    } else {
        a.amount=number(amount);
        if(a.amount<0) return invalid("Action.Amount="+amount);
    }
    a.damagePercent=number(read(section,"Action.PercentOfDamage","0"),true);
    if(a.damagePercent<0 || (!numeric && a.damagePercent)) return invalid("Action.PercentOfDamage");
    if(numeric) {
        if(setDamage) {
            auto raw=read(section,"Action.Amount");
            if(raw.empty() || a.damagePercent)
                return invalid("SetDamage requires Action.Amount and does not use Action.PercentOfDamage");
            if(!formulaAmount) {
                auto first=raw.data(),last=first+raw.size();
                if(first!=last && *first=='+') ++first;
                int value=0;auto parsed=std::from_chars(first,last,value);
                if(first==last || parsed.ec!=std::errc{} || parsed.ptr!=last || value<0) return invalid("Action.Amount="+raw);
                a.amount=value;
            }
        } else if(static_cast<int>(a.amount>0)+static_cast<int>(a.damagePercent>0)+static_cast<int>(formulaAmount)!=1)
            return invalid("Action.Amount / Action.PercentOfDamage");
    } else if(a.action=="spawnunit" || a.action=="changeowner" || a.action=="transform") {
        if(a.action!="transform") {
            a.owner=read(section,"Action.Owner","Self");
            bool playerSlot=a.owner.size()==7 && a.owner.starts_with("player") && a.owner[6]>='1' && a.owner[6]<='8';
            if(!playerSlot && !includes({"self","attacker","victim","neutral"},a.owner))
                return invalid("Action.Owner="+a.owner);
        }
        if(a.action!="changeowner") {
            auto id=read(section,"Action.UnitType");
            a.unitType=id.empty()?nullptr:TechnoTypeClass::Find(id.c_str());
            if(!a.unitType || a.unitType->WhatAmI()==AbstractType::BuildingType) return invalid("Action.UnitType="+id);
        }
    } else if(auto stat=std::find(percentActions.begin(),percentActions.end(),a.action);stat!=percentActions.end()) {
        a.percentIndex=static_cast<size_t>(stat-percentActions.begin());
        auto text=read(section,"Action.Percent");
        if(text.starts_with("$(")) {
            if(a.percentIndex==scaleIndex || a.percentIndex==projectileScaleIndex ||
               !a.formula.compile(section,"Action.Percent",text)) return invalid("Action.Percent="+text);
        } else {
            if(!text.empty() && text.back()=='%') text.pop_back();
            auto first=text.data(),last=first+text.size();
            bool plus=first!=last && *first=='+';
            if(plus) ++first;
            auto parsed=std::from_chars(first,last,a.percentDelta);
            if(parsed.ec!=std::errc{} || parsed.ptr!=last || a.percentDelta==0 || (plus && a.percentDelta<0))
                return invalid("Action.Percent");
        }
    } else if(a.action=="spawncrate") {
        auto name=read(section,"Action.CrateType","Random");
        if(name!="random") {
            const auto& names=Powerups::Effects;
            auto found=std::find_if(std::begin(names),std::end(names),[&](const char* type){return !_stricmp(type,name.c_str());});
            if(found==std::end(names)) return invalid("Action.CrateType="+name);
            a.crateType=static_cast<Powerup>(found-std::begin(names));
        }
    } else if(a.action=="sendsignal") {
        a.signal=read(section,"Action.Signal");
        a.signalTarget=read(section,"Action.Signal.Target");
        a.signalHouses=split(read(section,"Action.Signal.Houses","Self"));
        auto range=read(section,"Action.Signal.Range","0");
        auto parsed=std::from_chars(range.data(),range.data()+range.size(),a.signalRange);
        auto include=read(section,"Action.Signal.IncludeSelf","no");
        auto value=read(section,"Action.Signal.Value","0");
        if(a.signal.empty() || a.signal.size()>127 || parsed.ec!=std::errc{} || parsed.ptr!=range.data()+range.size() || a.signalRange< -1 || a.signalRange>128 ||
           (include!="yes" && include!="no") || a.signalHouses.empty() ||
           (!a.signalTarget.empty() && !includes({"self","target","attacker","victim","sender"},a.signalTarget)) ||
           (a.signalTarget=="sender" && skill.trigger!="onsignal")) return invalid("Action.Signal configuration");
        for(const auto& house:a.signalHouses) if(!includes({"self","allies","enemies","neutral","all"},house)) return invalid("Action.Signal.Houses");
        a.signalIncludeSelf=include=="yes";
        if(value.starts_with("$(")) {
            if(!a.signalFormula.compile(section,"Action.Signal.Value",value)) return invalid("Action.Signal.Value="+value);
        } else {
            auto parsedValue=std::from_chars(value.data(),value.data()+value.size(),a.signalValue);
            if(parsedValue.ec!=std::errc{} || parsedValue.ptr!=value.data()+value.size() || !std::isfinite(a.signalValue)) return invalid("Action.Signal.Value="+value);
        }
        if(a.signalFormula.expression.usesDamage() && !damageEvent(skill.trigger)) return invalid("Event.Damage requires a damage trigger");
    } else if(a.action=="selfdestruct") {
        if(a.target!="self") return invalid("Action.Target="+a.target);
    } else if(a.action=="consumepassenger") {
        if(!read(section,"Action.Amount").empty()) return invalid("ConsumePassenger removes one passenger and does not use Action.Amount");
    } else if(a.action=="blockdamage") {
        if(!read(section,"Action.Amount").empty())
            return invalid("BlockDamage does not use Action.Amount");
    } else if(a.action=="removepassive" || a.action=="stealpassive") {
        auto list=read(section,"Action.Passive");
        a.passiveList=split(list);
        if(a.passiveList.empty() || list.size()>=2047) return invalid("Action.Passive="+list);
        if(a.action=="stealpassive" && a.amount<=0) return invalid("Action.Amount must be a positive stack count");
    } else if(a.action=="grantpassive" || a.action=="changepassivestack") {
        a.passive=read(section,"Action.Passive");
        if(a.passive.empty()) return invalid("Action.Passive");
    } else if(a.action=="fireweapon" || a.action=="replaceweapon") {
        a.weapon=read(section,"Action.Weapon");
        if(a.weapon.empty() || !WeaponTypeClass::Find(a.weapon.c_str())) return invalid("Action.Weapon="+a.weapon);
        if(a.action=="replaceweapon" && (skill.trigger!="onbeforefire" || a.target!="self")) return invalid("ReplaceWeapon requires OnBeforeFire and Self");
    } else if(a.action=="detonatewarhead") {
        a.warhead=read(section,"Action.Warhead");
        if(a.warhead.empty() || !WarheadTypeClass::Find(a.warhead.c_str())) return invalid("Action.Warhead="+a.warhead);
    } else if(a.action!="changeammo" && a.action!="changemaxhealth") return invalid("Action="+a.action);
    if((a.formula.expression.usesDamage() || a.amountFormula.expression.usesDamage()) &&
       !damageEvent(skill.trigger)) return invalid("Event.Damage requires a damage trigger");
    if((a.formula.expression.usesSignal() || a.amountFormula.expression.usesSignal() || a.signalFormula.expression.usesSignal()) &&
       skill.trigger!="onsignal") return invalid("Sender and Event.SignalValue require OnSignal");
    skill.actions.push_back(std::move(a));
    return true;
}
bool readActions(Skill& skill,const std::set<string>& keys) {
    if(!keys.contains("actions")) return readAction(skill,skill.id);
    for(const auto& key:keys)
        if(key=="action" || key.starts_with("action.")) return invalidField(skill.id,"Actions + "+key);
    const string list=read(skill.id,"Actions");
    if(list.size()>=2047) return invalidField(skill.id,"Actions (list too long)");
    size_t start=0;
    for(;;) {
        auto end=list.find(',',start);
        auto section=trim(list.substr(start,end-start));
        if(section.empty()) return invalidField(skill.id,"Actions (empty section name)");
        auto fields=sectionKeys(section);
        if(!fields.contains("action")) return invalidField(skill.id,"Actions -> ["+section+"] missing Action");
        for(const auto& field:fields) {
            if(!actionField(field))
                return invalidField(skill.id,"Actions -> ["+section+"] "+field);
        }
        if(!readAction(skill,section)) return false;
        if(end==string::npos) return true;
        start=end+1;
    }
}
bool anyAttachedAnims=false;
bool readAnimations(Skill& s,const std::set<string>& keys) {
    for(auto key:{"Anim","TriggerAnim"}) {
        if(!keys.contains(lower(key))) continue;
        auto id=read(s.id,key);
        auto type=id.empty()?nullptr:AnimTypeClass::Find(id.c_str());
        if(!type) return invalidField(s.id,string(key)+"="+id);
        if(key==std::string_view("Anim")) s.anim=type;
        else s.triggerAnim=type;
    }
    if(keys.contains("anim.always")) {
        auto value=read(s.id,"Anim.Always");
        if(!s.anim || (value!="yes" && value!="no")) return invalidField(s.id,"Anim.Always");
        s.animAlways=value=="yes";
    }
    s.animTarget=read(s.id,"TriggerAnim.Target","Self");
    if((keys.contains("triggeranim.target") && !s.triggerAnim) ||
       (s.animTarget!="self" && s.animTarget!="target")) return invalidField(s.id,"TriggerAnim.Target");
    return true;
}
void readPercentLimits();
struct PassiveGrant {
    vector<string> ids;
    int duration=-1;
    bool stackOnReapply=false;
};
struct WarheadGrantRule : PassiveGrant {
    vector<string> houses;
    bool affectsOwner=true,affectsEnemies=true,affectsNeutral=true;
};
struct GrantPreset : PassiveGrant {
    vector<TechnoTypeClass*> targetTypes;
};
std::map<int,GrantPreset> grantPresets;
std::map<WarheadTypeClass*,WarheadGrantRule> grantRules;
bool readGrant(PassiveGrant& grant,const string& section,const char* listKey,const string& list,const char* durationKey) {
    grant.ids=split(list);
    if(grant.ids.empty() || std::any_of(grant.ids.begin(),grant.ids.end(),[](const string& id){return !skills.contains(id);}))
        return invalidField(section,string(listKey)+"="+list);
    auto duration=read(section,durationKey,"-1");
    if(duration!="-1" && (grant.duration=number(duration))<=0)
        return invalidField(section,string(durationKey)+"="+duration);
    return true;
}
void readGrantPresets() {
    const string section="Starfall.PassiveGrants";
    for(const auto& key:sectionKeys(section)) {
        int index=number(key);
        if(index<0 || key!=std::to_string(index)) {invalidField(section,key);continue;}
        auto id=read(section,key.c_str());
        if(id.empty()) {invalidField(section,key+" (missing preset)");continue;}
        GrantPreset preset;
        if(!readGrant(preset,id,"Passives",read(id,"Passives"),"Duration")) continue;
        auto types=split(read(id,"TargetTypes"));
        for(const auto& name:types) preset.targetTypes.push_back(TechnoTypeClass::Find(name.c_str()));
        if(std::find(preset.targetTypes.begin(),preset.targetTypes.end(),nullptr)!=preset.targetTypes.end()) {
            invalidField(id,"TargetTypes");continue;
        }
        grantPresets.emplace(index,std::move(preset));
    }
}
void readGrantRules() {
    for(auto wh:WarheadTypeClass::Array) {
        auto list=read(wh->ID,"Starfall.GrantPassives");
        if(list.empty()) continue;
        WarheadGrantRule rule;
        if(!readGrant(rule,wh->ID,"Starfall.GrantPassives",list,"Starfall.GrantPassives.Duration")) continue;
        auto reapply=read(wh->ID,"Starfall.GrantPassives.StackOnReapply","no");
        if(reapply!="yes" && reapply!="no") {invalidField(wh->ID,"Starfall.GrantPassives.StackOnReapply="+reapply);continue;}
        rule.stackOnReapply=reapply=="yes";
        rule.houses=split(read(wh->ID,"Starfall.GrantPassives.Houses","All"));
        if(rule.houses.empty() || std::any_of(rule.houses.begin(),rule.houses.end(),[](const string& h){
            return !includes({"all","self","allies","enemies","neutral"},h);
        })) {invalidField(wh->ID,"Starfall.GrantPassives.Houses");continue;}
        auto ini=CCINIClass::INI_Rules;
        rule.affectsOwner=ini?ini->ReadBool(wh->ID,"AffectsOwner",wh->AffectsAllies):wh->AffectsAllies;
        rule.affectsEnemies=!ini || ini->ReadBool(wh->ID,"AffectsEnemies",true);
        rule.affectsNeutral=!ini || ini->ReadBool(wh->ID,"AffectsNeutral",true);
        grantRules.emplace(wh,std::move(rule));
    }
}
void readSkills() {
    GetFullPathNameA("Starfall.ini", MAX_PATH, configPath, nullptr);
    installRangeGetter<UnitClass>();installRangeGetter<InfantryClass>();
    installRangeGetter<AircraftClass>();installRangeGetter<BuildingClass>();
    readPercentLimits();
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
        s.id=id; s.trigger=read(id,"Trigger");
        s.signal=read(id,"Signal");
        s.senderHouses=split(read(id,"Filter.SenderHouse","All"));
        s.senderKinds=split(read(id,"Filter.SenderKind","All"));
        s.houses=split(read(id,"Filter.TargetHouse","All"));
        s.kinds=split(read(id,"Filter.TargetKind","All"));
        for(const auto& value:s.houses) if(!includes({"all","self","allies","enemies","neutral","none"},value))
            configLog("["+id+"] Filter.TargetHouse="+value+" - 未知的所属方筛选值，不会匹配");
        for(const auto& value:s.kinds) if(!includes({"all","infantry","vehicle","aircraft","building","other","none"},value))
            configLog("["+id+"] Filter.TargetKind="+value+" - 未知的单位类别，不会匹配");
        s.count=number(read(id,"Condition.Count","1"));
        s.cooldown=number(read(id,"Cooldown","0"));
        s.chance=number(read(id,"Chance","100%"),true);
        s.minDamage=number(read(id,"Condition.MinDamage","0"));
        s.maxTriggers=number(read(id,"MaxTriggers","0"));
        auto interval=read(id,"Interval");
        if(s.trigger=="oninterval" || !interval.empty()) {
            s.interval=number(interval);
            if(s.trigger!="oninterval" || s.interval<=0) {invalidField(id,"Interval="+interval);continue;}
        }
        auto fields=sectionKeys(id);
        auto stackMax=read(id,"Stack.Max");
        if(!stackMax.empty() && (s.stackMax=number(stackMax))<=0) {invalidField(id,"Stack.Max="+stackMax);continue;}
        if(!readAnimations(s,fields)) continue;
        if(s.trigger.empty() && s.anim && std::all_of(fields.begin(),fields.end(),[](const string& key){return key=="anim" || key=="anim.always" || key=="stack.max";})) {
            skills.emplace(id,s); anyAttachedAnims=true; continue;
        }
        if(!parseState(s,fields)) continue;
        if(s.conditionFormula.expression.usesSignal() && s.trigger!="onsignal") {invalidField(id,"Sender and Event.SignalValue require OnSignal");continue;}
        needMotion|=(s.requiredMask&16)!=0;
        needUnitPower|=(s.requiredMask&2)!=0;
        if(s.trigger=="whilestate") { skills.emplace(id,s); anyStateSkills=true; anyAttachedAnims|=s.anim!=nullptr; continue; }
        if(s.count<=0 || s.cooldown<0 || s.chance<0 || s.chance>100 || s.minDamage<0 || s.maxTriggers<0) {
            for(auto [key,invalid]:{std::pair{"Condition.Count",s.count<=0},{"Chance",s.chance<0 || s.chance>100},
                 {"Cooldown",s.cooldown<0},{"MaxTriggers",s.maxTriggers<0},{"Condition.MinDamage",s.minDamage<0}})
                if(invalid) invalidField(id,string(key)+"="+read(id,key));
            continue;
        }
        if(!includes({"onkill","ondealdamage","ontakedamage","onbeforedealdamage","onbeforetakedamage","ondeath","onfire","onbeforefire","ondeploy","onpromote","onpromoteveteran","onpromoteelite","oninterval","onpassivegained","onpassengerconsumed","onsignal"},s.trigger)) {
            invalidField(id,"Trigger="+s.trigger);continue;
        }
        if(s.trigger=="onsignal" && (s.signal.empty() || s.signal.size()>127)) {invalidField(id,"Signal");continue;}
        if(s.trigger!="onsignal" && !s.signal.empty()) {invalidField(id,"Signal requires OnSignal");continue;}
        if(s.trigger!="onsignal" && (fields.contains("filter.senderhouse") || fields.contains("filter.senderkind"))) {invalidField(id,"Filter.Sender* requires OnSignal");continue;}
        if(std::any_of(s.senderHouses.begin(),s.senderHouses.end(),[](const auto& x){return !includes({"all","self","allies","enemies","neutral","none"},x);}) ||
           std::any_of(s.senderKinds.begin(),s.senderKinds.end(),[](const auto& x){return !includes({"all","infantry","vehicle","aircraft","building","other","none"},x);})) {invalidField(id,"Filter.Sender*");continue;}
        bool hasAction=fields.contains("action") || fields.contains("actions") ||
            std::any_of(fields.begin(),fields.end(),[](const string& key){return key.starts_with("action.");});
        if(s.trigger=="onbeforefire" && (fields.contains("actions") || s.count!=1 || s.chance!=100 || s.cooldown || s.maxTriggers || s.minDamage || s.triggerAnim)) {
            invalidField(id,"OnBeforeFire requires one deterministic ReplaceWeapon action");continue;
        }
        if((hasAction || !s.triggerAnim) && !readActions(s,fields)) continue;
        if(s.trigger=="onbeforefire" && (s.actions.size()!=1 || s.actions[0].action!="replaceweapon")) {
            invalidField(id,"OnBeforeFire requires ReplaceWeapon");continue;
        }
        if(beforeHealthLoss(s.trigger) && (s.actions.empty() || std::any_of(s.actions.begin(),s.actions.end(),[](const auto& action) {
            return action.action!="setdamage" && action.action!="blockdamage";
        }))) {
            invalidField(id,s.trigger+" requires SetDamage or BlockDamage");continue;
        }
        skills.emplace(id,s);
        anyAttachedAnims|=s.anim!=nullptr;
        anyIntervals|=s.trigger=="oninterval";
        observePromotions|=s.trigger=="onpromote" || s.trigger=="onpromoteveteran" || s.trigger=="onpromoteelite";
        observeDeployments|=s.trigger=="ondeploy";
    }
    // Resolve forward references after all skills are parsed, including dependencies of rejected grants.
    while(std::erase_if(skills,[](const auto& entry) {
        for(const auto& id:entry.second.aura.passives) {
            auto child=skills.find(id);
            if(child==skills.end() || child->second.aura.range) {
                invalidField(entry.first,"Modifier.Aura.Passives="+id+" (requires a registered non-aura passive)");return true;
            }
        }
        for(const auto& id:entry.second.spawnPassives) if(!skills.contains(id)) {
            invalidField(entry.first,"Modifier.SpawnPassives="+id);return true;
        }
        for(const auto& id:entry.second.transportPassives) {
            auto child=skills.find(id);
            if(child==skills.end() || !child->second.transportPassives.empty()) {
                invalidField(entry.first,"Modifier.TransportPassives="+id+" (requires a registered non-transport-support passive)");return true;
            }
        }
        for(const auto& a:entry.second.actions) if((a.action=="grantpassive" || a.action=="changepassivestack") && !skills.contains(a.passive)) {
            invalidField(entry.first,"Action.Passive="+a.passive);
            return true;
        }
        for(const auto& a:entry.second.actions) if(a.action=="stealpassive") {
            for(const auto& id:a.passiveList) if(!skills.contains(id)) {
                invalidField(entry.first,"Action.Passive="+id);
                return true;
            }
        }
        return false;
    })) {}
    anyAuras=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return entry.second.aura.range!=0;});
    anySpawnPassives=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return !entry.second.spawnPassives.empty();});
    anyTransportPassives=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return !entry.second.transportPassives.empty();});
    anyGainSkills=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return entry.second.trigger=="onpassivegained";});
    anyBeforeDealDamage=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return entry.second.trigger=="onbeforedealdamage";});
    anyBeforeTakeDamage=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return entry.second.trigger=="onbeforetakedamage";});
    readGrantRules();
    readGrantPresets();
    readResearch();
    ready=true;
}

struct Progress { int count=0; unsigned next=0; };
using Counts = std::map<DWORD,std::map<string,Progress>>;
Counts counts;
// Successful capped activations survive temporary passive removal and ownership changes.
std::map<DWORD,std::map<string,unsigned>> triggerTotals;
// Only nonzero values are stored; layers belong to one unit and one passive ID.
std::map<DWORD,std::map<string,int>> passiveStacks;
int stackCount(DWORD unit,const string& skill) {
    auto owner=passiveStacks.find(unit);
    int base=0;
    if(owner!=passiveStacks.end()) if(auto value=owner->second.find(skill);value!=owner->second.end()) base=value->second;
    unsigned layers=0;
    if(auto carrier=transportGrants.find(unit);carrier!=transportGrants.end())
        if(auto grant=carrier->second.find(skill);grant!=carrier->second.end()) layers=grant->second.layers;
    auto def=skills.find(skill);
    int maximum=def==skills.end()?INT_MAX:def->second.stackMax;
    return static_cast<int>(std::min<int64_t>(maximum,int64_t(base)+layers));
}
void eraseStacks(DWORD unit,const string& skill) {
    auto found=passiveStacks.find(unit);
    if(found==passiveStacks.end()) return;
    found->second.erase(skill);
    if(found->second.empty()) passiveStacks.erase(found);
}
bool changeStacks(DWORD unit,const string& id,int amount,int maximum) {
    int base=0;
    if(auto owner=passiveStacks.find(unit);owner!=passiveStacks.end()) if(auto value=owner->second.find(id);value!=owner->second.end()) base=value->second;
    int before=stackCount(unit,id);
    int upper=amount>0?std::max(base,std::max(0,maximum-(before-base))):maximum;
    int after=static_cast<int>(std::clamp<int64_t>(int64_t(base)+amount,0,upper));
    if(after==base) return false;
    if(after) passiveStacks[unit][id]=after;
    else eraseStacks(unit,id);
    return true;
}
using PassengerPassives=std::map<string,int>;
struct SignalChain { DWORD id=0,remaining=1024; };
DWORD nextSignalChain=1;
struct Event {
    string trigger, kind, relation;
    DWORD holder, attacker, victim;
    int damage, hp, maxHp, state=unknownState;
    CoordStruct impact{};
    bool hasImpact=false;
    int ammo=-1, passengers=-1, owner=-1;
    int attackerOwner=-1, victimOwner=-1;
    CoordStruct origin{};
    vector<string> snapshot; // skill IDs for a dying or replaced deployer
    double distance=-1; // Horizontal distance in cells, -1 when no target was available.
    std::map<string,int> stackSnapshot;
    int targetHp=0,targetMaxHp=0;
    std::optional<int> selfCost;
    std::optional<int> targetCost; // Absent for ground targets and older event snapshots.
    // The action chain and the carrier's queued event draw from the same remaining stacks.
    DWORD consumedTarget=0;
    string consumedKind;
    int consumedOwner=-1;
    CoordStruct consumedPosition{};
    std::shared_ptr<PassengerPassives> passengerPassives;
    string signal,senderKind,senderRelation;
    int senderHp=0,senderMaxHp=0;
    std::optional<int> senderCost;
    CoordStruct signalSenderPosition{};
    double signalValue=0;
    unsigned signalDepth=0;
    std::shared_ptr<SignalChain> signalChain;
};
vector<Event> pending;
bool applyingDamage=false;
std::map<DWORD,std::set<string>> removedPassives;
struct GrantTime {
    unsigned start;
    int duration;
    int remaining(unsigned frame) const {
        if(duration<0) return -1;
        unsigned elapsed=frame-start;
        return elapsed>=static_cast<unsigned>(duration)?0:duration-static_cast<int>(elapsed);
    }
};
struct UnitGrants {
    TechnoClass* unit=nullptr; // Rebound by object ID after loading a game.
    std::map<string,GrantTime> skills;
};
std::map<DWORD,UnitGrants> warheadGrants;
bool rebindGrants=false;
// Native damage and extensions use the registered warhead's identity. Keep the
// radius scoped to this synchronous impact, including re-entrant detonations.
struct SpreadScope {
    DWORD frame;
    WarheadTypeClass* wh;
    float original,effective;
};
vector<SpreadScope> spreadScopes;
struct ExplosionBatch { int first,scale; };
std::map<DWORD,ExplosionBatch> explosionBatches;

// Capture only the impact animation creation stage, after area damage and before
// debris. Phobos can append several AnimList or SplashList objects to the array.
void finishExplosions(DWORD frame) {
    auto batch=explosionBatches.find(frame);
    if(batch==explosionBatches.end()) return;
    for(int i=batch->second.first;i<AnimClass::Array.Count;++i) {
        if(auto anim=AnimClass::Array.Items[i])
            VisualScale::explosions.try_emplace(anim->UniqueID,batch->second.scale);
    }
    explosionBatches.erase(batch);
}
void enterSpread(DWORD frame,WarheadTypeClass* wh,int percent) {
    if(spreadScopes.empty() && (percent==100 || !wh || !(wh->CellSpread>0))) return;
    if(!spreadScopes.empty()) {
        const auto& outer=spreadScopes.back();
        if(outer.wh) outer.wh->CellSpread=outer.original;
    }
    float original=wh?wh->CellSpread:0;
    float effective=original;
    // The native cell-offset table ends at ten cells. Preserve larger mod-defined radii.
    if(percent!=100 && original>0)
        effective=static_cast<float>(std::min(original*(percent/100.0),std::max(10.0,double(original))));
    spreadScopes.push_back({frame,wh,original,effective});
    if(wh) wh->CellSpread=effective;
}
void leaveSpread(DWORD frame) {
    if(spreadScopes.empty() || spreadScopes.back().frame!=frame) return;
    auto top=spreadScopes.back();
    if(top.wh) top.wh->CellSpread=top.original;
    spreadScopes.pop_back();
    if(!spreadScopes.empty()) {
        const auto& outer=spreadScopes.back();
        if(outer.wh) outer.wh->CellSpread=outer.effective;
    }
}
// A bullet, its native area pass and the per-victim damage callbacks can all
// report the same hit. Nested detonations get their own recipient set.
struct ReapplyImpact {
    DWORD frame;
    WarheadTypeClass* wh;
    std::set<DWORD> recipients;
};
vector<ReapplyImpact> reapplyImpacts;
void beginReapplyImpact(DWORD frame,WarheadTypeClass* wh) {
    auto rule=grantRules.find(wh);
    if(!reapplyImpacts.empty() || (rule!=grantRules.end() && rule->second.stackOnReapply))
        reapplyImpacts.push_back({frame,wh,{}});
}
void endReapplyImpact(DWORD frame) {
    if(!reapplyImpacts.empty() && reapplyImpacts.back().frame==frame) reapplyImpacts.pop_back();
}
struct GrantImpact { WarheadTypeClass* wh; CoordStruct at; HouseClass* source; };
std::map<DWORD,GrantImpact> bulletGrantImpacts;
std::set<DWORD> passiveBullets;
std::set<DWORD> passiveDetonations;
bool suppressed(DWORD id,const string& skill) {
    auto it=removedPassives.find(id);
    if(it==removedPassives.end() || !it->second.contains(skill)) return false;
    auto grants=warheadGrants.find(id);
    if(grants==warheadGrants.end()) return true;
    auto entry=grants->second.skills.find(skill);
    return entry==grants->second.skills.end() || !entry->second.remaining(Unsorted::CurrentFrame);
}
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
    if(fresh) {
        it->second=split(read(typeID,"Starfall.Passives"));
        for(const auto& id:it->second) if(!skills.contains(id))
            configLog("["+string(typeID)+"] Starfall.Passives="+id+" - 引用的被动未注册或已停用");
    }
    return it->second;
}
bool researchSkills(TechnoClass* unit,vector<string>* active=nullptr) {
    if(!unit->Owner) return false;
    auto unlocked=Research::unlocked.find(unit->Owner->ArrayIndex);
    if(unlocked==Research::unlocked.end()) return false;
    const auto type=lower(unit->GetTechnoType()->ID);
    bool found=false;
    for(const auto& o:Research::options) {
        if(!unlocked->second.contains(o.id)) continue;
        auto add=[&](Research::Effect effect,const vector<string>& types,const string& passive) {
            if(effect!=Research::Effect::Passive || !Research::matchesTarget(types,type,kind(unit))) return;
            found=true;if(active && !includes(*active,passive)) active->push_back(passive);
        };
        if(o.effectList) for(const auto& effect:o.effects) add(effect.effect,effect.types,effect.passive);
        else add(o.effect,o.types,o.passive);
        if(found && !active) break;
    }
    return found;
}
bool extraSkills(TechnoClass* unit,vector<string>* active=nullptr) {
    bool found=researchSkills(unit,active);
    if(auto grants=transportGrants.find(unit->UniqueID);grants!=transportGrants.end()) {
        found=true;
        if(active) for(const auto& [id,grant]:grants->second) if(!includes(*active,id)) active->push_back(id);
    }
    if(auto aura=auraGrants.find(unit->UniqueID);aura!=auraGrants.end()) {
        found=true;
        if(active) for(const auto& grant:aura->second) if(!includes(*active,grant.first)) active->push_back(grant.first);
    }
    if(!spawnGrants.empty() && unit->SpawnOwner) {
        auto grant=spawnGrants.find(unit->UniqueID);
        auto parent=unit->SpawnOwner;
        if(grant!=spawnGrants.end() && grant->second.parent==parent->UniqueID &&
           alive(parent) && parent->IsOnMap && !parent->InLimbo && parent->Owner==unit->Owner) {
            found=true;
            if(active) for(const auto& id:grant->second.ids) if(!includes(*active,id)) active->push_back(id);
        }
    }
    auto it=warheadGrants.find(unit->UniqueID);
    if(it!=warheadGrants.end()) for(const auto& [id,time]:it->second.skills) {
        if(!time.remaining(Unsorted::CurrentFrame)) continue;
        found=true;
        if(!active) break;
        if(!includes(*active,id)) active->push_back(id);
    }
    return found;
}
void reconcileUnitStacks(TechnoClass* unit) {
    if(!unit) return;
    auto group=passiveStacks.find(unit->UniqueID);
    if(group==passiveStacks.end()) return;
    auto ids=boundSkills(unit->GetTechnoType()->ID);extraSkills(unit,&ids);
    for(auto it=group->second.begin();it!=group->second.end();) {
        auto skill=skills.find(it->first);
        if(skill==skills.end() || suppressed(unit->UniqueID,it->first) || !includes(ids,it->first)) it=group->second.erase(it);
        else {it->second=std::min(it->second,skill->second.stackMax);++it;}
    }
    if(group->second.empty()) passiveStacks.erase(group);
}
void reconcileAllStacks() {
    if(passiveStacks.empty()) return;
    for(auto unit:TechnoClass::Array) if(unit && passiveStacks.contains(unit->UniqueID)) reconcileUnitStacks(unit);
}
DeployPosture deploymentOf(const TechnoClass* unit) {
    switch(unit->WhatAmI()) {
        case AbstractType::Infantry: {
            auto infantry=static_cast<const InfantryClass*>(unit);
            if(infantry->IsDeployed()) return DeployPosture::Deployed;
            return infantry->Type->Deployer?DeployPosture::Undeployed:DeployPosture::None;
        }
        case AbstractType::Unit: {
            auto vehicle=static_cast<const UnitClass*>(unit);
            if(vehicle->Deployed) return DeployPosture::Deployed;
            return vehicle->Type->IsSimpleDeployer?DeployPosture::Undeployed:DeployPosture::None;
        }
        default: return DeployPosture::None;
    }
}
int stateOf(TechnoClass* unit) {
    int bits=(static_cast<int>(unit->CloakState)&3)<<2;
    if(unit->Owner && unit->Owner->HasFullPower()) bits|=1;
    if(needMotion && (unit->AbstractFlags & AbstractFlags::Foot)!=AbstractFlags::None) {
        auto foot=static_cast<FootClass*>(unit);
        if(foot->Locomotor && foot->Locomotor->Is_Really_Moving_Now()) bits|=16;
    }
    if(needUnitPower && !unit->Deactivated && !unit->IsUnderEMP() && unit->IsPowerOnline()) bits|=2;
    return bits;
}
bool meetsState(const Skill& s,int bits,int hp,int maximum,int ammo,int passengers) {
    return !s.requiredMask || (bits!=unknownState && (bits&s.requiredMask)==s.requiredBits);
}
struct Applied {
    Factors base=identity,last=identity;
    Factors state=identity;
    Factors percent=defaultStatPercent;
    std::array<bool,3> active{};
};
std::map<DWORD,Applied> applied;
void readPercentLimits() {
    constexpr const char* section="Starfall.PassiveLimits";
    constexpr std::array keys={"Speed","Firepower","Armor","Scale","ProjectileScale","WeaponRange","FireRate"};
    for(size_t k=0;k<keys.size();++k) {
        int limit=number(read(section,keys[k],"300%"),true);
        if(limit<1) {
            std::ofstream("Starfall.log",std::ios::app)<<"Starfall: ["<<section<<"] invalid "<<keys[k]<<", using 300%\n";
            limit=300;
        }
        const string minKey=string(keys[k])+".Min";
        int defaultMin=k==scaleIndex?25:1;
        defaultMin=std::min(defaultMin,limit);
        int minimum=number(read(section,minKey.c_str(),k==scaleIndex?"25%":"1%"),true);
        if(minimum<1 || minimum>limit) {
            std::ofstream("Starfall.log",std::ios::app)<<"Starfall: ["<<section<<"] invalid "<<minKey<<", using "<<defaultMin<<"%\n";
            minimum=defaultMin;
        }
        percentLimits[k]={minimum,limit};
    }
    VisualScale::minimum=percentLimits[scaleIndex].min;
    // Loaded games can predate the size cap or use a different ruleset.
    for(auto& [id,size]:VisualScale::base) size=percentLimits[scaleIndex].clamp(size);
    std::erase_if(VisualScale::base,[](const auto& entry){return entry.second==100;});
    for(auto& [id,size]:projectileBase) size=percentLimits[projectileScaleIndex].clamp(size);
    std::erase_if(projectileBase,[](const auto& entry){return entry.second==100;});
    for(auto& [id,size]:rangeBase) size=percentLimits[weaponRangeIndex].clamp(size);
    std::erase_if(rangeBase,[](const auto& entry){return entry.second==100;});
    for(auto& [id,percent]:fireRateBase) percent=percentLimits[fireRateIndex].clamp(percent);
    std::erase_if(fireRateBase,[](const auto& entry){return entry.second==100;});
    for(auto& [id,record]:applied) for(size_t k=0;k<record.percent.size();++k)
        if(record.percent[k]!=100) record.percent[k]=percentLimits[k].clamp(record.percent[k]);
}
std::array<double*,3> statFields(TechnoClass* unit) {
    auto speed=(unit->AbstractFlags & AbstractFlags::Foot)!=AbstractFlags::None ?
        &static_cast<FootClass*>(unit)->SpeedMultiplier : nullptr;
    return {speed,&unit->FirepowerMultiplier,&unit->ArmorMultiplier};
}
void updateStat(Applied& record,size_t k,double& value) {
    // An absolute engine write (e.g. a crate) becomes the new native baseline.
    if(!record.active[k] || value!=record.last[k]) record.base[k]=value;
    double factor=record.state[k]*(record.percent[k]/100.0);
    if(factor!=1.0 || record.active[k]) value=record.base[k]*factor;
    record.last[k]=value;
    record.active[k]=factor!=1.0;
}
struct StateTimer { unsigned start; bool observed; };
using TimerKey=std::pair<DWORD,string>;
std::map<TimerKey,StateTimer> stateTimers;
struct AttachedAnim { DWORD id; int slot=-1; bool seen=false; };
std::map<TimerKey,AttachedAnim> attachedAnims;
std::set<TimerKey> activeAnimStates;

AnimClass* playSkillAnim(AnimTypeClass* type,TechnoClass* unit,const CoordStruct& at,HouseClass* house,bool loop=false) {
    auto anim=GameCreate<AnimClass>(type,unit?unit->Location:at);
    if(anim) {
        anim->IsInert=true;
        if(unit) anim->SetOwnerObject(unit);
        anim->Owner=unit?unit->Owner:house;
        anim->RemainingIterations=loop?0xFF:1;
    }
    return anim;
}
// Native animation objects can expire independently. Validate their IDs against
// the live array, and rebuild the slot index only when the array has shifted.
struct AnimFrame {
    std::unordered_map<DWORD,int> slots;
    vector<AnimClass*> retire;
    AnimClass* resolve(AttachedAnim& record) {
        auto& array=AnimClass::Array;
        if(record.slot>=0 && record.slot<array.Count) {
            auto anim=array.Items[record.slot];
            if(anim && anim->UniqueID==record.id) return anim;
        }
        if(slots.empty()) for(int i=0;i<array.Count;++i)
            if(auto anim=array.Items[i]) slots.emplace(anim->UniqueID,i);
        auto found=slots.find(record.id);
        if(found==slots.end()) return nullptr;
        record.slot=found->second;
        return array.Items[record.slot];
    }
    void attach(TechnoClass* unit,const Skill& s) {
        auto [it,fresh]=attachedAnims.try_emplace({unit->UniqueID,s.id},AttachedAnim{});
        auto& record=it->second;
        auto anim=fresh?nullptr:resolve(record);
        if(anim && (anim->OwnerObject!=unit || anim->Type!=s.anim || anim->TimeToDie)) {
            retire.push_back(anim); anim=nullptr;
        }
        if(!anim) {
            anim=playSkillAnim(s.anim,unit,unit->Location,unit->Owner,true);
            if(!anim) {attachedAnims.erase(it);return;}
            record.id=anim->UniqueID;
            record.slot=AnimClass::Array.Count-1;
        }
        anim->Owner=unit->Owner;
        VisualScale::attachedAnims[anim->UniqueID]=unit->UniqueID;
        record.seen=true;
    }
    void finish() {
        std::erase_if(attachedAnims,[&](auto& item) {
            if(item.second.seen) return false;
            auto record=item.second;
            VisualScale::attachedAnims.erase(record.id);
            if(auto anim=resolve(record)) retire.push_back(anim);
            return true;
        });
        for(auto anim:retire) anim->UnInit();
    }
};
// State rules are derived from the unit every frame. Never change a shared type's weapon array.
std::map<DWORD,WeaponRules> weaponRules;
struct VolleyWeapon { int slot; WeaponTypeClass* replacement; unsigned frame; };
std::map<DWORD,VolleyWeapon> volleyWeapons;
struct MountCopy { WeaponStruct original,visible; };
std::map<std::pair<DWORD,int>,MountCopy> mountCopies;
struct BuildingLookup { DWORD id; int slot; };
std::map<DWORD,BuildingLookup> buildingLookups;
const SlotRule* slotRule(DWORD id,int slot) {
    if(slot<0 || slot>1 || weaponRules.empty()) return nullptr;
    auto it=weaponRules.find(id);
    return it==weaponRules.end()?nullptr:&it->second[slot];
}
void applyMount(REGISTERS* R,DWORD id,int slot) {
    const auto rule=slotRule(id,slot);
    auto volley=volleyWeapons.find(id);
    auto replacement=volley!=volleyWeapons.end() && volley->second.slot==slot && volley->second.replacement?
        volley->second.replacement:rule?rule->replacement:nullptr;
    auto native=R->EAX<WeaponStruct*>();
    if(!replacement || !native) return;
    auto& copy=mountCopies[{id,slot}];
    // Ordinary buildings delegate to the Techno getter and reach both hooks.
    if(native!=&copy.visible) copy.original=*native;
    copy.visible=copy.original;
    copy.visible.WeaponType=replacement;
    R->EAX(&copy.visible);
}
void updateSlots(DWORD id,const WeaponRules& next) {
    bool active=false;
    for(int slot=0;slot<2;++slot) {
        active|=next[slot].disabled || next[slot].replacement;
        auto existing=mountCopies.find({id,slot});
        if(existing!=mountCopies.end())
            existing->second.visible.WeaponType=next[slot].replacement?next[slot].replacement:existing->second.original.WeaponType;
    }
    if(active) weaponRules[id]=next;
    else weaponRules.erase(id);
}
struct NearbySample {unsigned next; int owner; bool matches,seen;};
std::map<TimerKey,NearbySample> nearbySamples;
template<class Visitor>
bool visitNearbyCells(CellStruct center,int r,bool includeAir,Visitor&& visit) {
    auto list=[&](ObjectClass* object){
        for(;object;object=object->NextObject)
            if((object->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None && visit(static_cast<TechnoClass*>(object))) return true;
        return false;
    };
    for(int y=-r;y<=r;++y) for(int x=-r;x<=r;++x) {
        if(x*x+y*y>r*r) continue;
        CellStruct pos{static_cast<short>(center.X+x),static_cast<short>(center.Y+y)};
        auto cell=MapClass::Instance.TryGetCellAt(pos);
        if(cell && (list(cell->FirstObject) || list(cell->AltObject))) return true;
    }
    // Aircraft and jumpjets can be absent from the ground object chains.
    if(auto cell=MapClass::Instance.TryGetCellAt(center);cell && includeAir) {
        auto& tracker=AircraftTrackerClass::Instance;
        tracker.FillCurrentVector(cell,r);
        while(auto air=tracker.Get()) {
            auto pos=air->GetMapCoords(); int x=pos.X-center.X,y=pos.Y-center.Y;
            if(x*x+y*y<=r*r && visit(air)) return true;
        }
    }
    return false;
}
bool scanNearby(TechnoClass* unit,const NearbyRule& rule) {
    auto owner=unit->Owner;
    if(!owner) return false;
    auto center=unit->GetMapCoords(); const int r=rule.range;
    int count=0; std::set<DWORD> seen;
    // For an upper bound the first excess match decides immediately; for a
    // lower bound alone the last required match decides immediately.
    auto visit=[&](TechnoClass* candidate) {
        if(candidate==unit || !alive(candidate) || !candidate->IsOnMap || candidate->InLimbo || !candidate->Owner) return false;
        auto other=candidate->Owner;
        bool allied=owner->IsAlliedWith(other);
        const char* house=other==owner?"self":allied?"allies":other->IsNeutral()?"neutral":"enemies";
        if(!includes(rule.houses,"all") && !includes(rule.houses,house)) return false;
        if(!includes(rule.kinds,"all") && !includes(rule.kinds,kind(candidate))) return false;
        if(other!=owner && !allied) {
            if(candidate->CloakState!=CloakState::Uncloaked) {
                const auto cell=MapClass::Instance.TryGetCellAt(candidate->GetMapCoords());
                if(!cell || owner->ArrayIndex<0 || owner->ArrayIndex>=24 || !cell->Sensors_InclHouse(owner->ArrayIndex)) return false;
            }
            if(candidate->Disguised && !candidate->IsClearlyVisibleTo(owner)) return false;
        }
        if(!seen.insert(candidate->UniqueID).second) return false;
        ++count;return count>rule.most || (rule.most==INT_MAX && count>=rule.least);
    };
    if(visitNearbyCells(center,r,!includes(rule.kinds,"building") || rule.kinds.size()>1,visit))
        return count<=rule.most;
    return count>=rule.least && count<=rule.most;
}
bool nearbyState(TechnoClass* unit,const Skill& s,unsigned frame) {
    const auto& rule=s.nearby;
    if(!rule.range) return true;
    int owner=unit->Owner?unit->Owner->ArrayIndex:-1;
    unsigned offset=unit->UniqueID%static_cast<unsigned>(rule.interval);
    NearbySample initial{frame+offset,owner,false,true};
    auto& sample=nearbySamples.try_emplace({unit->UniqueID,s.id},initial).first->second;
    if(sample.owner!=owner) sample=initial;
    sample.seen=true;
    if(static_cast<int>(frame-sample.next)>=0) {
        sample.matches=scanNearby(unit,rule);sample.next=frame+rule.interval;
    }
    return sample.matches;
}
double distanceCells(const CoordStruct& a,const CoordStruct& b);
bool stateActive(TechnoClass* unit,const Skill& s,int bits,UnitHealth::Values hp,unsigned frame) {
    auto type=unit->GetTechnoType();
    if(!meetsState(s,bits,hp.current,hp.maximum,type->Ammo<0?-1:unit->Ammo,unit->Passengers.NumPassengers)) return false;
    if(!s.conditionFormula.expression.empty()) {
        auto aim=unit->Target;
        std::optional<double> distance;
        std::optional<HealthFormula::Values> target;
        if(aim && s.conditionFormula.expression.usesDistance()) {
            auto point=(aim->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None ?
                static_cast<TechnoClass*>(aim)->GetCenterCoords():aim->GetCoords();
            distance=distanceCells(unit->GetCenterCoords(),point);
        }
        if(aim && (aim->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None && s.conditionFormula.expression.usesTargetValues()) {
            auto other=static_cast<TechnoClass*>(aim);auto otherType=other->GetTechnoType();
            auto value=UnitHealth::read(other,otherType->Strength);
            target=HealthFormula::Values{value.current,value.maximum,otherType->Cost};
        }
        bool missing=false;
        auto value=s.conditionFormula.expression.evaluate(hp.current,hp.maximum,0,distance,stackCount(unit->UniqueID,s.id),
            target,&missing,type->Cost,type->Ammo<0?-1:unit->Ammo,unit->Passengers.NumPassengers);
        if(!value) {if(!missing) s.conditionFormula.report();return false;}
        if(*value==0) return false;
    }
    if(s.deployPosture!=DeployPosture::None && deploymentOf(unit)!=s.deployPosture) return false;
    if(!nearbyState(unit,s,frame)) return false;
    if(!s.stateDelayFrames) return true;
    auto& timer=stateTimers.try_emplace({unit->UniqueID,s.id},StateTimer{frame,true}).first->second;
    timer.observed=true;
    return frame-timer.start>=s.stateDelayFrames;
}
std::set<DWORD> mirageUnits,immuneUnits,mindImmuneUnits;
double distanceCells(const CoordStruct& a,const CoordStruct& b);
struct BuildingPowerState {
    HouseClass* owner;
    double multiplier;
    bool operator==(const BuildingPowerState&) const = default;
};
std::map<DWORD,BuildingPowerState> buildingPower;
void updateBuildingPower(BuildingClass* building,double multiplier) {
    auto id=building->UniqueID;
    auto old=buildingPower.find(id);
    BuildingPowerState next{building->Owner,multiplier};
    if(old!=buildingPower.end()) {
        if(old->second==next) return;
        if(auto owner=old->second.owner) owner->RecheckPower=true;
        buildingPower.erase(old);
    }
    if(next.owner && multiplier!=1) {
        buildingPower.emplace(id,next);
        next.owner->RecheckPower=true;
    }
}
int powerContribution(BuildingClass* building,int base,int accumulated) {
    if(base<=0 || buildingPower.empty()) return base;
    auto it=buildingPower.find(building->UniqueID);
    double factor=1;
    if(it!=buildingPower.end() && it->second.owner==building->Owner)
        factor=it->second.multiplier;
    const int room=INT_MAX-std::max(0,accumulated);
    return static_cast<int>(std::min<double>(room,std::round(base*factor)));
}
void tickStates() {
    if(buildingPower.empty() && !anyStateSkills && !anyAttachedAnims && attachedAnims.empty() && activeAnimStates.empty() && applied.empty() && stateTimers.empty() && mirageUnits.empty() && immuneUnits.empty() && mindImmuneUnits.empty()) return;
    AnimFrame animFrame;
    for(auto& [key,anim]:attachedAnims) anim.seen=false;
    std::set<TimerKey> nextAnimStates;
    std::set<DWORD> nextMirage,nextImmune,nextMindImmune;
    const unsigned frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    for(auto& [key,sample]:nearbySamples) sample.seen=false;
    for(auto& [key,timer]:stateTimers) if(!transportStateKeys.contains(key)) timer.observed=false;
    for(auto* unit:TechnoClass::Array) {
        if(!unit) continue;
        reconcileUnitStacks(unit);
        auto old=applied.find(unit->UniqueID);
        Factors desired=identity;
        double scaleFactor=1.0,projectileFactor=1.0,rangeFactor=1.0,fireRateFactor=1.0;
        double powerFactor=1;
        bool berserk=false,mirage=false,immune=false,mindImmune=false;
        WeaponRules nextSlots{};
        const bool onMap=unit->IsOnMap && !unit->InLimbo;
        const bool passengerOnly=!onMap && alive(unit->Transporter) && unit->Transporter->WhatAmI()==AbstractType::Unit;
        if(alive(unit) && (onMap || passengerOnly)) {
            auto type=unit->GetTechnoType();
            const auto& native=boundSkills(type->ID);
            if(native.empty() && old==applied.end() && !weaponRules.contains(unit->UniqueID) && !VisualScale::state.contains(unit->UniqueID) && !projectileState.contains(unit->UniqueID) && !rangeState.contains(unit->UniqueID) && !fireRateState.contains(unit->UniqueID) && !buildingPower.contains(unit->UniqueID) && !mirageUnits.contains(unit->UniqueID) && !extraSkills(unit)) continue;
            auto ids=native;
            extraSkills(unit,&ids);
            int bits=stateOf(unit);
            auto hp=UnitHealth::read(unit,type->Strength);
            for(const auto& id:ids) {
                auto it=skills.find(id);
                if(it==skills.end()) continue;
                auto& s=it->second;
                if(suppressed(unit->UniqueID,id)) continue;
                if(passengerOnly && (!s.passengerCombat || s.nearby.range || s.conditionFormula.expression.usesDistance())) continue;
                bool active=(s.trigger=="whilestate" || (s.anim && !s.animAlways)) && stateActive(unit,s,bits,hp,frame);
                if(onMap && s.anim && (s.animAlways || active) && unit->CloakState==CloakState::Uncloaked) animFrame.attach(unit,s);
                if(s.trigger!="whilestate" || !active) continue;
                if(onMap && s.triggerAnim) {
                    TimerKey key{unit->UniqueID,id};
                    if(!activeAnimStates.contains(key)) {
                        AbstractClass* aim=s.animTarget=="self"?unit:unit->Target;
                        if(aim) {
                            auto target=(aim->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None?static_cast<TechnoClass*>(aim):nullptr;
                            playSkillAnim(s.triggerAnim,target,aim->GetCoords(),unit->Owner);
                        }
                    }
                    nextAnimStates.insert(std::move(key));
                }
                auto multipliers=s.multipliers;
                for(const auto& [index,formula]:s.stateFormulas) {
                    if(passengerOnly && (!passengerCombatMultiplier(index) || formula.expression.usesDistance())) continue;
                    std::optional<double> distance;
                    std::optional<HealthFormula::Values> targetValues;
                    auto aim=unit->Target;
                    if(aim && formula.expression.usesDistance()) {
                        auto point=(aim->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None?
                            static_cast<TechnoClass*>(aim)->GetCenterCoords():aim->GetCoords();
                        distance=distanceCells(unit->GetCenterCoords(),point);
                    }
                    if(aim && (aim->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None && formula.expression.usesTargetValues()) {
                        auto target=static_cast<TechnoClass*>(aim);
                        auto type=target->GetTechnoType();
                        auto values=UnitHealth::read(target,type->Strength);
                        targetValues=HealthFormula::Values{values.current,values.maximum,type->Cost};
                    }
                    bool missing=false;
                    auto value=formula.expression.evaluate(hp.current,hp.maximum,0,distance,stackCount(unit->UniqueID,id),targetValues,&missing,type->Cost,type->Ammo<0?-1:unit->Ammo,unit->Passengers.NumPassengers);
                    if(value && validStateMultiplier(index,*value)) multipliers[index]=*value;
                    else if(!missing) formula.report();
                }
                rangeFactor=std::clamp(rangeFactor*multipliers[3],0.0001,10000.0);
                fireRateFactor=std::clamp(fireRateFactor*multipliers[4],0.0001,10000.0);
                desired[1]=std::clamp(desired[1]*multipliers[1],0.0001,10000.0);
                if(passengerOnly) continue;
                scaleFactor*=s.visualScale/100.0;
                projectileFactor*=s.projectileScale/100.0;
                powerFactor=std::min(10000.0,powerFactor*multipliers[powerMultiplierIndex]);
                berserk|=s.berserk;
                mirage|=s.mirage;
                immune|=s.immune;
                mindImmune|=s.mindImmune;
                for(int slot=0;slot<2;++slot) {
                    nextSlots[slot].disabled|=s.slots[slot].disabled;
                    if(s.slots[slot].replacement) nextSlots[slot].replacement=s.slots[slot].replacement;
                }
                desired[0]=std::clamp(desired[0]*multipliers[0],0.0001,10000.0);
                desired[2]=std::clamp(desired[2]*multipliers[2],0.0001,10000.0);
            }
        }
        if(immune) nextImmune.insert(unit->UniqueID);
        if(mindImmune) nextMindImmune.insert(unit->UniqueID);
        if(mirage && unit->WhatAmI()==AbstractType::Unit) nextMirage.insert(unit->UniqueID);
        else if(mirageUnits.contains(unit->UniqueID) && unit->WhatAmI()==AbstractType::Unit) {
            auto type=unit->GetTechnoType();
            if(type && !(type->CanDisguise && !type->PermaDisguise && type->DisguiseWhenStill)
                && unit->Disguised && unit->Disguise && unit->Disguise->WhatAmI()==AbstractType::TerrainType)
                unit->ClearDisguise();
        }
        // Renew a short native effect. Longer effects from chaos weapons keep
        // their own timer and the engine handles expiration, including on load.
        if(berserk && unit->WhatAmI()!=AbstractType::Building && (!unit->Berzerk || unit->BerzerkDurationLeft<2)) {
            using EnterBerserk=void(__thiscall*)(TechnoClass*,int);
            reinterpret_cast<EnterBerserk>(0x70F890)(unit,2);
        }
        if(unit->WhatAmI()==AbstractType::Building)
            updateBuildingPower(static_cast<BuildingClass*>(unit),powerFactor);
        auto scaleId=unit->UniqueID;
        if(scaleFactor!=1.0 && unit->WhatAmI()!=AbstractType::Building) {
            auto [entry,added]=VisualScale::state.try_emplace(scaleId,scaleFactor);
            if(added || entry->second!=scaleFactor) {entry->second=scaleFactor;unit->MarkForRedraw();}
        } else if(VisualScale::state.erase(scaleId)) unit->MarkForRedraw();
        if(projectileFactor!=1.0) projectileState[scaleId]=projectileFactor;
        else projectileState.erase(scaleId);
        if(rangeFactor!=1.0) rangeState[scaleId]=rangeFactor;
        else rangeState.erase(scaleId);
        if(fireRateFactor!=1.0) fireRateState[scaleId]=fireRateFactor;
        else fireRateState.erase(scaleId);
        updateSlots(unit->UniqueID,nextSlots);
        auto fields=statFields(unit);
        if(!fields[0]) desired[0]=1.0;
        if(old==applied.end() && desired==identity) continue;
        auto& record=applied[unit->UniqueID];
        record.state=desired;
        for(size_t k=0;k<fields.size();++k) if(fields[k]) updateStat(record,k,*fields[k]);
        if(desired==identity && record.percent==defaultStatPercent) applied.erase(unit->UniqueID);
    }
    animFrame.finish();
    mirageUnits.swap(nextMirage);
    immuneUnits.swap(nextImmune);
    mindImmuneUnits.swap(nextMindImmune);
    activeAnimStates.swap(nextAnimStates);
    std::erase_if(nearbySamples,[](const auto& p){return !p.second.seen;});
    std::erase_if(stateTimers,[](const auto& item){return !item.second.observed;});
}
struct Transition {int rank; bool deployed;};
std::map<DWORD,Transition> previousState;
std::set<DWORD> reportedDeaths;
std::map<DWORD,Event> shots;
double distanceCells(const CoordStruct& a,const CoordStruct& b) {
    return std::hypot(static_cast<double>(a.X)-b.X,static_cast<double>(a.Y)-b.Y)/256.0;
}
Event eventSnapshot(const char* name,TechnoClass* holder,TechnoClass* other,TechnoClass* attacker,TechnoClass* victim,int damage) {
    if(alive(holder)) reconcileUnitStacks(holder);
    const char* rel="none";
    if(other && other->Owner && holder->Owner)
        rel=other->Owner==holder->Owner?"self":holder->Owner->IsAlliedWith(other->Owner)?"allies":other->Owner->IsNeutral()?"neutral":"enemies";
    auto type=holder->GetTechnoType();
    auto hp=UnitHealth::read(holder,type->Strength);
    Event e{name,other?kind(other):"none",rel,holder->UniqueID,attacker?attacker->UniqueID:0,
            victim?victim->UniqueID:0,damage,hp.current,hp.maximum,stateOf(holder)};
    e.selfCost=type->Cost;
    e.senderKind=kind(holder);
    e.origin=holder->GetCenterCoords();
    if(auto stacks=passiveStacks.find(holder->UniqueID);stacks!=passiveStacks.end()) e.stackSnapshot=stacks->second;
    if(auto grants=transportGrants.find(holder->UniqueID);grants!=transportGrants.end())
        for(const auto& [id,grant]:grants->second) e.stackSnapshot[id]=stackCount(holder->UniqueID,id);
    if(other) {
        e.distance=distanceCells(e.origin,other->GetCenterCoords());
        auto targetType=other->GetTechnoType();
        e.targetCost=targetType->Cost;
        auto target=UnitHealth::read(other,targetType->Strength);
        if(target.maximum>0) {e.targetMaxHp=target.maximum;e.targetHp=std::clamp(target.current,0,target.maximum);}
    }
    if(victim) {e.hasImpact=true;e.impact=victim->GetCoords();}
    e.ammo=type->Ammo<0?-1:holder->Ammo;
    e.passengers=holder->Passengers.NumPassengers;
    e.owner=holder->Owner?holder->Owner->ArrayIndex:-1;
    if(attacker && attacker->Owner) e.attackerOwner=attacker->Owner->ArrayIndex;
    if(victim && victim->Owner) e.victimOwner=victim->Owner->ArrayIndex;
    return e;
}
bool meetsEventFormula(const Skill& skill,const Event& event) {
    if(skill.conditionFormula.expression.empty()) return true;
    auto distance=event.distance>=0?std::optional<double>{event.distance}:std::nullopt;
    auto stacks=event.stackSnapshot.find(skill.id);
    HealthFormula::Values target{event.targetHp,event.targetMaxHp,event.targetCost};
    std::optional<HealthFormula::Values> sender;
    if(event.trigger=="onsignal") sender=HealthFormula::Values{event.senderHp,event.senderMaxHp,event.senderCost};
    bool missing=false;
    auto value=skill.conditionFormula.expression.evaluate(event.hp,event.maxHp,event.damage,distance,
        stacks==event.stackSnapshot.end()?0:stacks->second,target,&missing,event.selfCost,event.ammo,
        event.passengers>=0?std::optional<int>{event.passengers}:std::nullopt,sender,
        event.trigger=="onsignal"?std::optional<double>{event.signalValue}:std::nullopt);
    if(!value) {if(!missing) skill.conditionFormula.report();return false;}
    return *value!=0;
}
bool interested(TechnoClass* unit,const char* trigger,vector<string>* snapshot=nullptr) {
    if(!unit) return false;
    auto ids=boundSkills(unit->GetTechnoType()->ID);
    extraSkills(unit,&ids);
    bool any=false;
    for(const auto& id:ids) {
        auto it=skills.find(id);
        if(it==skills.end() || it->second.trigger!=trigger || suppressed(unit->UniqueID,id)) continue;
        any=true;
        if(!snapshot) break;
        snapshot->push_back(id);
    }
    return any;
}
void forgetGain(DWORD unit,const string& id) {
    auto it=gainSeen.find(unit);
    if(it==gainSeen.end()) return;
    std::erase(it->second,id);
    if(it->second.empty()) gainSeen.erase(it);
}
void observeGains(TechnoClass* unit) {
    if(!anyGainSkills || !alive(unit)) return;
    vector<string> current;
    interested(unit,"onpassivegained",&current);
    auto old=gainSeen.find(unit->UniqueID);
    if(current.empty()) {if(old!=gainSeen.end()) gainSeen.erase(old);return;}
    if(!unit->IsOnMap || unit->InLimbo) {
        // Remember continuing ownership while transported. Defer first acquisition until placement.
        if(old!=gainSeen.end()) {
            std::erase_if(old->second,[&](const string& id){return !includes(current,id);});
            if(old->second.empty()) gainSeen.erase(old);
        }
        return;
    }
    vector<string> added;
    for(const auto& id:current) if(old==gainSeen.end() || !includes(old->second,id)) added.push_back(id);
    if(!added.empty()) {
        auto e=eventSnapshot("onpassivegained",unit,nullptr,unit,unit,0);
        e.snapshot=std::move(added);pending.push_back(std::move(e));
    }
    if(old==gainSeen.end() || old->second!=current) gainSeen[unit->UniqueID]=std::move(current);
}
void tickGains() {
    if(!anyGainSkills) return;
    for(auto unit:TechnoClass::Array) {
        if(!alive(unit)) continue;
        auto owned=Research::unlocked.find(unit->Owner?unit->Owner->ArrayIndex:-1);
        GainInputs now{unit->GetTechnoType(),unit->Owner,owned==Research::unlocked.end()?0:owned->second.size(),
            unit->IsOnMap && !unit->InLimbo};
        auto [it,fresh]=gainInputs.try_emplace(unit->UniqueID,now);
        if(!fresh && it->second==now) continue;
        it->second=now;
        observeGains(unit);
    }
}
void capture(const char* event,TechnoClass* holder,TechnoClass* other,TechnoClass* attacker,TechnoClass* victim,int damage) {
    if(applyingDamage || !passiveDetonations.empty() || !alive(holder)) return;
    vector<string> ids;
    if(!interested(holder,event,&ids)) return;
    auto e=eventSnapshot(event,holder,other,attacker,victim,damage);
    e.snapshot=std::move(ids);
    pending.push_back(std::move(e));
}
// Keep a separate clock for every instance/skill. The first tick is due one
// complete interval after acquisition. Leaving the map or losing the skill resets it.
struct PeriodicClock { unsigned start; bool seen; };
std::map<TimerKey,PeriodicClock> periodic;
bool expiryScheduled=false;
unsigned nextExpiry=0;
void scheduleExpiry(unsigned frame,int remaining) {
    if(remaining<=0) return;
    unsigned end=frame+static_cast<unsigned>(remaining);
    if(!expiryScheduled || static_cast<int>(end-nextExpiry)<0) nextExpiry=end;
    expiryScheduled=true;
}
void clearGrantProgress(TechnoClass* unit,const string& id) {
    // A native or researched copy keeps its existing count and clocks.
    if(!suppressed(unit->UniqueID,id)) {
        auto ids=boundSkills(unit->GetTechnoType()->ID);
        extraSkills(unit,&ids);
        if(includes(ids,id)) return;
    }
    forgetGain(unit->UniqueID,id);
    eraseStacks(unit->UniqueID,id);
    TimerKey key{unit->UniqueID,id};
    if(auto found=counts.find(key.first);found!=counts.end()) found->second.erase(id);
    periodic.erase(key);stateTimers.erase(key);nearbySamples.erase(key);activeAnimStates.erase(key);
}
void refreshTransportGrants() {
    for(const auto& key:transportStateKeys) {
        if(auto timer=stateTimers.find(key);timer!=stateTimers.end()) timer->second.observed=false;
    }
    transportStateKeys.clear();
    if(!anyTransportPassives && transportGrants.empty()) return;
    decltype(transportGrants) next;
    for(auto carrier:UnitClass::Array) {
        if(!alive(carrier) || carrier->Passengers.NumPassengers<=0) continue;
        int remaining=carrier->Passengers.NumPassengers;
        for(ObjectClass* object=carrier->Passengers.FirstPassenger;object && remaining-->0;object=object->NextObject) {
            auto passenger=static_cast<FootClass*>(object);
            if(!alive(passenger) || passenger->Transporter!=carrier) continue;
            auto ids=boundSkills(passenger->GetTechnoType()->ID);extraSkills(passenger,&ids);
            std::map<string,bool> provided;
            for(const auto& id:ids) {
                auto skill=skills.find(id);
                if(skill==skills.end() || skill->second.transportPassives.empty() || suppressed(passenger->UniqueID,id)) continue;
                transportStateKeys.emplace(passenger->UniqueID,id);
                if(!stateActive(passenger,skill->second,stateOf(passenger),UnitHealth::read(passenger),Unsorted::CurrentFrame)) continue;
                for(const auto& grant:skill->second.transportPassives) provided[grant]=provided[grant] || skill->second.transportStack;
            }
            for(const auto& [id,stacks]:provided) {
                auto& grant=next[carrier->UniqueID][id];
                ++grant.sources;if(stacks) ++grant.layers;
            }
        }
    }
    if(next==transportGrants) return;
    transportGrants.swap(next);
    for(auto carrier:UnitClass::Array) {
        if(!carrier) continue;
        auto old=next.find(carrier->UniqueID),current=transportGrants.find(carrier->UniqueID);
        if(old==next.end() && current==transportGrants.end()) continue;
        if(old!=next.end() && current!=transportGrants.end() && old->second==current->second) continue;
        gainInputs.erase(carrier->UniqueID);
        if(old!=next.end()) for(const auto& [id,grant]:old->second) clearGrantProgress(carrier,id);
    }
}
std::set<DWORD> auraTargets(TechnoClass* emitter,const AuraRule& rule) {
    std::set<DWORD> result;
    auto origin=emitter->GetCenterCoords();
    auto owner=emitter->Owner;
    auto visit=[&](TechnoClass* target) {
        if(!alive(target) || !target->IsOnMap || target->InLimbo || !target->Owner ||
           (rule.range>0 && target==emitter && !rule.includeSelf)) return false;
        if(rule.range>0) {
            auto at=target->GetCenterCoords();
            int64_t dx=int64_t(at.X)-origin.X,dy=int64_t(at.Y)-origin.Y;
            int64_t radius=rule.range*256;
            if(dx*dx+dy*dy>radius*radius) return false;
        }
        const char* relation=target->Owner==owner?"self":owner->IsAlliedWith(target->Owner)?"allies":
            target->Owner->IsNeutral()?"neutral":"enemies";
        if((includes(rule.houses,"all") || includes(rule.houses,relation)) &&
            Research::matchesTarget(rule.types,lower(target->GetTechnoType()->ID),kind(target)))
            result.insert(target->UniqueID);
        return false;
    };
    if(rule.range==-1) {
        for(auto target:TechnoClass::Array) visit(target);
    } else {
        // Cell rounding can shift the diagonal separation by up to sqrt(2) cells.
        visitNearbyCells(CellClass::Coord2Cell(origin),rule.range+2,true,visit);
        if(rule.includeSelf) visit(emitter);
    }
    return result;
}
void removeAuraTarget(DWORD target,const vector<string>& grants) {
    auto unit=auraGrants.find(target);
    if(unit==auraGrants.end()) return;
    for(const auto& id:grants) {
        auto it=unit->second.find(id);
        if(it!=unit->second.end() && --it->second==0) {
            unit->second.erase(it);auraLosses[target].insert(id);gainInputs.erase(target);
        }
    }
    if(unit->second.empty()) auraGrants.erase(unit);
}
void updateAuraCoverage(AuraSource& source,const vector<string>& grants,std::set<DWORD> targets) {
    const bool same=source.grants==grants;
    if(same && source.targets==targets) return;
    for(auto target:source.targets) if(!same || !targets.contains(target)) removeAuraTarget(target,source.grants);
    for(auto target:targets) if(!same || !source.targets.contains(target)) {
        auto& refs=auraGrants[target];
        for(const auto& id:grants) if(refs[id]++==0) gainInputs.erase(target);
    }
    source.grants=grants;
    source.targets=std::move(targets);
}
void flushAuraLosses() {
    if(auraLosses.empty()) return;
    for(auto unit:TechnoClass::Array) if(alive(unit)) {
        auto it=auraLosses.find(unit->UniqueID);
        if(it!=auraLosses.end()) for(const auto& id:it->second) clearGrantProgress(unit,id);
    }
    auraLosses.clear();
}
void stopAuraSources(DWORD emitter,const string* passive=nullptr) {
    for(auto it=auraSources.lower_bound({emitter,{}});it!=auraSources.end() && it->first.first==emitter;) {
        if(passive && it->first.second!=*passive) {++it;continue;}
        for(auto target:it->second.targets) removeAuraTarget(target,it->second.grants);
        it=auraSources.erase(it);
    }
}
void refreshAuras() {
    if(!anyAuras && auraSources.empty()) {flushAuraLosses();return;}
    const auto frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    const bool pulse=frame%auraInterval==0;
    if(pulse || discoverAuras) {
        discoverAuras=false;
        for(auto unit:TechnoClass::Array) {
            if(!alive(unit) || !unit->IsOnMap || unit->InLimbo || !unit->Owner) continue;
            auto ids=boundSkills(unit->GetTechnoType()->ID);extraSkills(unit,&ids);
            for(const auto& id:ids) {
                auto s=skills.find(id);
                if(s!=skills.end() && s->second.aura.range && !suppressed(unit->UniqueID,id))
                    auraSources[{unit->UniqueID,id}].unit=unit;
            }
        }
    }
    std::map<std::pair<int,string>,std::set<DWORD>> globalTargets;
    for(auto it=auraSources.begin();it!=auraSources.end();) {
        auto& source=it->second;
        auto unit=source.unit;
        const auto& id=it->first.second;
        auto skill=skills.find(id);
        bool held=false;
        if(alive(unit) && unit->Owner && unit->IsOnMap && !unit->InLimbo &&
           skill!=skills.end() && skill->second.aura.range && !suppressed(unit->UniqueID,id)) {
            auto ids=boundSkills(unit->GetTechnoType()->ID);extraSkills(unit,&ids);
            held=includes(ids,id);
        }
        if(!held) {
            updateAuraCoverage(source,{},{});it=auraSources.erase(it);continue;
        }
        const auto& rule=skill->second.aura;
        if(!stateActive(unit,skill->second,stateOf(unit),UnitHealth::read(unit),frame)) {
            if(!source.grants.empty()) updateAuraCoverage(source,{},{});
        } else if(pulse || source.owner!=unit->Owner->ArrayIndex || source.grants!=rule.passives) {
            if(rule.range==-1) {
                // Identical global auras owned by the same house share one world scan per refresh.
                auto [pool,fresh]=globalTargets.try_emplace({unit->Owner->ArrayIndex,id});
                if(fresh) pool->second=auraTargets(unit,rule);
                auto targets=pool->second;
                if(!rule.includeSelf) targets.erase(unit->UniqueID);
                updateAuraCoverage(source,rule.passives,std::move(targets));
            } else updateAuraCoverage(source,rule.passives,auraTargets(unit,rule));
            source.owner=unit->Owner->ArrayIndex;
        }
        ++it;
    }
    // Resolve all source changes before clearing progress or observing gains:
    // crossing two overlapping auras must not re-trigger OnPassiveGained.
    flushAuraLosses();
}
void refreshSpawnGrants() {
    if(!anySpawnPassives && spawnGrants.empty()) return;
    decltype(spawnGrants) next;
    for(auto manager:SpawnManagerClass::Array) {
        auto parent=manager->Owner;
        if(!alive(parent) || !parent->IsOnMap || parent->InLimbo) continue;
        auto ids=boundSkills(parent->GetTechnoType()->ID);
        extraSkills(parent,&ids);
        vector<string> grants;
        for(const auto& id:ids) {
            auto it=skills.find(id);
            if(it==skills.end() || it->second.spawnPassives.empty() || suppressed(parent->UniqueID,id)) continue;
            if(!stateActive(parent,it->second,stateOf(parent),UnitHealth::read(parent),Unsorted::CurrentFrame)) continue;
            for(const auto& skill:it->second.spawnPassives) if(!includes(grants,skill)) grants.push_back(skill);
        }
        if(grants.empty()) continue;
        for(auto node:manager->SpawnedNodes) {
            auto child=node?node->Unit:nullptr;
            if(alive(child) && child->SpawnOwner==parent && child->Owner==parent->Owner)
                next.emplace(child->UniqueID,SpawnGrant{parent->UniqueID,grants});
        }
    }
    if(next==spawnGrants) return;
    spawnGrants.swap(next);
    for(auto child:AircraftClass::Array) {
        auto old=next.find(child->UniqueID),current=spawnGrants.find(child->UniqueID);
        if(old==next.end() && current==spawnGrants.end()) continue;
        if(old!=next.end() && current!=spawnGrants.end() && old->second==current->second) continue;
        if(old!=next.end()) for(const auto& id:old->second.ids) clearGrantProgress(child,id);
        if(anyGainSkills) observeGains(child);
    }
}
void expireWarheadGrants() {
    unsigned frame=Unsorted::CurrentFrame;
    if(!rebindGrants && (!expiryScheduled || static_cast<int>(frame-nextExpiry)<0)) return;
    if(rebindGrants) {
        for(auto u:TechnoClass::Array) if(u) {
            auto it=warheadGrants.find(u->UniqueID);
            if(it!=warheadGrants.end()) it->second.unit=u;
        }
        rebindGrants=false;
    }
    expiryScheduled=false;
    for(auto group=warheadGrants.begin();group!=warheadGrants.end();) {
        auto& [unit,entries]=group->second;
        if(unit) for(auto it=entries.begin();it!=entries.end();) {
            int left=it->second.remaining(frame);
            if(left==0 || !skills.contains(it->first)) {
                auto expired=it++;
                string id=expired->first;
                entries.erase(expired);clearGrantProgress(unit,id);
            } else {scheduleExpiry(frame,left);++it;}
        }
        if(!unit || entries.empty()) group=warheadGrants.erase(group); else ++group;
    }
}
bool grantToUnit(TechnoClass* target,const PassiveGrant& rule,bool notifyGains=true) {
    bool changed=false;
    unsigned frame=Unsorted::CurrentFrame;
    vector<string> held;
    if(rule.stackOnReapply) {
        held=boundSkills(target->GetTechnoType()->ID);
        extraSkills(target,&held); // Includes active temporary, research, aura and spawn grants.
    }
    auto& group=warheadGrants[target->UniqueID];
    group.unit=target;
    for(const auto& id:rule.ids) {
        if(rule.stackOnReapply && includes(held,id) && !suppressed(target->UniqueID,id)) {
            auto skill=skills.find(id);
            if(skill!=skills.end() && changeStacks(target->UniqueID,id,1,skill->second.stackMax)) changed=true;
        }
        if(!group.skills.contains(id)) clearGrantProgress(target,id);
        auto [entry,fresh]=group.skills.try_emplace(id,GrantTime{frame,rule.duration});
        if(!fresh) {
            int left=entry->second.remaining(frame);
            if(left<0 || (rule.duration>0 && left>=rule.duration)) continue;
            if(!left) clearGrantProgress(target,id);
            entry->second={frame,rule.duration};
        }
        auto type=skills.find(id);
        if(type!=skills.end() && type->second.aura.range)
            auraSources[{target->UniqueID,id}].unit=target;
        changed=true;
        scheduleExpiry(frame,rule.duration);
    }
    if(changed && notifyGains && anyGainSkills) observeGains(target);
    return changed;
}
void grantFromPreset(TechnoClass* unit,const GrantPreset& preset) {
    if(!alive(unit) || !unit->IsOnMap || unit->InLimbo || unit->IsSinking || unit->IsBeingWarpedOut()) return;
    if(!preset.targetTypes.empty() && std::find(preset.targetTypes.begin(),preset.targetTypes.end(),
        unit->GetTechnoType())==preset.targetTypes.end()) return;
    grantToUnit(unit,preset);
}
void grantFromTrigger(const GrantPreset& preset,int target,HouseClass* house,ObjectClass* object,TriggerClass* trigger) {
    if(target==-1) {
        if(object && (object->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None)
            grantFromPreset(static_cast<TechnoClass*>(object),preset);
        return;
    }
    if(target==-2) {
        if(!trigger) return;
        for(auto unit:TechnoClass::Array) if(unit && unit->AttachedTag) {
            for(auto linked=unit->AttachedTag->FirstTrigger;linked;linked=linked->NextTrigger) {
                if(linked->Type!=trigger->Type) continue;
                grantFromPreset(unit,preset);
                break;
            }
        }
        return;
    }
    if(target!=0) house=HouseClass::Index_IsMP(target)?HouseClass::FindByPlayerAt(target):nullptr;
    if(house) for(auto unit:TechnoClass::Array) if(unit && unit->Owner==house) grantFromPreset(unit,preset);
}
void tryGrantUnit(TechnoClass* unit,WarheadTypeClass* wh,const WarheadGrantRule& rule,HouseClass* source) {
    if(!alive(unit) || !unit->Owner || !unit->IsOnMap || unit->InLimbo || unit->IsSinking ||
       unit->IsIronCurtained() || unit->IsBeingWarpedOut()) return;
    auto owner=unit->Owner;
    bool self=owner==source,ally=source && source->IsAlliedWith(owner);
    if(source && !(self?rule.affectsOwner:ally?wh->AffectsAllies:rule.affectsEnemies)) return;
    if(owner->IsNeutral() && !rule.affectsNeutral) return;
    const char* house=self?"self":ally?"allies":owner->IsNeutral()?"neutral":source?"enemies":"none";
    if(!includes(rule.houses,"all") && !includes(rule.houses,house)) return;
    if(!MapClass::GetTotalDamage(10000,wh,unit->GetTechnoType()->Armor,0)) return;
    if(rule.stackOnReapply && !reapplyImpacts.empty()) {
        auto& impact=reapplyImpacts.back();
        if(impact.wh==wh && !impact.recipients.insert(unit->UniqueID).second) return;
    }
    grantToUnit(unit,rule);
}
void deliverWarheadGrants(WarheadTypeClass* wh,const CoordStruct& at,HouseClass* source,AbstractClass* aim) {
    auto config=grantRules.find(wh);
    if(config==grantRules.end()) return;
    const auto& rule=config->second;
    auto& map=MapClass::Instance;
    auto center=CellClass::Coord2Cell(at);
    if(!map.CoordinatesLegal(center)) return;
    std::set<DWORD> visited;
    auto deliver=[&](TechnoClass* unit) {
        if(unit && visited.insert(unit->UniqueID).second) tryGrantUnit(unit,wh,rule,source);
    };
    if(!(wh->CellSpread>0)) {
        if(aim && (aim->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None) {
            auto unit=static_cast<TechnoClass*>(aim);
            auto cell=map.TryGetCellAt(center);
            if(unit->GetCoords().DistanceFromSquared(at)<=64.0*64.0 ||
               (cell && unit->WhatAmI()==AbstractType::Building && cell->GetBuilding()==unit)) deliver(unit);
        }
        return;
    }
    const double radius=static_cast<double>(wh->CellSpread)*Unsorted::LeptonsPerCell;
    const int cells=static_cast<int>(std::min(512.0,std::ceil(static_cast<double>(wh->CellSpread))+1));
    for(int y=std::max(0,center.Y-cells);y<=std::min(511,center.Y+cells);++y)
        for(int x=std::max(0,center.X-cells);x<=std::min(511,center.X+cells);++x) {
            CellStruct pos{static_cast<short>(x),static_cast<short>(y)};
            auto cell=map.TryGetCellAt(pos);
            if(!cell) continue;
            // A large building can be hit on any occupied foundation cell.
            if(auto b=cell->GetBuilding(); b && !b->Type->InvisibleInGame &&
               cell->GetCenterCoords().DistanceFromSquared(at)<=radius*radius) deliver(b);
            for(auto head:{cell->FirstObject,cell->AltObject}) for(auto obj=head;obj;obj=obj->NextObject) {
                if((obj->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None &&
                   obj->WhatAmI()!=AbstractType::Building && obj->GetCoords().DistanceFromSquared(at)<=radius*radius)
                    deliver(static_cast<TechnoClass*>(obj));
            }
            if(auto jj=cell->Jumpjet; jj && jj->Location.DistanceFromSquared(at)<=radius*radius) deliver(jj);
        }
    // Avoid borrowing the engine's shared air-target iterator during a detonation.
    for(auto air:AircraftClass::Array) if(air && air->Location.DistanceFromSquared(at)<=radius*radius) deliver(air);
}
void tickIntervals() {
    if(!anyIntervals && periodic.empty()) return;
    const unsigned frame=Unsorted::CurrentFrame;
    for(auto& [key,timer]:periodic) timer.seen=false;
    vector<string> ids;
    for(auto* unit:TechnoClass::Array) {
        if(!alive(unit) || !unit->IsOnMap || unit->InLimbo) continue;
        ids.clear();
        if(!interested(unit,"oninterval",&ids)) continue;
        for(const auto& id:ids) {
            auto& timer=periodic.try_emplace({unit->UniqueID,id},PeriodicClock{frame,true}).first->second;
            timer.seen=true;
            if(frame-timer.start<static_cast<unsigned>(skills.at(id).interval)) continue;
            timer.start=frame;
            auto aim=unit->Target;
            auto other=aim && (aim->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None
                ? static_cast<TechnoClass*>(aim) : nullptr;
            auto e=eventSnapshot("oninterval",unit,other,unit,other,0);
            if(aim) {e.impact=aim->GetCoords(); e.hasImpact=true;if(!other) e.distance=distanceCells(e.origin,e.impact);}
            e.snapshot.push_back(id);
            pending.push_back(std::move(e));
        }
    }
    std::erase_if(periodic,[](const auto& item){return !item.second.seen;});
}
void observeTransitions() {
    shots.clear(); // failed firing attempts have no event
    if(!observePromotions && !observeDeployments) return;
    for(auto* u:TechnoClass::Array) {
        if(!alive(u)) continue;
        bool promo=observePromotions && (interested(u,"onpromote") || interested(u,"onpromoteveteran") || interested(u,"onpromoteelite")),deploy=observeDeployments && interested(u,"ondeploy");
        if(!promo && !deploy) { previousState.erase(u->UniqueID); continue; }
        bool deployed=deploy && deploymentOf(u)==DeployPosture::Deployed;
        int rank=u->Veterancy.IsElite()?2:u->Veterancy.IsVeteran()?1:0;
        Transition now{rank,deployed};
        auto [it,fresh]=previousState.try_emplace(u->UniqueID,now);
        if(!fresh) {
            if(promo && rank>it->second.rank) {
                capture("onpromote",u,nullptr,u,u,0);
                capture(rank==2?"onpromoteelite":"onpromoteveteran",u,nullptr,u,u,0);
            }
            if(deployed && !it->second.deployed) capture("ondeploy",u,nullptr,u,u,0);
            it->second=now;
        }
    }
}
using Objects = std::map<DWORD, TechnoClass*>;
TechnoClass* findLive(const Objects& objects,DWORD id) {
    auto it=objects.find(id);
    // Destructors invalidate entries during secondary damage.
    return it!=objects.end() && alive(it->second) ? it->second : nullptr;
}
Objects* activeObjects=nullptr;
struct EffectScope {
    bool previous=applyingDamage;
    EffectScope(){ applyingDamage=true; }
    ~EffectScope(){ applyingDamage=previous; }
};
bool weaponEffect(const PassiveAction& s,TechnoClass* firer,AbstractClass* target,const CoordStruct& destination,const CoordStruct& origin) {
    auto weapon=s.action=="fireweapon"?WeaponTypeClass::Find(s.weapon.c_str()):nullptr;
    if(s.action=="fireweapon" && !weapon) return false;
    auto warhead=weapon?weapon->Warhead:WarheadTypeClass::Find(s.warhead.c_str());
    auto projectile=weapon?weapon->Projectile:BulletTypeClass::Find("Invisible");
    if(!warhead || !projectile || !target) return false;
    EffectScope scope;
    auto bullet=projectile->CreateBullet(target,firer,weapon?weapon->Damage:s.amount,warhead,
        weapon?weapon->Speed:100,weapon?weapon->Bright:warhead->Bright);
    if(!bullet) return false;
    bullet->SetWeaponType(weapon);
    // A self-directed shot has no flight segment. Resolve the configured weapon at its impact point.
    if(!weapon || target==firer || (!firer && origin==destination)) {
        bullet->SetLocation(destination);
        bullet->Detonate(destination);
        bullet->UnInit();
        return true;
    }

    double dx=destination.X-origin.X,dy=destination.Y-origin.Y,dz=destination.Z-origin.Z;
    double length=std::sqrt(dx*dx+dy*dy+dz*dz);
    double speed=std::max(1,bullet->Speed);
    BulletVelocity velocity{};
    if(length>0) velocity={dx*speed/length,dy*speed/length,dz*speed/length};
    if(projectile->Arcing) {
        double horizontal=std::hypot(dx,dy);
        if(horizontal>0) {
            double gravity=std::max(1,RulesClass::Instance->Gravity);
            double flight=std::sqrt(horizontal/gravity)*(weapon->Lobber?2.0:1.0);
            velocity={dx/flight,dy/flight,dz/flight+0.5*gravity*flight};
        }
    }
    passiveBullets.insert(bullet->UniqueID);
    if(!bullet->MoveTo(origin,velocity)) {bullet->UnInit();return false;}
    return true;
}
HouseClass* actionOwner(const string& source,const Event& e,TechnoClass* holder,const Objects& objects) {
    if(source=="neutral") return HouseClass::FindNeutral();
    if(source.starts_with("player")) return HouseClass::FindByPlayerAt(HouseClass::PlayerAtA+source.back()-'1');
    DWORD id=source=="attacker"?e.attacker:source=="victim"?e.victim:e.holder;
    auto live=id==e.holder?holder:findLive(objects,id);
    if(live) return live->Owner;
    int saved=source=="attacker"?e.attackerOwner:source=="victim"?e.victimOwner:e.owner;
    return saved>=0 && saved<HouseClass::Array.Count ? HouseClass::Array.Items[saved] : nullptr;
}
// Newer Phobos builds expose a supported conversion entry that also refreshes
// their own per-instance data. Older builds cannot safely be synchronized.
using PhobosConvertFn=HRESULT(__stdcall*)(FootClass*,TechnoTypeClass*);
PhobosConvertFn publicPhobosConverter() {
    auto module=GetModuleHandleW(L"Phobos.dll");
    if(!module) return nullptr;
    for(auto name:{"ConvertToType_Phobos","_ConvertToType_Phobos@8","ConvertToType_Phobos@8"})
        if(auto entry=GetProcAddress(module,name)) return reinterpret_cast<PhobosConvertFn>(entry);
    return nullptr;
}
bool conversionAvailable() {
    if(!GetModuleHandleW(L"Phobos.dll") || publicPhobosConverter()) return true;
    static bool reported=false;
    if(!reported) {
        std::ofstream("Starfall.log",std::ios::app)<<"Starfall: unit conversion requires a Phobos build exposing ConvertToType_Phobos\n";
        reported=true;
    }
    return false;
}
using AresPermaMindFn=bool(__thiscall*)(void*,HouseClass*,AbstractClass*);
std::array<AresPermaMindFn,2> aresMindOriginal{};
bool __fastcall aresMindGuard0(void* self,void*,HouseClass* house,AbstractClass* target) {
    if(target && (target->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None &&
       mindImmuneUnits.contains(static_cast<TechnoClass*>(target)->UniqueID)) return true;
    return aresMindOriginal[0](self,house,target);
}
bool __fastcall aresMindGuard1(void* self,void*,HouseClass* house,AbstractClass* target) {
    if(target && (target->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None &&
       mindImmuneUnits.contains(static_cast<TechnoClass*>(target)->UniqueID)) return true;
    return aresMindOriginal[1](self,house,target);
}
void installAresMindGuards() {
    static bool installed=false,reported=false;
    if(installed) return;
    auto failure=[&] {
        if(!reported) {std::ofstream("Starfall.log",std::ios::app)<<"Starfall: unsupported Ares permanent mind-control guard\n";reported=true;}
    };
    auto base=reinterpret_cast<BYTE*>(GetModuleHandleW(L"Ares.dll"));
    if(!base) return;
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) return;
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const BYTE entry[]={0x80,0x79,0x08,0x00,0x74,0x23};
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp!=0x61daa114 ||
       nt->OptionalHeader.SizeOfImage!=0xdb000 || memcmp(base+0x53980,entry,sizeof(entry))) {
        failure();
        return;
    }
    BYTE* calls[]={base+0x5450A,base+0x727E3};
    void* guards[]={reinterpret_cast<void*>(&aresMindGuard0),reinterpret_cast<void*>(&aresMindGuard1)};
    BYTE original[2][5]{};
    for(int i=0;i<2;++i) {
        if(calls[i][0]!=0xE8) {failure();return;}
        memcpy(original[i],calls[i],5);
        int32_t displacement;memcpy(&displacement,calls[i]+1,4);
        auto destination=reinterpret_cast<uintptr_t>(calls[i]+5)+displacement;
        HMODULE owner=nullptr;
        if(destination!=reinterpret_cast<uintptr_t>(base+0x53980) &&
           (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
               reinterpret_cast<LPCWSTR>(destination),&owner) || owner!=GetModuleHandleW(L"Phobos.dll"))) {failure();return;}
        aresMindOriginal[i]=reinterpret_cast<AresPermaMindFn>(destination);
    }
    for(int i=0;i<2;++i) {
        DWORD old=0;
        if(!VirtualProtect(calls[i],5,PAGE_EXECUTE_READWRITE,&old)) {
            if(i) {DWORD protect=0;if(VirtualProtect(calls[0],5,PAGE_EXECUTE_READWRITE,&protect)) {
                memcpy(calls[0],original[0],5);FlushInstructionCache(GetCurrentProcess(),calls[0],5);
                VirtualProtect(calls[0],5,protect,&protect);
            }}
            failure();return;
        }
        auto relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(guards[i])-reinterpret_cast<uintptr_t>(calls[i]+5));
        memcpy(calls[i]+1,&relative,4);FlushInstructionCache(GetCurrentProcess(),calls[i],5);
        VirtualProtect(calls[i],5,old,&old);
    }
    installed=true;
}
bool nativeConversionTypesSafe(const TechnoTypeClass* from,const TechnoTypeClass* to) {
    if(from->Spawns || to->Spawns || from->Enslaves || to->Enslaves) return false;
    for(int i=0;i<TechnoTypeClass::MaxWeapons;++i) for(auto* weapon:{to->Weapon[i].WeaponType,to->EliteWeapon[i].WeaponType})
        if(weapon && weapon->Warhead && (weapon->Warhead->MindControl || weapon->Warhead->Parasite || weapon->Warhead->Temporal)) return false;
    return true;
}
bool canTransformUnit(TechnoClass* target,TechnoTypeClass* to) {
    if(!target || !to || !conversionAvailable()) return false;
    auto from=target->GetTechnoType();
    if(!from || !target->IsOnMap || target->InLimbo || from==to || from->WhatAmI()!=to->WhatAmI()
        || !target->Owner || !alive(target) || from->Strength<=0 || to->Strength<=0 || !IsEqualGUID(from->Locomotor,to->Locomotor)) return false;
    if(target->WhatAmI()!=AbstractType::Infantry && target->WhatAmI()!=AbstractType::Unit && target->WhatAmI()!=AbstractType::Aircraft) return false;
    if(!GetModuleHandleW(L"Phobos.dll")) {
        auto foot=static_cast<FootClass*>(target);
        if(!nativeConversionTypesSafe(from,to) || target->SpawnManager || target->SlaveManager || target->CaptureManager ||
            target->Airstrike || target->TemporalTargetingMe || target->IsMindControlled() || target->MindControlledByHouse ||
            foot->ParasiteEatingMe || foot->ParasiteImUsing || foot->IsDeploying || target->Absorbed ||
            (target->Passengers.NumPassengers && (from->Gunner!=to->Gunner || from->OpenTopped!=to->OpenTopped))) return false;
    }
    if(target->Passengers.NumPassengers>0) {
        if(target->Passengers.GetTotalSize()>to->Passengers) return false;
        for(auto* p=target->Passengers.FirstPassenger;p;p=static_cast<FootClass*>(p->NextObject))
            if(p->GetTechnoType()->Size>to->SizeLimit) return false;
    }
    if(from->SpeedType!=to->SpeedType && !target->IsInAir()) {
        auto cell=target->GetCell();
        if(!cell || to->SpeedType<SpeedType::Foot || to->SpeedType>SpeedType::FloatBeach) return false;
        auto land=target->OnBridge?LandType::Clear:cell->LandType;
        if(land<LandType::Clear || static_cast<size_t>(land)>=std::size(GroundType::Array)
            || GroundType::Array[static_cast<int>(land)].Cost[static_cast<int>(to->SpeedType)]<=0) return false;
    }
    return true;
}
bool transformUnit(TechnoClass* target,TechnoTypeClass* to) {
    if(!canTransformUnit(target,to)) return false;
    if(auto fn=publicPhobosConverter()) return SUCCEEDED(fn(static_cast<FootClass*>(target),to));
    if(GetModuleHandleW(L"Phobos.dll")) return false;

    auto from=target->GetTechnoType();
    auto owner=target->Owner;
    auto hp=UnitHealth::read(target,from->Strength);
    const int maxHp=static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(to->Strength)+hp.maximum-from->Strength,1,INT_MAX));
    const int current=static_cast<int>(std::clamp<int64_t>((static_cast<int64_t>(hp.current)*maxHp+hp.maximum/2)/hp.maximum,1,maxHp));
    auto temporal=target->TemporalImUsing;
    if(temporal && temporal->Target) temporal->LetGo();
    owner->RegisterLoss(target,false);
    owner->RemoveTracking(target);
    switch(target->WhatAmI()) {
        case AbstractType::Infantry: static_cast<InfantryClass*>(target)->Type=static_cast<InfantryTypeClass*>(to);break;
        case AbstractType::Unit: static_cast<UnitClass*>(target)->Type=static_cast<UnitTypeClass*>(to);break;
        case AbstractType::Aircraft: static_cast<AircraftClass*>(target)->Type=static_cast<AircraftTypeClass*>(to);break;
        default: return false; // Callers validate supported types before changing bookkeeping.
    }
    if(maxHp==to->Strength) {
        UnitHealth::pools.erase(target->UniqueID);
        target->Health=target->EstimatedHealth=current;
    } else {
        auto& pool=UnitHealth::pools[target->UniqueID];
        pool.base=to->Strength;pool.maximum=maxHp;pool.set(target,current);
    }
    if(to->Ammo<0) target->Ammo=-1;
    else target->Ammo=target->Ammo<0?to->Ammo:std::min(target->Ammo,to->Ammo);
    if(target->WhatAmI()==AbstractType::Aircraft) target->SecondaryFacing.SetROT(to->ROT);
    else target->PrimaryFacing.SetROT(to->ROT);
    owner->AddTracking(target);
    owner->RegisterGain(target,false);
    owner->RecheckTechTree=true;
    target->MarkForRedraw();
    return true;
}
bool spawnUnit(TechnoTypeClass* type,HouseClass* owner,const CoordStruct& location) {
    auto& map=MapClass::Instance;
    const auto center=CellClass::Coord2Cell(location);
    if(!map.IsWithinUsableArea(center,false)) return false;
    auto speed=type->WhatAmI()==AbstractType::AircraftType ? SpeedType::Wheel : type->SpeedType;
    if(speed<SpeedType::Foot || speed>SpeedType::FloatBeach) return false;
    bool canUseBridge=GroundType::Array[static_cast<int>(LandType::Clear)].Cost[static_cast<int>(speed)]>0;
    for(int r=0;r<=3;++r) {
        for(int dy=-r;dy<=r;++dy) for(int dx=-r;dx<=r;++dx) {
            if(std::max(std::abs(dx),std::abs(dy))!=r) continue;
            CellStruct pos{static_cast<short>(center.X+dx),static_cast<short>(center.Y+dy)};
            if(!map.IsWithinUsableArea(pos,false)) continue;
            auto cell=map.TryGetCellAt(pos);
            if(!cell || cell->GetBuilding() || cell->GetContent()) continue;
            bool bridge=cell->ContainsBridge();
            if((bridge && !canUseBridge) || !cell->IsClearToMove(speed,false,false,-1,MovementZone::Normal,-1,bridge)) continue;
            auto unit=static_cast<FootClass*>(type->CreateObject(owner));
            if(!unit) return false;
            unit->OnBridge=bridge;
            if(!unit->Unlimbo(cell->GetCoordsWithBridge(),DirType::North)) {
                unit->UnInit();
                continue;
            }
            unit->QueueMission(Mission::Guard,false);
            owner->RecheckTechTree=true;
            return true;
        }
    }
    return false;
}
// Validate the whole selected subtree before removing anything. Parents are
// recorded before their loaded children so reversing the list empties them inside out.
bool collectCargoTransports(TechnoClass* carrier,FootClass* root,vector<FootClass*>& transports) {
    if(!alive(root) || root==carrier || !root->GetTechnoType() || root->ParasiteEatingMe) return false;
    if(!root->Passengers.NumPassengers && !root->Passengers.FirstPassenger) return true;
    std::set<TechnoClass*> seen{carrier,root};
    transports.push_back(root);
    for(size_t i=0;i<transports.size();++i) {
        auto* parent=transports[i];
        int remaining=parent->Passengers.NumPassengers;
        for(ObjectClass* object=parent->Passengers.FirstPassenger;object;object=object->NextObject) {
            if(remaining<=0 || (object->AbstractFlags & AbstractFlags::Foot)==AbstractFlags::None) return false;
            --remaining;
            auto child=static_cast<FootClass*>(object);
            if(!seen.insert(child).second || !alive(child) || !child->GetTechnoType() || child->ParasiteEatingMe ||
               (child->Transporter && child->Transporter!=parent)) return false;
            if(child->Passengers.NumPassengers || child->Passengers.FirstPassenger) transports.push_back(child);
        }
        if(remaining) return false;
    }
    return true;
}
void disposePassenger(TechnoClass* carrier,FootClass* passenger) {
    if(carrier->GetTechnoType()->OpenTopped) carrier->ExitedOpenTopped(passenger);
    passenger->Transporter=nullptr;
    passenger->RegisterDestruction(nullptr);
    passenger->UnInit();
}
bool consumePassenger(TechnoClass* carrier,Event& actionEvent) {
    if(!carrier || carrier->Passengers.NumPassengers<=0) return false;
    auto passenger=carrier->Passengers.GetFirstPassenger();
    vector<FootClass*> transports;
    if(!collectCargoTransports(carrier,passenger,transports)) return false;
    auto hp=UnitHealth::read(passenger);
    auto passengerId=passenger->UniqueID;
    auto passengerCost=passenger->GetTechnoType()->Cost;
    actionEvent.consumedKind=kind(passenger);
    actionEvent.consumedOwner=passenger->Owner?passenger->Owner->ArrayIndex:-1;
    actionEvent.consumedPosition=carrier->GetCenterCoords();
    auto held=boundSkills(passenger->GetTechnoType()->ID);
    extraSkills(passenger,&held);
    auto remaining=std::make_shared<PassengerPassives>();
    for(const auto& id:held) if(skills.contains(id) && !suppressed(passengerId,id))
        remaining->emplace(id,stackCount(passengerId,id));
    vector<string> ids;
    std::optional<Event> consumed;
    if(interested(carrier,"onpassengerconsumed",&ids)) {
        consumed=eventSnapshot("onpassengerconsumed",carrier,passenger,carrier,passenger,0);
        consumed->snapshot=std::move(ids);
        consumed->distance=0.0;consumed->hasImpact=true;consumed->impact=carrier->GetCoords();
    }
    for(auto it=transports.rbegin();it!=transports.rend();++it) {
        auto parent=*it;
        while(auto child=parent->RemoveFirstPassenger()) disposePassenger(parent,child);
    }
    FootClass* removed=nullptr;
    if((carrier->AbstractFlags&AbstractFlags::Foot)!=AbstractFlags::None)
        removed=static_cast<FootClass*>(carrier)->RemoveFirstPassenger();
    else removed=carrier->Passengers.RemoveFirstPassenger();
    if(removed!=passenger) return false;
    disposePassenger(carrier,passenger);
    // Release layers supplied by the departing passenger before checking the carrier's stack cap.
    if(transportGrants.contains(carrier->UniqueID)) refreshTransportGrants();
    if(carrier->Owner) carrier->Owner->RecheckPower=true;
    carrier->MarkForRedraw();
    actionEvent.targetHp=std::clamp(hp.current,0,hp.maximum);
    actionEvent.targetMaxHp=hp.maximum;
    actionEvent.targetCost=passengerCost;
    actionEvent.consumedTarget=passengerId;
    actionEvent.passengerPassives=remaining;
    if(consumed) {
        consumed->consumedTarget=passengerId;
        consumed->passengerPassives=std::move(remaining);
        consumed->passengers=carrier->Passengers.NumPassengers;
        pending.push_back(std::move(*consumed));
    }
    return true;
}
bool ownsPassive(TechnoClass* unit,const string& id) {
    if(!skills.contains(id) || suppressed(unit->UniqueID,id)) return false;
    auto ids=boundSkills(unit->GetTechnoType()->ID);
    extraSkills(unit,&ids);
    return includes(ids,id);
}
void removeHeldPassive(TechnoClass* unit,const string& id) {
    auto uid=unit->UniqueID;
    if(auto granted=warheadGrants.find(uid);granted!=warheadGrants.end()) {
        granted->second.skills.erase(id);
        if(granted->second.skills.empty()) warheadGrants.erase(granted);
    }
    stopAuraSources(uid,&id);
    removedPassives[uid].insert(id);
    clearGrantProgress(unit,id);
}
bool stealPassives(TechnoClass* holder,TechnoClass* donor,const PassiveAction& action,PassengerPassives* passenger=nullptr) {
    if(!alive(holder) || (!donor && !passenger) || holder==donor) return false;
    reconcileUnitStacks(donor);
    reconcileUnitStacks(holder);
    bool changed=false;
    for(const auto& id:action.passiveList) {
        int available;
        if(passenger) {
            auto saved=passenger->find(id);
            if(saved==passenger->end() || !skills.contains(id)) continue;
            available=saved->second;
        } else {
            if(!ownsPassive(donor,id)) continue;
            available=stackCount(donor->UniqueID,id);
        }
        int maximum=skills.at(id).stackMax;
        int room=maximum-stackCount(holder->UniqueID,id);
        int moved=std::min({available,action.amount,room});
        // A zero-stack passive still conveys its base ability once.
        if(!moved && (available>0 || ownsPassive(holder,id))) continue;
        grantToUnit(holder,PassiveGrant{{id},-1},false);
        if(moved) changeStacks(holder->UniqueID,id,moved,maximum);
        if(passenger) {
            if(moved==available) passenger->erase(id);
            else passenger->at(id)-=moved;
        } else if(moved==available) removeHeldPassive(donor,id);
        else changeStacks(donor->UniqueID,id,-moved,maximum);
        changed=true;
    }
    // Acquisition formulas see all transferred stacks, including on the first grant.
    if(changed && anyGainSkills) observeGains(holder);
    return changed;
}
bool apply(const PassiveAction& s, Event& e, TechnoClass* holder, const Objects& objects,int stacks) {
    DWORD targetId=s.targetIsEventTarget && e.consumedTarget ? e.consumedTarget :
        s.target=="sender"?e.attacker:s.target=="attacker"?e.attacker:s.target=="victim"?e.victim:e.holder;
    auto target=targetId==e.holder ? holder : findLive(objects,targetId);
    auto eventDistance=e.distance>=0?std::optional<double>{e.distance}:std::nullopt;
    HealthFormula::Values targetValues{e.targetHp,e.targetMaxHp,e.targetCost};
    std::optional<HealthFormula::Values> senderValues;
    if(e.trigger=="onsignal") senderValues=HealthFormula::Values{e.senderHp,e.senderMaxHp,e.senderCost};
    auto signalValue=e.trigger=="onsignal"?std::optional<double>{e.signalValue}:std::nullopt;
    if(s.action=="sendsignal") {
        unsigned depth=e.signalChain?e.signalDepth+1:1;
        if(depth>16) {configLog("signal chain depth limit reached");return false;}
        auto chain=e.signalChain?e.signalChain:std::make_shared<SignalChain>(SignalChain{nextSignalChain++,1024});
        if(!chain->remaining) {configLog("signal chain delivery limit reached");return false;}
        double value=s.signalValue;
        if(!s.signalFormula.expression.empty()) {
            bool missing=false;
            auto result=s.signalFormula.expression.evaluate(e.hp,e.maxHp,e.damage,eventDistance,stacks,targetValues,&missing,e.selfCost,e.ammo,
                e.passengers>=0?std::optional<int>{e.passengers}:std::nullopt,senderValues,signalValue);
            if(!result || !std::isfinite(*result)) {if(!missing) s.signalFormula.report();return false;}
            value=*result;
        }
        auto senderHouse=holder?holder->Owner:(e.owner>=0 && e.owner<HouseClass::Array.Count?HouseClass::Array.Items[e.owner]:nullptr);
        if(!senderHouse || (!target && !(s.target=="self" && !holder && s.signalRange!=0))) return false;
        CoordStruct center=target?target->GetCenterCoords():e.origin;
        auto senderHealth=holder?UnitHealth::read(holder):UnitHealth::Values{e.hp,e.maxHp};
        auto senderCost=holder?std::optional<int>{holder->GetTechnoType()->Cost}:e.selfCost;
        DWORD payloadId=0;
        if(s.signalTarget=="self") payloadId=e.holder;
        else if(s.signalTarget=="sender") payloadId=e.attacker;
        else if(s.signalTarget=="attacker") payloadId=e.attacker;
        else if(s.signalTarget=="victim") payloadId=e.victim;
        else if(s.signalTarget=="target") payloadId=e.consumedTarget?e.consumedTarget:
            (e.trigger=="ontakedamage" || e.trigger=="onbeforetakedamage" || e.trigger=="ondeath")?e.attacker:e.victim;
        auto payload=findLive(objects,payloadId);
        vector<TechnoClass*> receivers;
        for(auto unit:TechnoClass::Array) {
            if(!alive(unit) || !unit->IsOnMap || unit->InLimbo || !unit->Owner) continue;
            if(s.signalRange==0 && unit!=target) continue;
            if(s.signalRange!=0 && unit->UniqueID==e.holder && !s.signalIncludeSelf) continue;
            if(s.signalRange>0 && distanceCells(center,unit->GetCenterCoords())>s.signalRange) continue;
            const char* house=unit->Owner==senderHouse?"self":senderHouse->IsAlliedWith(unit->Owner)?"allies":unit->Owner->IsNeutral()?"neutral":"enemies";
            if(!includes(s.signalHouses,"all") && !includes(s.signalHouses,house)) continue;
            if(interested(unit,"onsignal")) receivers.push_back(unit);
        }
        std::sort(receivers.begin(),receivers.end(),[](auto a,auto b){return a->UniqueID<b->UniqueID;});
        bool sent=false;
        for(auto unit:receivers) {
            vector<string> ids;interested(unit,"onsignal",&ids);
            std::erase_if(ids,[&](const auto& id){return skills.at(id).signal!=s.signal;});
            if(ids.empty()) continue;
            if(!chain->remaining) {configLog("signal chain delivery limit reached");break;}
            auto message=eventSnapshot("onsignal",unit,payload,holder,payload,0);
            message.attacker=e.holder;message.victim=payloadId;
            message.attackerOwner=senderHouse->ArrayIndex;
            message.signal=s.signal;message.signalValue=value;message.signalDepth=depth;message.signalChain=chain;
            message.senderHp=senderHealth.current;message.senderMaxHp=senderHealth.maximum;message.senderCost=senderCost;
            message.signalSenderPosition=holder?holder->GetCenterCoords():e.origin;
            message.senderKind=holder?kind(holder):e.senderKind.empty()?"other":e.senderKind;
            message.senderRelation=unit->Owner==senderHouse?"self":unit->Owner->IsAlliedWith(senderHouse)?"allies":senderHouse->IsNeutral()?"neutral":"enemies";
            if(!payload && payloadId) {
                if(payloadId==e.holder) {
                    message.targetHp=e.hp;message.targetMaxHp=e.maxHp;message.targetCost=e.selfCost;message.kind=message.senderKind;
                    message.relation=message.senderRelation;message.victimOwner=e.owner;message.distance=distanceCells(unit->GetCenterCoords(),e.origin);
                }
                else if(payloadId==e.victim || payloadId==e.attacker || payloadId==e.consumedTarget) {
                    const bool consumed=payloadId==e.consumedTarget && !e.consumedKind.empty();
                    const bool originalSender=e.trigger=="onsignal" && payloadId==e.attacker && !consumed;
                    message.targetHp=originalSender?e.senderHp:e.targetHp;message.targetMaxHp=originalSender?e.senderMaxHp:e.targetMaxHp;
                    message.targetCost=originalSender?e.senderCost:e.targetCost;message.kind=consumed?e.consumedKind:originalSender?e.senderKind:e.kind;
                    int owner=consumed?e.consumedOwner:payloadId==e.victim?e.victimOwner:e.attackerOwner;
                    message.victimOwner=owner;
                    auto other=owner>=0 && owner<HouseClass::Array.Count?HouseClass::Array.Items[owner]:nullptr;
                    message.relation=!other?"none":other==unit->Owner?"self":unit->Owner->IsAlliedWith(other)?"allies":other->IsNeutral()?"neutral":"enemies";
                    if(consumed) message.distance=distanceCells(unit->GetCenterCoords(),e.consumedPosition);
                    else if(originalSender) message.distance=distanceCells(unit->GetCenterCoords(),e.signalSenderPosition);
                    else if(e.hasImpact && payloadId==e.victim) message.distance=distanceCells(unit->GetCenterCoords(),e.impact);
                }
            }
            message.snapshot=std::move(ids);--chain->remaining;pending.push_back(std::move(message));sent=true;
        }
        return sent;
    }
    if(s.action=="stealpassive") {
        if(e.consumedTarget && targetId==e.consumedTarget)
            return stealPassives(holder,nullptr,s,e.passengerPassives.get());
        return stealPassives(holder,target,s);
    }
    if(s.action=="fireweapon" || s.action=="detonatewarhead" || s.action=="spawncrate" || s.action=="spawnunit") {
        if(e.trigger=="onsignal" && s.target=="victim" && !target) return false;
        auto origin=holder?holder->GetCenterCoords():e.origin;
        CoordStruct destination;
        AbstractClass* aim=target;
        if(s.target=="victimlocation" || (!target && s.target=="victim")) {
            if(!e.hasImpact) return false;
            destination=e.impact;
            aim=nullptr;
        } else if(!holder && s.target=="self") {
            destination=origin;
        } else if(target) {
            destination=target->GetCenterCoords();
        } else return false;
        auto& map=MapClass::Instance;
        if(s.action=="spawnunit") {
            auto owner=actionOwner(s.owner,e,holder,objects);
            return owner && !owner->Defeated && !owner->IsObserver() && spawnUnit(s.unitType,owner,destination);
        }
        if(s.action=="spawncrate") {
            auto cell=CellClass::Coord2Cell(destination);
            return map.IsWithinUsableArea(cell,false) && map.PlacePowerupCrate(cell,s.crateType);
        }
        if(!aim) aim=map.TryGetCellAt(destination);
        return weaponEffect(s,holder,aim,destination,origin);
    }
    auto amountValue=[&]() -> int {
        if(!s.amountFormula.expression.empty()) {
            bool missing=false;
            auto value=s.amountFormula.expression.evaluate(e.hp,e.maxHp,e.damage,eventDistance,stacks,targetValues,&missing,e.selfCost,e.ammo,e.passengers>=0?std::optional<int>{e.passengers}:std::nullopt,senderValues,signalValue);
            if(!value || *value<0 || *value>INT_MAX) {if(!missing) s.amountFormula.report();return 0;}
            return static_cast<int>(*value);
        }
        return s.amount?s.amount:static_cast<int>(std::min<long long>(INT_MAX,1LL*e.damage*s.damagePercent/100));
    };
    if(s.action=="givecredits" || s.action=="giveresearchpoints") {
        auto house=target?target->Owner:nullptr;
        if(!house && !holder && s.target=="self" && e.owner>=0 && e.owner<HouseClass::Array.Count)
            house=HouseClass::Array.Items[e.owner];
        int amount=amountValue();
        if(!house || amount<=0) return false;
        if(s.action=="giveresearchpoints") {
            int before=Research::balance(house->ArrayIndex);
            Research::award(house->ArrayIndex,amount);
            return Research::balance(house->ArrayIndex)>before;
        }
        house->GiveMoney(amount);return true;
    }
    if (!target) return false;
    if(s.action=="consumepassenger") return consumePassenger(target,e);
    if(s.percentIndex<percentActions.size()) {
        const auto k=s.percentIndex;
        auto id=target->UniqueID;
        double delta=s.percentDelta;
        if(!s.formula.expression.empty()) {
            bool missing=false;
            auto value=s.formula.expression.evaluate(e.hp,e.maxHp,e.damage,eventDistance,stacks,targetValues,&missing,e.selfCost,e.ammo,e.passengers>=0?std::optional<int>{e.passengers}:std::nullopt,senderValues,signalValue);
            if(!value) {if(!missing) s.formula.report();return false;}
            delta=*value*100.0;
            if(!std::isfinite(delta)) {s.formula.report();return false;}
            if(delta==0) return false;
        }
        auto changeStored=[&](auto& values) {
            auto it=values.find(id);
            double current=it==values.end()?100:static_cast<double>(it->second);
            double next=percentLimits[k].clamp(current+delta);
            if(next==current) return false;
            if(next==100) values.erase(id);
            else values[id]=static_cast<typename std::decay_t<decltype(values)>::mapped_type>(next);
            return true;
        };
        if(k==projectileScaleIndex) return changeStored(projectileBase);
        if(k==weaponRangeIndex) return changeStored(rangeBase);
        if(k==fireRateIndex) return changeStored(fireRateBase);
        if(k==scaleIndex) {
            if(target->WhatAmI()==AbstractType::Building || !changeStored(VisualScale::base)) return false;
            target->MarkForRedraw();return true;
        }
        auto field=statFields(target)[k];
        if(!field) return false;
        auto it=applied.find(id);
        double current=it==applied.end()?100:it->second.percent[k];
        double next=percentLimits[k].clamp(current+delta);
        if(next==current) return false;
        auto& record=applied[id];
        record.percent[k]=next;
        updateStat(record,k,*field);
        if(record.state==identity && record.percent==defaultStatPercent) applied.erase(id);
        return true;
    }
    if(s.action=="changemaxhealth") {
        int amount=s.amount;
        if(s.maxHealthPercent) {
            auto maximum=UnitHealth::read(target).maximum;
            amount=static_cast<int>(std::clamp<int64_t>((int64_t(maximum)*s.maxHealthPercent+50)/100,1,INT_MAX));
        }
        return UnitHealth::change(target,amount);
    }
    if(s.action=="transform") return transformUnit(target,s.unitType);
    if(s.action=="changeowner") {
        auto owner=actionOwner(s.owner,e,holder,objects);
        if(!owner || owner->Defeated || owner->IsObserver() || owner==target->Owner || target->IsMindControlled()
            || target->MindControlledByHouse) return false;
        return target->SetOwningHouse(owner,false);
    }
    if(s.action=="changeammo") {
        int max=target->GetTechnoType()->Ammo;
        if(max<=0 || target->Ammo<0) return false;
        int next=static_cast<int>(std::clamp<long long>(1LL*target->Ammo+s.amount,0,max));
        if(next==target->Ammo) return false;
        target->Ammo=next;target->MarkForRedraw();return true;
    }
    if(s.action=="selfdestruct") {
        int damage=UnitHealth::read(holder).current;
        auto id=holder->UniqueID;
        EffectScope scope;
        holder->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,nullptr,true,false,nullptr);
        return !findLive(objects,id);
    }
    if(s.action=="grantpassive") return grantToUnit(target,PassiveGrant{{s.passive},s.duration,s.stackOnReapply});
    if(s.action=="changepassivestack") {
        reconcileUnitStacks(target);
        if(!ownsPassive(target,s.passive)) return false;
        return changeStacks(target->UniqueID,s.passive,s.amount,skills.at(s.passive).stackMax);
    }
    if(s.action=="removepassive") {
        bool changed=false;
        for(const auto& id:s.passiveList) if(ownsPassive(target,id)) {
            removeHeldPassive(target,id);
            changed=true;
        }
        return changed;
    }
    int amount=amountValue();
    if (!amount) return false;
    if(s.action=="heal") {
        auto hp=UnitHealth::read(target);
        int gain=std::min(amount,hp.maximum-hp.current);
        if(gain<=0) return false;
        UnitHealth::heal(target,hp.current+gain);
        target->MarkForRedraw();
    } else {
        int before=UnitHealth::read(target).current;
        DWORD id=target->UniqueID;
        applyingDamage=true;
        target->ReceiveDamage(&amount,0,RulesClass::Instance->C4Warhead,holder,false,false,holder?holder->Owner:nullptr);
        applyingDamage=false;
        auto after=findLive(objects,id);
        return !after || UnitHealth::read(after).current<before;
    }
    return true;
}
bool playEventAnim(const Skill& s,const Event& e,TechnoClass* holder,const Objects& objects) {
    auto owner=holder?holder->Owner:e.owner>=0 && e.owner<HouseClass::Array.Count?HouseClass::Array.Items[e.owner]:nullptr;
    if(s.animTarget=="self" || e.trigger=="ondeploy" || e.trigger=="onpromote" || e.trigger=="onpromoteveteran" || e.trigger=="onpromoteelite" || e.trigger=="onpassivegained")
        return playSkillAnim(s.triggerAnim,holder,e.origin,owner)!=nullptr;
    bool attacker=e.trigger=="ontakedamage" || e.trigger=="ondeath";
    if(auto unit=findLive(objects,attacker?e.attacker:e.victim))
        return playSkillAnim(s.triggerAnim,unit,unit->Location,owner)!=nullptr;
    return !attacker && e.hasImpact && playSkillAnim(s.triggerAnim,nullptr,e.impact,owner);
}
void dispatch() {
    if(pending.empty()) return;
    Objects objects;
    for(auto* t : TechnoClass::Array) if(alive(t)) objects.emplace(t->UniqueID,t);
    activeObjects=&objects;
    auto events=std::move(pending);
    for(auto& e:events) {
        auto holder=findLive(objects,e.holder);
        bool death=e.trigger=="ondeath";
        if(death) holder=nullptr;
        if(!holder && !death) continue;
        auto active=e.snapshot;
        if(active.empty()) {
            if(!holder) continue;
            active=boundSkills(holder->GetTechnoType()->ID);extraSkills(holder,&active);
        }
        for(const auto& id:active) {
            auto it=skills.find(id);
            if(it==skills.end()) continue;
            const Skill& s=it->second;
            if(e.trigger=="onsignal") {
                vector<string> held;interested(holder,"onsignal",&held);
                if(!includes(held,id) || s.signal!=e.signal ||
                   (!includes(s.senderHouses,"all") && !includes(s.senderHouses,e.senderRelation)) ||
                   (!includes(s.senderKinds,"all") && !includes(s.senderKinds,e.senderKind))) continue;
            }
            if(suppressed(e.holder,id) || s.trigger!=e.trigger || e.damage<s.minDamage || !meetsState(s,e.state,e.hp,e.maxHp,e.ammo,e.passengers) || !meetsEventFormula(s,e) ||
               (!includes(s.houses,"all") && !includes(s.houses,e.relation)) ||
               (!includes(s.kinds,"all") && !includes(s.kinds,e.kind))) continue;
            if(s.maxTriggers>0) {
                auto unit=triggerTotals.find(e.holder);
                if(unit!=triggerTotals.end()) {
                    auto total=unit->second.find(id);
                    if(total!=unit->second.end() && total->second>=static_cast<unsigned>(s.maxTriggers)) continue;
                }
            }
            auto& p=counts[e.holder][id];
            unsigned frame=Unsorted::CurrentFrame;
            if(frame<p.next || ++p.count<s.count) continue;
            p.count=0;
            if(s.chance==0 || (s.chance<100 && ScenarioClass::Instance->Random.RandomRanged(1,100)>s.chance)) continue;
            bool played=s.triggerAnim && playEventAnim(s,e,holder,objects);
            bool success=s.actions.empty() && played;
            Event actionEvent=e;
            for(const auto& action:s.actions) {
                // Keep the last live position if this action destroys the holder.
                if(holder && s.actions.size()>1) actionEvent.origin=holder->GetCenterCoords();
                auto stack=e.stackSnapshot.find(id);
                bool changed=apply(action,actionEvent,holder,objects,stack==e.stackSnapshot.end()?0:stack->second);
                if(changed) success=true;
                if(!death) holder=findLive(objects,e.holder);
                if(action.action=="consumepassenger" && !changed) break;
            }
            if(success && s.maxTriggers>0) ++triggerTotals[e.holder][id];
            if(death) continue;
            if(!holder) break;
            if(success && !suppressed(e.holder,id)) counts[e.holder][id].next=frame+s.cooldown;
        }
        if(death) counts.erase(e.holder);
    }
    activeObjects=nullptr;
}
// Damage interception must finish synchronously, before the engine subtracts HP.
int adjustPendingDamage(const char* trigger,TechnoClass* holder,TechnoClass* other,
                        TechnoClass* attacker,TechnoClass* victim,int damage) {
    vector<string> ids;
    if(!interested(holder,trigger,&ids)) return damage;
    auto e=eventSnapshot(trigger,holder,other,attacker,victim,damage);
    auto distance=e.distance>=0?std::optional<double>{e.distance}:std::nullopt;
    HealthFormula::Values target{e.targetHp,e.targetMaxHp,e.targetCost};
    for(const auto& id:ids) {
        auto it=skills.find(id);
        if(it==skills.end() || suppressed(e.holder,id)) continue;
        const auto& skill=it->second;
        e.damage=damage;
        if(damage<skill.minDamage || !meetsState(skill,e.state,e.hp,e.maxHp,e.ammo,e.passengers) || !meetsEventFormula(skill,e) ||
           (!includes(skill.houses,"all") && !includes(skill.houses,e.relation)) ||
           (!includes(skill.kinds,"all") && !includes(skill.kinds,e.kind))) continue;
        if(skill.maxTriggers>0) {
            auto unit=triggerTotals.find(e.holder);
            if(unit!=triggerTotals.end()) {
                auto total=unit->second.find(id);
                if(total!=unit->second.end() && total->second>=static_cast<unsigned>(skill.maxTriggers)) continue;
            }
        }
        auto& progress=counts[e.holder][id];
        unsigned frame=Unsorted::CurrentFrame;
        if(frame<progress.next || ++progress.count<skill.count) continue;
        progress.count=0;
        if(skill.chance==0 || (skill.chance<100 && ScenarioClass::Instance->Random.RandomRanged(1,100)>skill.chance)) continue;
        bool success=false;
        auto stack=e.stackSnapshot.find(id);
        int stacks=stack==e.stackSnapshot.end()?0:stack->second;
        for(const auto& action:skill.actions) {
            if(action.action=="blockdamage") {damage=0;success=true;break;}
            int adjusted=action.amount;
            if(!action.amountFormula.expression.empty()) {
                bool missing=false;
                auto value=action.amountFormula.expression.evaluate(e.hp,e.maxHp,damage,distance,stacks,target,&missing,e.selfCost,e.ammo,e.passengers>=0?std::optional<int>{e.passengers}:std::nullopt);
                if(!value || *value<0 || *value>INT_MAX) {if(!missing) action.amountFormula.report();continue;}
                adjusted=static_cast<int>(*value);
            }
            damage=adjusted;success=true;
            if(!damage) break;
        }
        if(!success) continue;
        if(skill.maxTriggers>0) ++triggerTotals[e.holder][id];
        progress.next=frame+skill.cooldown;
        auto animTarget=skill.animTarget=="target"?other:holder;
        if(skill.triggerAnim && animTarget) playSkillAnim(skill.triggerAnim,animTarget,animTarget->GetCenterCoords(),holder->Owner);
        if(!damage) break;
    }
    return damage;
}
struct Income { int amount,interval; };
std::map<BuildingTypeClass*,Income> incomeTypes;
std::map<BuildingTypeClass*,vector<string>> rewardTypes;
const vector<string>& rewardOptions(BuildingTypeClass* type) {
    auto [it,fresh]=rewardTypes.try_emplace(type);
    if(fresh) it->second=split(read(type->ID,"Starfall.ResearchOptions"));
    return it->second;
}
bool eligibleHouse(HouseClass* h) {
    return h && h->ArrayIndex>=0 && !h->Defeated && !h->IsObserver() && !h->IsNeutral();
}
void tickGlobalResearch() {
    auto& state=Research::globalIncome;
    unsigned frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    if(state.initialized && (!state.periodicPoints || !state.intervalFrames || frame-state.lastFrame<state.intervalFrames)) return;
    std::set<int> players;
    auto add=[&](HouseClass* house) {if(eligibleHouse(house)) players.insert(house->ArrayIndex);};
    for(int slot=0;slot<8;++slot) add(HouseClass::FindByPlayerAt(HouseClass::PlayerAtA+slot));
    if(SessionClass::IsCampaign()) add(HouseClass::CurrentPlayer);
    if(players.empty()) return;
    if(!state.initialized) {
        for(int house:players) Research::award(house,state.initialPoints);
        state.initialized=true;state.lastFrame=frame;
    } else if(state.periodicPoints>0 && state.intervalFrames>0 && frame-state.lastFrame>=state.intervalFrames) {
        for(int house:players) Research::award(house,state.periodicPoints);
        state.lastFrame=frame;
    }
}
void tickBuildings() {
    if(!Research::isEnabled()) return;
    tickGlobalResearch();
    for(auto* b:BuildingClass::Array) {
        if(!b || !b->Type) continue;
        if(!rewardOptions(b->Type).empty() && b->Owner && alive(b)) {
            auto [reward,added]=Research::buildingRewards.try_emplace(b->UniqueID,Research::BuildingReward{b->Owner->ArrayIndex,-1,false});
            if(!added && reward->second.owner!=b->Owner->ArrayIndex) {
                reward->second.owner=b->Owner->ArrayIndex;
                reward->second.authorized=reward->second.claimed?-1:b->Owner->ArrayIndex;
            }
        }
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
void reset() { transportGrants.clear();transportStateKeys.clear();anyTransportPassives=false;auraSources.clear();auraGrants.clear();auraLosses.clear();anyAuras=false;discoverAuras=true;buildingPower.clear();anyBeforeDealDamage=false;anyBeforeTakeDamage=false;incomingDamage.clear();passiveStacks.clear();triggerTotals.clear();spawnGrants.clear();anySpawnPassives=false;anyGainSkills=false;gainInputs.clear();gainSeen.clear();UnitHealth::pools.clear();fireRateBase.clear();fireRateState.clear();rangeBase.clear();rangeState.clear();projectileBase.clear();projectileState.clear();VisualScale::projectiles.clear();VisualScale::explosions.clear();VisualScale::attachedAnims.clear();explosionBatches.clear();spreadScopes.clear();grantPresets.clear();grantRules.clear();warheadGrants.clear();bulletGrantImpacts.clear();reapplyImpacts.clear();rebindGrants=false;expiryScheduled=false;mirageUnits.clear();immuneUnits.clear();mindImmuneUnits.clear();VisualScale::base.clear();VisualScale::state.clear();VisualScale::clearSprites();attachedAnims.clear();activeAnimStates.clear();anyAttachedAnims=false;needMotion=false;nearbySamples.clear();weaponRules.clear(); volleyWeapons.clear(); mountCopies.clear(); buildingLookups.clear(); anyIntervals=false; periodic.clear(); shots.clear();previousState.clear();reportedDeaths.clear();removedPassives.clear();passiveBullets.clear();passiveDetonations.clear();stateTimers.clear(); applied.clear(); anyStateSkills=false; observePromotions=false; observeDeployments=false; needUnitPower=false; incomeTypes.clear(); rewardTypes.clear(); Research::reset(); counts.clear(); pending.clear(); bindings.clear(); skills.clear(); ready=false; }
} // namespace

void Research::ensureReady() { if(!ready) readSkills(); }
bool Research::isEnabled() { ensureReady();return enabled; }
std::vector<int> Research::buildingOptions(unsigned long id,int house) {
    std::vector<int> result;
    if(!isEnabled()) return result;
    auto state=buildingRewards.find(id);
    if(state==buildingRewards.end() || state->second.claimed || state->second.authorized!=house) return result;
    BuildingClass* building=nullptr;
    for(auto* b:BuildingClass::Array) if(b && b->UniqueID==id) {building=b;break;}
    if(!building || !building->Type || !alive(building) || !building->IsOnMap || building->InLimbo || !building->Owner || building->Owner->ArrayIndex!=state->second.owner) return result;
    const auto& ids=rewardOptions(building->Type);
    if(ids.empty() || ids.size()>3) return result;
    for(const auto& key:ids) for(int i=0;i<static_cast<int>(options.size());++i)
        if(options[i].id==key && available(i) && !owns(house,key)) {result.push_back(i);break;}
    return result;
}
bool Research::claimBuilding(unsigned long id,int house,int option) {
    auto choices=buildingOptions(id,house);
    if(std::find(choices.begin(),choices.end(),option)==choices.end() || !executeEffect(house,option)) return false;
    unlocked[house].insert(options[option].id);unlockOrder[house].push_back(options[option].id);
    buildingRewards[id].claimed=true;buildingRewards[id].authorized=-1;
    savedOffers.erase(house);refreshedSlots.erase(house);clearOfferCache();return true;
}
DEFINE_HOOK(0x51A002, SF_SpyResearchBuilding, 6) {
    auto spy=R->ESI<InfantryClass*>();auto building=R->EDI<BuildingClass*>();
    if(Research::isEnabled() && spy && eligibleHouse(spy->Owner) && building && building->Owner && building->Type && !rewardOptions(building->Type).empty()) {
        auto [it,added]=Research::buildingRewards.try_emplace(building->UniqueID,Research::BuildingReward{building->Owner->ArrayIndex,-1,false});
        if(!it->second.claimed) {it->second.owner=building->Owner->ArrayIndex;it->second.authorized=spy->Owner->ArrayIndex;}
    }
    return 0;
}
namespace {
// Purchased project IDs are already synchronized and saved per house. Resolve
// their grants once per house, including technologies inside combined projects.
std::map<int,std::set<TechnoTypeClass*>> productionLicenses;
bool hasProductionLicense(const HouseClass* house,TechnoTypeClass* type) {
    if(!house || !type || Research::unlocked.empty()) return false;
    const int player=house->ArrayIndex;
    auto owned=Research::unlocked.find(player);
    if(owned==Research::unlocked.end() || owned->second.empty()) return false;
    auto [entry,fresh]=productionLicenses.try_emplace(player);
    if(fresh) {
        auto collect=[&](const auto& effect) {
            if(effect.effect==Research::Effect::Technology)
                if(auto* item=TechnoTypeClass::Find(effect.technology.c_str())) entry->second.insert(item);
        };
        for(const auto& option:Research::options) if(owned->second.contains(option.id)) {
            if(option.effectList) {for(const auto& effect:option.effects) collect(effect);}
            else collect(option);
        }
    }
    return entry->second.contains(type);
}
bool useLicensedProduction(const HouseClass* house,TechnoTypeClass* type) {
    return hasProductionLicense(house,type) && type->FindFactory(true,false,false,house);
}
// Ares replaces the entire CanBuild routine. Forward to the installed checker
// with its build-limit-only argument so its factory and limit checks still run.
using ProductionCheck=CanBuildResult(__thiscall*)(const HouseClass*,TechnoTypeClass*,bool,bool);
ProductionCheck previousProductionCheck=nullptr;
CanBuildResult __fastcall checkLicensedProduction(const HouseClass* house,void*,TechnoTypeClass* type,bool limitsOnly,bool queued) {
    return previousProductionCheck(house,type,limitsOnly || useLicensedProduction(house,type),queued);
}
void installProductionCheck() {
    if(previousProductionCheck) return;
    auto entry=reinterpret_cast<BYTE*>(0x4F7870);
    if(entry[0]!=0xE9) return; // The original game takes the native hook below.
    DWORD old;
    if(!VirtualProtect(entry,5,PAGE_EXECUTE_READWRITE,&old)) {
        OutputDebugStringA("Starfall: cannot chain production availability check\n");return;
    }
    int displacement;
    memcpy(&displacement,entry+1,sizeof(displacement));
    auto next=reinterpret_cast<uintptr_t>(entry+5);
    previousProductionCheck=reinterpret_cast<ProductionCheck>(next+displacement);
    displacement=static_cast<int>(reinterpret_cast<uintptr_t>(&checkLicensedProduction)-next);
    memcpy(entry+1,&displacement,sizeof(displacement));
    FlushInstructionCache(GetCurrentProcess(),entry,5);
    VirtualProtect(entry,5,old,&old);
}
} // namespace
// Vanilla has loaded buildLimitOnly into AL before pushing EDI.
DEFINE_HOOK(0x4F787C,SF_NativeProductionLicense,7) {
    if(!R->AL() && useLicensedProduction(R->EBP<HouseClass*>(),R->Stack<TechnoTypeClass*>(0x4C))) R->AL(1);
    return 0;
}
namespace {
bool matchesConversion(TechnoClass* unit,HouseClass* h,TechnoTypeClass* from,TechnoTypeClass* to) {
    return unit && unit->Owner==h && alive(unit) && unit->GetTechnoType()==from && canTransformUnit(unit,to);
}
Research::EffectSpec singleEffect(const Research::Option& o) {
    return {o.effect,o.passive,o.technology,o.from,o.to,o.types,o.credits,o.cost};
}
bool validResearchEffect(const Research::EffectSpec& o,const EffectWarning& warn) {
    using Research::Effect;
    auto fail=[&](const char* key,const char* reason) {if(warn) warn(key,reason);return false;};
    switch(o.effect) {
        case Effect::Passive: {
            bool valid=true;
            if(!skills.contains(o.passive)) valid=fail("Passive","被动 ID 未注册、未填写或该被动已停用");
            if(o.types.empty()) return fail("TargetTypes","至少填写一个受益类型或类别");
            for(const auto& type:o.types) if(type.starts_with('@') && !Research::isTargetCategory(type))
                return fail("TargetTypes","未知的受益类别");
            return valid;
        }
        case Effect::TypeCost: {
            bool valid=true;
            if(!o.cost.delta) valid=fail("CostModifier","价格修正须为非零整数或百分比");
            if(o.types.empty()) return fail("TargetTypes","须填写有效的生产类型 ID");
            for(const auto& id:o.types) if(!TechnoTypeClass::Find(id.c_str())) {
                if(warn) warn("TargetTypes","未注册的生产类型 ["+id+"]");
                return false;
            }
            return valid;
        }
        case Effect::CategoryCost: {
            bool valid=true;
            if(!o.cost.delta || !o.cost.percent) valid=fail("CostModifier","类别价格修正须为非零百分比");
            if(o.cost.category==Research::CostCategory::Invalid) valid=fail("Category","未知的生产类别");
            return valid;
        }
        case Effect::Credits: return o.credits>0 || fail("Credits","须填写正整数资金");
        case Effect::Technology: return (!o.technology.empty() && TechnoTypeClass::Find(o.technology.c_str())) ||
            fail("Technology","须填写已注册的单位或建筑类型 ID");
        case Effect::ConvertInfantry:
        case Effect::ConvertVehicle: {
            auto from=TechnoTypeClass::Find(o.from.c_str()),to=TechnoTypeClass::Find(o.to.c_str());
            auto kind=o.effect==Effect::ConvertInfantry?AbstractType::InfantryType:AbstractType::UnitType;
            bool valid=true;
            if(!from || from->WhatAmI()!=kind) valid=fail("FromType","原类型不存在或类别不符");
            if(!to || to->WhatAmI()!=kind) valid=fail("ToType","目标类型不存在或类别不符");
            if(!valid) return false;
            if(from==to || !IsEqualGUID(from->Locomotor,to->Locomotor)) return fail("ToType","两个类型相同或移动方式不兼容");
            if(!conversionAvailable() || (!GetModuleHandleW(L"Phobos.dll") && !nativeConversionTypesSafe(from,to)))
                return fail("ToType","当前转换接口不支持这组类型");
            return true;
        }
        default: return fail("Effect","未知或缺少科研效果");
    }
}
bool researchEffectCanApply(HouseClass* h,const Research::EffectSpec& o) {
    using Research::Effect;
    if(o.effect==Effect::Passive || o.effect==Effect::TypeCost || o.effect==Effect::CategoryCost) return true;
    if(o.effect==Effect::Technology) {
        auto type=TechnoTypeClass::Find(o.technology.c_str());
        return type && !hasProductionLicense(h,type);
    }
    if(o.effect==Effect::ConvertInfantry || o.effect==Effect::ConvertVehicle) {
        auto from=TechnoTypeClass::Find(o.from.c_str()),to=TechnoTypeClass::Find(o.to.c_str());
        for(auto unit:TechnoClass::Array) if(matchesConversion(unit,h,from,to)) return true;
        return false;
    }
    return o.effect==Effect::Credits;
}
bool applyResearchEffect(HouseClass* h,const Research::EffectSpec& o) {
    using Research::Effect;
    if(!researchEffectCanApply(h,o)) return false;
    if(o.effect==Effect::Passive || o.effect==Effect::TypeCost || o.effect==Effect::CategoryCost) return true;
    if(o.effect==Effect::Credits) {h->GiveMoney(o.credits);return true;}
    if(o.effect==Effect::Technology) {
        h->RecheckTechTree=true;return true;
    }
    auto from=TechnoTypeClass::Find(o.from.c_str()),to=TechnoTypeClass::Find(o.to.c_str());
    vector<TechnoClass*> targets;
    for(auto unit:TechnoClass::Array) if(matchesConversion(unit,h,from,to)) targets.push_back(unit);
    int changed=0;
    for(auto unit:targets) if(matchesConversion(unit,h,from,to) && transformUnit(unit,to)) ++changed;
    return changed>0;
}
}
bool Research::available(int option) {
    if(!isEnabled() || option<0 || option>=static_cast<int>(options.size())) return false;
    const auto& o=options[option];
    if(o.rarity==Rarity::Invalid) return false;
    if(!o.effectList) return validResearchEffect(singleEffect(o));
    return !o.effects.empty() && std::all_of(o.effects.begin(),o.effects.end(),[](const auto& effect){ return validResearchEffect(effect); });
}
bool Research::canApply(int house,int option) {
    if(!isEnabled() || option<0 || option>=static_cast<int>(options.size()) || house<0 || house>=HouseClass::Array.Count) return false;
    auto h=HouseClass::Array.Items[house];if(!eligibleHouse(h)) return false;
    const auto& o=options[option];
    if(!o.effectList) return researchEffectCanApply(h,singleEffect(o));
    return std::any_of(o.effects.begin(),o.effects.end(),[h](const auto& effect){return researchEffectCanApply(h,effect);});
}
bool Research::executeEffect(int house,int option) {
    if(!canApply(house,option)) return false;
    auto h=HouseClass::Array.Items[house];const auto& o=options[option];
    if(!o.effectList) return applyResearchEffect(h,singleEffect(o));
    bool applied=false;
    for(const auto& effect:o.effects) if(applyResearchEffect(h,effect)) applied=true;
    return applied;
}
bool Research::meetsPrerequisites(int house,int option) {
    const auto& project=options[option];
    return project.validChain && std::all_of(project.prerequisites.begin(),project.prerequisites.end(),
        [house](const auto& id){return owns(house,id);});
}
// Opening the local UI must not consume the simulation RNG. Use the shared match
// seed plus the owner's index, and persist the chosen IDs rather than registry positions.
namespace {
struct OfferCache { string country; vector<int> rows,pool; };
std::map<int,OfferCache> offerCache;
}
namespace {
struct CostAdjustment {
    long long percent=0, credits=0;
    bool active=false;
};
using PriceKey=std::pair<int,TechnoTypeClass*>;
std::map<PriceKey,CostAdjustment> priceCache;
Research::CostCategory sidebarCategory(TechnoTypeClass* type) {
    using C=Research::CostCategory;
    switch(type->WhatAmI()) {
        case AbstractType::BuildingType:
            return static_cast<BuildingTypeClass*>(type)->BuildCat==BuildCat::Combat?C::Defense:C::Building;
        case AbstractType::InfantryType: return C::Infantry;
        case AbstractType::UnitType:
        case AbstractType::AircraftType: return C::Vehicle;
        default: return C::Invalid;
    }
}
CostAdjustment costAdjustment(HouseClass* house,TechnoTypeClass* type) {
    const auto owned=Research::unlocked.find(house->ArrayIndex);
    if(owned==Research::unlocked.end() || owned->second.empty()) return {};
    Research::ensureReady();
    auto [it,added]=priceCache.try_emplace({house->ArrayIndex,type});
    auto& sum=it->second;
    if(!added) return sum;
    const auto id=lower(type->ID);
    const auto category=sidebarCategory(type);
    auto collect=[&](const auto& e) {
        using Research::Effect;
        const bool byType=e.effect==Effect::TypeCost && includes(e.types,id);
        const bool byCategory=e.effect==Effect::CategoryCost && e.cost.category==category && e.cost.percent;
        if(!byType && !byCategory) return;
        if(!e.cost.delta) return;
        (e.cost.percent?sum.percent:sum.credits)+=e.cost.delta;
        sum.active=true;
    };
    for(const auto& project:Research::options) if(owned->second.contains(project.id)) {
        if(project.effectList) {for(const auto& effect:project.effects) collect(effect);}
        else collect(project);
    }
    return sum;
}
}
// GetActualCost has already applied country, difficulty and factory plant modifiers.
DEFINE_HOOK(0x711F46,SF_ResearchProductionPrice,5) {
    const int price=R->EAX<int>();
    if(price<0 || Research::unlocked.empty()) return 0;
    const auto mod=costAdjustment(R->EDI<HouseClass*>(),R->ESI<TechnoTypeClass*>());
    if(mod.active) {
        double adjusted=price*(100.0+mod.percent)/100.0+mod.credits;
        R->EAX(static_cast<int>(std::round(std::clamp(adjusted,0.0,static_cast<double>(INT_MAX)))));
    }
    return 0;
}
// The vanilla cancellation code re-quotes the product. Its Value holds the
// original order price, including for orders started before buying a project.
DEFINE_HOOK(0x4CA03D,SF_ResearchProductionCancel,6) {
    auto factory=R->ESI<FactoryClass*>();
    if(!Research::unlocked.empty() && factory->Owner && factory->Object &&
       costAdjustment(factory->Owner,factory->Object->GetTechnoType()).active)
        R->EAX(factory->Object->Value);
    return 0;
}
void Research::clearOfferCache() { offerCache.clear(); priceCache.clear(); productionLicenses.clear(); }
const vector<int>& Research::offers(int house,const char* country) {
    static const vector<int> empty;
    if(!isEnabled()) return empty;
    const string countryId=lower(country?country:"");
    auto existing=offerCache.find(house);
    if(existing!=offerCache.end() && existing->second.country==countryId) return existing->second.rows;
    auto& result=offerCache[house];
    result.country=countryId;
    result.rows.clear();
    result.pool.clear();
    std::array<vector<int>,3> pools;
    for(int i=0;i<static_cast<int>(options.size());++i) {
        const auto& allowed=options[i].countries;
        int rarity=static_cast<int>(options[i].rarity);
        if(rarity>=0 && rarity<3 && rarityWeights[rarity]>0 && !owns(house,options[i].id) && available(i) && meetsPrerequisites(house,i) && (allowed.empty() || includes(allowed,"*") || includes(allowed,countryId))) pools[rarity].push_back(i);
    }
    auto& ids=savedOffers[house];
    int chosen=-1;
    for(const auto& id:ids) {
        for(int rarity=0;rarity<3 && chosen<0;++rarity)
            for(int i:pools[rarity]) if(options[i].id==id) {chosen=rarity;break;}
        if(chosen>=0) break;
    }
    // Use only synchronized state, so opening the panel does not consume game RNG.
    unsigned seed=static_cast<unsigned>(Game::Seed) ^ (0x9E3779B9u*(house+1u));
    auto owned=unlocked.find(house);
    if(owned!=unlocked.end()) for(const auto& id:owned->second) {
        for(unsigned char c:id) seed=(seed^c)*16777619u;
        seed=(seed^0xffu)*16777619u;
    }
    std::mt19937 rng(seed);
    if(chosen<0) {
        unsigned long long total=0;
        for(int i=0;i<3;++i) if(!pools[i].empty()) total+=static_cast<unsigned>(rarityWeights[i]);
        if(total) {
            auto draw=std::uniform_int_distribution<unsigned long long>(0,total-1)(rng);
            for(int i=0;i<3;++i) if(!pools[i].empty()) {
                auto weight=static_cast<unsigned>(rarityWeights[i]);
                if(draw<weight) {chosen=i;break;}
                draw-=weight;
            }
        }
    }
    if(chosen<0) {ids.clear();return result.rows;}
    auto& pool=pools[chosen];
    auto remember=[&](int option) {
        if(result.rows.size()<choices && std::find(pool.begin(),pool.end(),option)!=pool.end()
            && std::find(result.rows.begin(),result.rows.end(),option)==result.rows.end()) result.rows.push_back(option);
    };
    for(const auto& id:ids)
        for(int i:pool) if(options[i].id==id) { remember(i); break; }
    if(pool.size()>choices) {
        std::mt19937 shuffleRng(seed ^ 0xA511E9B3u);
        std::shuffle(pool.begin(),pool.end(),shuffleRng);
    }
    for(int i:pool) { if(result.rows.size()==choices) break; remember(i); }
    ids.clear();
    for(int i:result.rows) ids.push_back(options[i].id);
    result.pool=std::move(pool);
    return result.rows;
}
bool Research::slotUsed(int house,int slot) {
    auto it=refreshedSlots.find(house);
    return slot>=0 && slot<choices && it!=refreshedSlots.end() && (it->second & (1u<<slot))!=0;
}
bool Research::canRefresh(int house,const char* country,int slot) {
    ensureReady();
    if(!allowCardRefresh || balance(house)<price || slot<0 || slot>=choices || slotUsed(house,slot)) return false;
    const auto& rows=offers(house,country);
    return static_cast<size_t>(slot)<rows.size() && offerCache.at(house).pool.size()>rows.size();
}
bool Research::refresh(int house,const char* country,int slot,int expected) {
    if(!canRefresh(house,country,slot)) return false;
    auto& board=offerCache.at(house);
    if(board.rows[slot]!=expected) return false;
    for(int option:board.pool) {
        if(std::find(board.rows.begin(),board.rows.end(),option)!=board.rows.end()) continue;
        board.rows[slot]=option;
        savedOffers.at(house)[slot]=options[option].id;
        refreshedSlots[house]|=1u<<slot;
        return true;
    }
    return false;
}
static void replaceNumber(std::wstring& text,const std::wstring& key,int value) {
    const auto number=std::to_wstring(value); size_t pos=0;
    while((pos=text.find(key,pos))!=std::wstring::npos) {
        text.replace(pos,key.size(),number); pos+=number.size();
    }
}
static std::wstring describeAction(const PassiveAction& a,std::wstring text) {
    if(text.empty()) {
        if(a.percentIndex<percentActions.size()) {
            constexpr std::array names={L"移速",L"火力",L"装甲",L"体型",L"弹体尺寸",L"武器射程",L"射速"};
            if(!a.formula.expression.empty()) return std::wstring(names[a.percentIndex])+L"按事件公式变化";
            return std::wstring(names[a.percentIndex])+L"变化 "+std::to_wstring(a.percentDelta)+L" 个百分点";
        }
        if(a.action=="spawnunit") return L"生成单位："+displayText(a.unitType->ID);
        if(a.action=="transform") return L"原地变形为："+(a.unitType->UIName && *a.unitType->UIName ?
            std::wstring(a.unitType->UIName) : displayText(a.unitType->ID));
        if(a.action=="changeowner") return L"修改目标所属方";
        if(a.action=="changemaxhealth") return a.maxHealthPercent?
            L"永久提高当前最大生命的 "+std::to_wstring(a.maxHealthPercent)+L"%":
            L"永久改变最大生命："+std::to_wstring(a.amount)+L" 点";
        if(a.action=="changeammo") return L"弹药变化："+std::to_wstring(a.amount);
        if(a.action=="spawncrate") {
            if(a.crateType==randomCrate) return L"生成一个随机升级箱";
            return L"生成升级箱："+displayText(Powerups::Effects[static_cast<unsigned>(a.crateType)]);
        }
        if(a.action=="selfdestruct") return L"触发后摧毁自身";
        if(a.action=="grantpassive") return L"授予被动："+displayText(a.passive)+(a.duration<0?L"（永久）":L"（"+std::to_wstring(a.duration)+L" 帧）")+(a.stackOnReapply?L"；重复授予加 1 层":L"");
        if(a.action=="removepassive") return L"移除目标的指定被动";
        if(a.action=="stealpassive") return L"夺取目标的指定被动，每项最多 "+std::to_wstring(a.amount)+L" 层";
        if(a.action=="changepassivestack") return L"修改被动层数："+displayText(a.passive)+L" "+(a.amount>0?L"+":L"")+std::to_wstring(a.amount);
        if(a.action=="consumepassenger") return L"消耗一名乘客";
        if(a.action=="blockdamage") return L"抵挡本次伤害";
        if(a.action=="setdamage") return a.amountFormula.expression.empty() ?
            L"本次伤害设为 "+std::to_wstring(a.amount) : L"按公式调整本次伤害";
        if(a.action=="fireweapon") return L"向目标发射一枚指定武器弹体";
        if(a.action=="replaceweapon") return L"开火时替换武器："+displayText(a.weapon);
        if(a.action=="detonatewarhead") return L"在目标位置引爆指定弹头";
        text=a.action=="heal"?L"恢复生命":a.action=="damage"?L"造成伤害":a.action=="giveresearchpoints"?L"获得科研点":L"获得资金";
        if(!a.amountFormula.expression.empty()) return text+L"（按公式计算）";
        text+=a.amount?L"：{amount}":L"：实际伤害的 {percent}%";
    }
    replaceNumber(text,L"{amount}",a.amount); replaceNumber(text,L"{percent}",a.damagePercent);
    return text;
}
static std::wstring describeResearchEffect(const Research::Option& o,wchar_t (&conditions)[96]) {
    using Research::Effect;
    if(o.effect!=Effect::Passive) {
        swprintf_s(conditions,L"即时生效  ·  每位玩家限购一次");
        auto detail=o.detail;
        if(o.effect==Effect::TypeCost || o.effect==Effect::CategoryCost) {
            if(!detail.empty()) return detail;
            return std::wstring(L"生产价格")+(o.cost.delta<0?L"降低 ":L"提高 ")+
                std::to_wstring(std::abs(o.cost.delta))+(o.cost.percent?L"%":L" 资金");
        }
        if(detail.empty()) detail=o.effect==Effect::Credits?L"立即获得 {credits} 资金":
            o.effect==Effect::Technology?L"获得指定单位或建筑的生产权限":
            o.effect==Effect::ConvertVehicle?L"转换当前全部符合条件的己方载具":L"转换当前全部符合条件的己方步兵";
        replaceNumber(detail,L"{credits}",o.credits);
        return detail;
    }
    auto existing=skills.find(o.passive);
    if(existing==skills.end()) {
        swprintf_s(conditions,L"原被动配置不可用");
        return o.detail.empty()?L"已研究项目的被动配置不存在":o.detail;
    }
    const auto& s=existing->second;
    if(s.trigger.empty()) {
        swprintf_s(conditions,L"持有被动期间显示");
        return o.detail.empty()?L"在单位身上挂载动画":o.detail;
    }
    if(s.trigger=="whilestate") {
        if(s.stateDelayFrames) swprintf_s(conditions,L"连续满足 %u 帧后生效，条件中断即重置",s.stateDelayFrames);
        else swprintf_s(conditions,L"条件满足期间持续生效");
        if(!o.detail.empty()) return o.detail;
        wchar_t info[96]{};
        constexpr std::array names={L"移速",L"火力",L"护甲",L"武器射程",L"射速",L"建筑电力"};
        std::wstring summary;
        for(size_t k=0;k<s.multipliers.size();++k) if(s.multipliers[k]!=1.0) {
            swprintf_s(info,L"%s ×%.2f  ",names[k],s.multipliers[k]);
            summary+=info;
        }
        for(const auto& entry:s.stateFormulas) summary+=std::wstring(names[entry.first])+L"按公式变化  ";
        if(s.triggerAnim) summary+=L"进入状态时播放动画  ";
        if(s.projectileScale!=100) summary=L"弹体、爆炸和范围 "+std::to_wstring(s.projectileScale)+L"%  "+summary;
        if(s.visualScale!=100) summary=L"体型 "+std::to_wstring(s.visualScale)+L"%  "+summary;
        if(s.berserk) summary=L"进入混乱状态  "+summary;
        if(s.aura.range) {
            summary+=s.aura.range==-1?L"全图光环 ":L"光环 "+std::to_wstring(s.aura.range)+L" 格 ";
            for(const auto& id:s.aura.passives) summary+=displayText(id)+L" ";
        }
        for(const auto& id:s.spawnPassives) summary+=L" 子机获得 "+displayText(id);
        if(s.immune) summary=L"免疫常规伤害  "+summary;
        if(s.mindImmune) summary=L"免疫心灵控制  "+summary;
        if(s.mirage) summary=L"载具静止时伪装  "+summary;
        for(int slot=0;slot<2;++slot) {
            if(s.slots[slot].disabled) summary+=slot?L"  禁用副武器":L"  禁用主武器";
            else if(s.slots[slot].replacement) {
                summary+=slot?L"  副武器：":L"  主武器：";
                summary+=displayText(s.slots[slot].replacement->ID);
            }
        }
        return summary;
    }
    const wchar_t* event=s.trigger=="onpassivegained"?L"获得被动":s.trigger=="onpassengerconsumed"?L"消耗乘客":s.trigger=="onbeforedealdamage"?L"造成伤害前判定":s.trigger=="onbeforetakedamage"?L"受伤前判定":L"判定";
    swprintf_s(conditions,L"每 %d 次%s  ·  概率 %d%%  ·  冷却 %d 帧",s.count,event,s.chance,s.cooldown);
    std::wstring text;
    if(s.actions.empty()) {
        text=o.detail.empty()?(s.triggerAnim?L"触发时播放动画":L"持有期间显示动画"):o.detail;
    } else if(!o.detail.empty()) {
        text=describeAction(s.actions.front(),o.detail);
    } else {
        for(const auto& action:s.actions) {
            if(!text.empty()) text+=L'\n';
            text+=describeAction(action,{});
        }
    }
    replaceNumber(text,L"{count}",s.count); replaceNumber(text,L"{chance}",s.chance); replaceNumber(text,L"{cooldown}",s.cooldown);
    return text;
}
std::wstring Research::describe(int option,wchar_t (&conditions)[96]) {
    const auto& o=options[option];
    if(!o.effectList) return describeResearchEffect(o,conditions);
    swprintf_s(conditions,L"多个效果按顺序生效");
    if(!o.detail.empty()) return o.detail;
    std::wstring result;
    for(const auto& effect:o.effects) {
        Option part;part.effect=effect.effect;part.passive=effect.passive;part.types=effect.types;
        part.cost=effect.cost;part.credits=effect.credits;part.technology=effect.technology;part.from=effect.from;part.to=effect.to;
        wchar_t requirement[96]{};
        if(!result.empty()) result+=L'\n';
        result+=describeResearchEffect(part,requirement);
        if(effect.effect==Effect::Passive) {result+=L"（";result+=requirement;result+=L"）";}
    }
    return result;
}
// Scale only this building's contribution, after engine/extension power getters
// have included damage, upgrades and power-plant enhancers.
DEFINE_HOOK(0x508CF9,SF_BuildingPowerOutput,6) {
    auto building=R->EDI<BuildingClass*>();
    R->EAX(powerContribution(building,R->EAX<int>(),R->ESI<HouseClass*>()->PowerOutput));
    return 0;
}
DEFINE_HOOK(0x508D0E,SF_BuildingPowerDrain,6) {
    auto building=R->EDI<BuildingClass*>();
    R->EAX(powerContribution(building,R->EAX<int>(),R->ESI<HouseClass*>()->PowerDrain));
    return 0;
}
// The blackout check queries output a second time, outside the accumulation.
DEFINE_HOOK(0x508D29,SF_BuildingPowerOutageCheck,5) {
    if(buildingPower.empty()) return 0;
    auto building=R->EDI<BuildingClass*>();
    R->EAX(powerContribution(building,building->GetPowerOutput(),0));
    return 0x508D2E;
}
DEFINE_HOOK(0x683549, SF_ScenarioStart, 9) { reset();installAresMindGuards(); return 0; }
DEFINE_HOOK(0x6F4500, SF_UnitDestroyed, 5) {
    auto id=R->ECX<TechnoClass*>()->UniqueID;
    auto power=buildingPower.find(id);
    if(power!=buildingPower.end()) {
        if(auto owner=power->second.owner) owner->RecheckPower=true;
        buildingPower.erase(power);
    }
    stopAuraSources(id);
    if(auraGrants.erase(id)) {
        for(auto& entry:auraSources) entry.second.targets.erase(id);
    }
    auraLosses.erase(id);
    transportGrants.erase(id);
    std::erase_if(transportStateKeys,[&](const auto& key){return key.first==id;});
    spawnGrants.erase(id);
    UnitHealth::pools.erase(id);gainSeen.erase(id);gainInputs.erase(id);
    warheadGrants.erase(id);passiveStacks.erase(id);mirageUnits.erase(id);immuneUnits.erase(id);mindImmuneUnits.erase(id);projectileBase.erase(id);projectileState.erase(id);rangeBase.erase(id);rangeState.erase(id);fireRateBase.erase(id);fireRateState.erase(id);
    VisualScale::base.erase(id);VisualScale::state.erase(id);
    weaponRules.erase(id); volleyWeapons.erase(id); mountCopies.erase({id,0});mountCopies.erase({id,1});
    previousState.erase(id); reportedDeaths.erase(id); removedPassives.erase(id); counts.erase(id); triggerTotals.erase(id); applied.erase(id); Research::producers.erase(id);
    if(activeObjects) activeObjects->erase(id);
    return 0;
}
DEFINE_HOOK(0x466507,SF_ProjectileLaunchScale,6) {
    auto bullet=R->ECX<BulletClass*>();
    if(auto owner=bullet->Owner) {
        auto fixed=projectileBase.find(owner->UniqueID);
        auto state=projectileState.find(owner->UniqueID);
        double value=(fixed==projectileBase.end()?100:fixed->second)*(state==projectileState.end()?1.0:state->second);
        int percent=static_cast<int>(std::lround(std::clamp(value,1.0,static_cast<double>(INT_MAX))));
        if(percent!=100) {VisualScale::projectiles[bullet->UniqueID]=percent;return 0;}
    }
    VisualScale::projectiles.erase(bullet->UniqueID);
    return 0;
}
DEFINE_HOOK(0x469BD4,SF_ExplosionBegin,8) {
    auto bullet=R->ESI<BulletClass*>();
    auto found=VisualScale::projectiles.find(bullet->UniqueID);
    if(found!=VisualScale::projectiles.end())
        explosionBatches[R->EBP()]={AnimClass::Array.Count,found->second};
    return 0;
}
DEFINE_HOOK(0x469D06,SF_ExplosionEnd,6) {
    finishExplosions(R->EBP());
    return 0;
}
// Bullet impacts can happen several frames after FireWeapon returns. Guard both
// exits of BulletClass::Detonate and keep an enclosing passive impact suppressed.
DEFINE_HOOK(0x4690BC,SF_ImpactEnter,5) {
    auto bullet=R->ECX<BulletClass*>();
    auto scale=VisualScale::projectiles.find(bullet->UniqueID);
    beginReapplyImpact(R->EBP(),bullet->WH);
    enterSpread(R->EBP(),bullet->WH,scale==VisualScale::projectiles.end()?100:scale->second);
    if(passiveBullets.contains(bullet->UniqueID)) passiveDetonations.insert(R->EBP());
    return 0;
}
DEFINE_HOOK(0x469211,SF_BulletGrantPassives,6) {
    auto bullet=R->ESI<BulletClass*>();
    if(!grantRules.contains(bullet->WH)) return 0;
    auto at=R->Base<CoordStruct*>(8);
    auto house=bullet->Owner?bullet->Owner->Owner:nullptr;
    bulletGrantImpacts[R->EBP()]={bullet->WH,*at,house};
    deliverWarheadGrants(bullet->WH,*at,house,bullet->Target);
    return 0;
}
// Suspend the outer radius before Phobos and the native area logic inspect it.
DEFINE_HOOK(0x489280,SF_AreaSpreadEnter,6) {
    if(!spreadScopes.empty() && (R->Stack<DWORD>(0)!=0x469A88 || R->EBP()!=spreadScopes.back().frame))
        enterSpread(R->ESP()-sizeof(DWORD),nullptr,100);
    return 0;
}
DEFINE_HOOK(0x48928C,SF_AreaGrantPassives,5) {
    auto wh=R->Base<WarheadTypeClass*>(0xC);
    // The original bullet-to-area call is part of the same detonation. Other
    // area calls (including nested hits at identical coordinates) are separate.
    bool bulletArea=!reapplyImpacts.empty() && reapplyImpacts.back().wh==wh &&
        reapplyImpacts.back().frame==R->Base<DWORD>(0) && R->Base<DWORD>(4)==0x469A88;
    if(!bulletArea) beginReapplyImpact(R->EBP(),wh);
    if(!grantRules.contains(wh) || !(wh->CellSpread>0)) return 0;
    auto firer=R->Base<TechnoClass*>(8);
    auto house=firer?firer->Owner:R->Base<HouseClass*>(0x14);
    auto at=R->ECX<CoordStruct*>();
    auto parent=bulletGrantImpacts.find(R->Base<DWORD>(0));
    if((grantRules.at(wh).stackOnReapply && !bulletArea) || parent==bulletGrantImpacts.end() || parent->second.wh!=wh ||
       parent->second.at!=*at || parent->second.source!=house) deliverWarheadGrants(wh,*at,house,nullptr);
    return 0;
}
DEFINE_HOOK_AGAIN(0x489B3B,SF_AreaSpreadLeave,5)
DEFINE_HOOK_AGAIN(0x48A4DA,SF_AreaSpreadLeave,5)
DEFINE_HOOK(0x48A4AE,SF_AreaSpreadLeave,5) {endReapplyImpact(R->EBP());leaveSpread(R->EBP());return 0;}
DEFINE_HOOK_AGAIN(0x46A2FB,SF_ImpactLeave,5)
DEFINE_HOOK(0x46A290,SF_ImpactLeave,5) {endReapplyImpact(R->EBP());finishExplosions(R->EBP());leaveSpread(R->EBP());bulletGrantImpacts.erase(R->EBP());passiveDetonations.erase(R->EBP());return 0;}
DEFINE_HOOK(0x4665E9,SF_BulletDestroyed,10) {auto id=R->ESI<BulletClass*>()->UniqueID;VisualScale::projectiles.erase(id);passiveBullets.erase(id);return 0;}
void queueDeath(TechnoClass* victim,TechnoClass* killer) {
    vector<string> deathSkills;
    if(victim && interested(victim,"ondeath",&deathSkills) && reportedDeaths.insert(victim->UniqueID).second) {
        auto e=eventSnapshot("ondeath",victim,killer,killer,victim,0);
        e.snapshot=std::move(deathSkills);pending.push_back(std::move(e));
    }
    if(victim) stopAuraSources(victim->UniqueID);
}
DEFINE_HOOK(0x702E4E, SF_Kill, 6) {
    auto killer=R->EDI<TechnoClass*>(), victim=R->ESI<TechnoClass*>();
    if(victim && victim->Health<=0) queueDeath(victim,killer);
    if(victim) capture("onkill",killer,victim,killer,victim,0);
    return 0;
}
// Only full rearm passes through these exits. Burst cadence is calculated
// separately. The normal path already includes house, veteran and bunker rules.
DEFINE_HOOK(0x6FD1F1,SF_FullRearmFireRate,5) {
    R->EBP(adjustedRearmDelay(R->ESI<TechnoClass*>(),R->EBP<int>()));
    return 0;
}
DEFINE_HOOK(0x6FD1FA,SF_FixedRearmFireRate,6) {
    R->EAX(adjustedRearmDelay(R->ESI<TechnoClass*>(),R->EDI<WeaponTypeClass*>()->ROF));
    return 0x6FD200;
}
// InRange performs a separate calculation. EDI now holds the maximum in
// leptons, including native bonuses. MinimumRange is checked independently.
DEFINE_HOOK(0x6F72E3,SF_WeaponRangeCheck,6) {
    R->EDI(adjustedWeaponRange(R->ESI<TechnoClass*>(),R->EDI<int>()));
    return 0;
}
// Aircraft attack missions also compare distance directly. Use the range of
// the resolved mount (including alternate aircraft weapons selected by Phobos).
DEFINE_HOOK(0x4180FF,SF_AircraftAttackRange,6) {
    auto plane=R->ESI<AircraftClass*>();
    if(!rangeBase.contains(plane->UniqueID) && !rangeState.contains(plane->UniqueID)) return 0;
    auto mount=R->EAX<WeaponStruct*>();
    if(!mount || !mount->WeaponType || !plane->Target) return 0;
    int slot=R->EBX<int>();
    if(slot<0 || slot>=TechnoTypeClass::MaxWeapons || plane->GetWeapon(slot)!=mount) {
        slot=plane->SelectWeapon(plane->Target);
        if(slot<0 || slot>=TechnoTypeClass::MaxWeapons || plane->GetWeapon(slot)!=mount) return 0;
    }
    R->EDI(mount->WeaponType);
    return plane->DistanceFrom(plane->Target)<plane->GetWeaponRange(slot)?0x418117:0x418131;
}
DEFINE_HOOK(0x418BAE,SF_AircraftAttackDelayRange,5) {
    R->EAX(adjustedWeaponRange(R->ESI<AircraftClass*>(),R->EAX<int>()));
    return 0;
}
// Standard units, infantry, aircraft, and the base path used by buildings.
// EAX already contains the mount chosen by the engine (including elite FLH).
DEFINE_HOOK(0x70E190,SF_WeaponMount,5) {
    if(!weaponRules.empty() || !volleyWeapons.empty()) applyMount(R,R->ESI<TechnoClass*>()->UniqueID,R->EDI<int>());
    return 0;
}
// Occupied/upgraded buildings also have paths that bypass the base getter.
// Preserve the building identity: ESI may hold an occupant at the epilogue.
DEFINE_HOOK(0x4526F0,SF_BuildingMountStart,6) {
    if(!weaponRules.empty() || !volleyWeapons.empty()) {
        auto id=R->ECX<BuildingClass*>()->UniqueID;
        int slot=R->Stack<int>(4);
        auto volley=volleyWeapons.find(id);
        if((slotRule(id,slot) && slotRule(id,slot)->replacement) ||
           (volley!=volleyWeapons.end() && volley->second.slot==slot && volley->second.replacement))
            buildingLookups[R->ESP()]={id,slot};
    }
    return 0;
}
DEFINE_HOOK_AGAIN(0x45278B,SF_BuildingMountEnd,7)
DEFINE_HOOK_AGAIN(0x4527AD,SF_BuildingMountEnd,7)
DEFINE_HOOK(0x4527BC,SF_BuildingMountEnd,7) {
    if(!buildingLookups.empty()) {
        auto it=buildingLookups.find(R->ESP()+16);
        if(it!=buildingLookups.end()) {
            applyMount(R,it->second.id,it->second.slot);buildingLookups.erase(it);
        }
    }
    return 0;
}
// For a conventional primary/secondary loadout, select the remaining weapon.
// Native fire checks still enforce range, ammunition and targeting restrictions.
DEFINE_HOOK(0x6F3330,SF_SelectEnabledSlot,5) {
    if(weaponRules.empty()) return 0;
    auto unit=R->ECX<TechnoClass*>();
    auto it=weaponRules.find(unit->UniqueID);
    if(it==weaponRules.end() || it->second[0].disabled==it->second[1].disabled) return 0;
    auto type=unit->GetTechnoType();
    if(type->Gunner || type->IsGattling || type->WeaponCount>2 || type->HasMultipleTurrets()) return 0;
    int other=it->second[0].disabled?1:0;
    auto mount=unit->GetWeapon(other);
    if(!mount || !mount->WeaponType) return 0;
    R->EAX(other);return 0x6F3813;
}
DEFINE_HOOK(0x6FC0B0,SF_DisabledSlotCheck,8) {
    if(!weaponRules.empty()) {
        auto rule=slotRule(R->ECX<TechnoClass*>()->UniqueID,R->Stack<int>(8));
        if(rule && rule->disabled) { R->EAX(5); return 0x6FC876; }
    }
    return 0;
}DEFINE_HOOK(0x6FDD7D,SF_StartFiring,5) {
    auto u=R->ESI<TechnoClass*>(); auto key=R->EBP();shots.erase(key);
    int slot=R->Base<int>(12);
    if(auto rule=slotRule(u->UniqueID,slot);rule && rule->disabled) return 0x6FDE03;
    if(slot>=0 && slot<2 && !applyingDamage && passiveDetonations.empty() && alive(u) &&
       (volleyWeapons.contains(u->UniqueID) || interested(u,"onbeforefire"))) {
        auto aim=R->Base<AbstractClass*>(8);
        if(u->CurrentBurstIndex==0) {
            volleyWeapons.erase(u->UniqueID);
            // Fire fetched the mount before this hook; discard any previous volley's
            // cached replacement before deciding whether this volley qualifies.
            auto native=u->GetWeapon(slot);
            if(!native || !native->WeaponType) return 0x6FDE03;
            R->EBX(native->WeaponType);R->Stack(0x40,native->WeaponType);
            vector<string> ids;
            if(aim && interested(u,"onbeforefire",&ids)) {
                auto other=(aim->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None?static_cast<TechnoClass*>(aim):nullptr;
                auto event=eventSnapshot("onbeforefire",u,other,u,other,0);
                for(const auto& id:ids) {
                    const auto& s=skills.at(id);
                    if(!meetsState(s,event.state,event.hp,event.maxHp,event.ammo,event.passengers) || !meetsEventFormula(s,event) ||
                       (!includes(s.houses,"all") && !includes(s.houses,event.relation)) ||
                       (!includes(s.kinds,"all") && !includes(s.kinds,event.kind))) continue;
                    auto candidate=WeaponTypeClass::Find(s.actions[0].weapon.c_str());
                    volleyWeapons[u->UniqueID]={slot,nullptr,static_cast<unsigned>(Unsorted::CurrentFrame)};
                    if(!candidate || !candidate->Projectile || !candidate->Warhead || candidate->NeverUse) break;
                    volleyWeapons[u->UniqueID].replacement=candidate;
                    bool air=other && other->IsInAir();
                    if((air && !candidate->Projectile->AA) || (!air && !candidate->Projectile->AG) ||
                       u->GetFireError(aim,slot,false)!=FireError::OK || !u->IsCloseEnough(aim,slot))
                        volleyWeapons[u->UniqueID].replacement=nullptr;
                    break;
                }
            }
        }
        auto choice=volleyWeapons.find(u->UniqueID);
        if(choice!=volleyWeapons.end() && choice->second.slot==slot && choice->second.replacement) {
            auto weapon=choice->second.replacement;
            R->EBX(weapon);R->Stack(0x40,weapon);
        }
    }
    vector<string> fireSkills;
    if(!applyingDamage && passiveDetonations.empty() && alive(u) && interested(u,"onfire",&fireSkills)) {
        auto aim=R->Base<AbstractClass*>(8);
        auto victim=aim && (aim->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None?static_cast<TechnoClass*>(aim):nullptr;
        auto e=eventSnapshot("onfire",u,victim,u,victim,0);
        e.snapshot=std::move(fireSkills);
        if(aim) {e.impact=aim->GetCoords();e.hasImpact=true;if(!victim) e.distance=distanceCells(e.origin,e.impact);}
        shots.emplace(key,std::move(e));
    }return 0;
}
DEFINE_HOOK(0x6FF660,SF_ShotCommitted,6) {
    auto it=shots.find(R->EBP());
    if(it!=shots.end()) {pending.push_back(std::move(it->second));shots.erase(it);}
    auto unit=R->ESI<TechnoClass*>();
    if(observeDeployments && alive(unit) && unit->WhatAmI()==AbstractType::Unit &&
        unit->CurrentMission==Mission::Unload && unit->GetTechnoType()->DeployFire &&
        !static_cast<UnitClass*>(unit)->Type->IsSimpleDeployer) capture("ondeploy",unit,nullptr,unit,unit,0);
    return 0;
}
DEFINE_HOOK(0x739956,SF_DeployedInto,6) {
    auto old=R->EBP<UnitClass*>(); auto building=R->EBX<BuildingClass*>();
    vector<string> ids;
    if(old && building && interested(old,"ondeploy",&ids)) {
        auto e=eventSnapshot("ondeploy",old,nullptr,building,building,0);
        e.holder=building->UniqueID;e.snapshot=std::move(ids);pending.push_back(std::move(e));
    }return 0;
}
// Preserve the original sign before Phobos and native damage adjustments.
// The entry stack address distinguishes nested damage calls.
DEFINE_HOOK(0x701900,SF_DamageInput,6) {
    if(anyBeforeDealDamage || anyBeforeTakeDamage) {
        auto amount=R->Stack<int*>(4);
        incomingDamage[R->ESP()]={R->ECX<TechnoClass*>(),amount,!amount || *amount<=0};
    }
    return 0;
}
// These two points straddle ObjectClass::ReceiveDamage. Armor and overkill
// are accounted for by measuring the actual reduction in hit points.
// ObjectClass::ReceiveDamage in gamemd 1.001. Its damage/armor math operates in
// real hit points. Publish the native-scale Health only at the two write sites.
DEFINE_HOOK(0x5F53E5,SF_DamageMaxHealth,6) {
    auto obj=R->ESI<ObjectClass*>();
    if(auto p=UnitHealth::find(obj->UniqueID)) {
        p->sync(static_cast<TechnoClass*>(obj),R->EAX<ObjectTypeClass*>()->Strength);
        R->Stack(0x14,p->current);
        if(R->EDI<int*>()==&obj->Health) {
            // Some forced kills pass &Health itself. Keep the damage operand separate.
            R->Stack(0x18,p->current);
            R->EDI(R->lea_Stack<int*>(0x18));
        }
        R->EBP(p->maximum);
        return 0x5F53EB;
    }
    return 0;
}
DEFINE_HOOK(0x5F546A,SF_NativeHealMaxHealth,5) {
    auto obj=R->ESI<ObjectClass*>();
    if(auto p=UnitHealth::find(obj->UniqueID)) {
        int old=p->current;
        int next=static_cast<int>(std::min<int64_t>(p->maximum,int64_t(old)-R->ECX<int>()));
        p->set(static_cast<TechnoClass*>(obj),next);
        return next!=old?0x5F5480:0x5F548C;
    }
    return 0;
}
// This positive-damage branch runs after native armor and immunity checks,
// but before the engine clamps damage to health or decides lethal state.
DEFINE_HOOK(0x5F5498,SF_BeforeTakeDamage,12) {
    auto object=R->ESI<ObjectClass*>();
    auto amount=R->EDI<int*>();
    const bool enabled=anyBeforeDealDamage || anyBeforeTakeDamage;
    bool nonpositiveInput=false;
    if(enabled && R->Stack<DWORD>(0x24)==0x701DFD) {
        auto found=incomingDamage.find(R->ESP()+0x108);
        if(found!=incomingDamage.end()) {
            nonpositiveInput=found->second.unit==object && found->second.amount==amount && found->second.nonpositive;
            incomingDamage.erase(found);
        }
    }
    if(enabled && !nonpositiveInput && !(R->EBX<DWORD>()&0xff) && object && amount && *amount>0 &&
       (object->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None) {
        auto source=R->Stack<ObjectClass*>(0x34);
        auto attacker=source && (source->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None ?
            static_cast<TechnoClass*>(source) : nullptr;
        auto victim=static_cast<TechnoClass*>(object);
        if(alive(victim)) {
            int damage=*amount;
            if(anyBeforeDealDamage && alive(attacker))
                damage=adjustPendingDamage("onbeforedealdamage",attacker,victim,attacker,victim,damage);
            if(anyBeforeTakeDamage && damage>0)
                damage=adjustPendingDamage("onbeforetakedamage",victim,attacker,attacker,victim,damage);
            *amount=damage;
        }
        if(!*amount) return 0x5F545C;
    }
    R->EDX(R->Stack<int>(0x14));
    R->Stack(0x10,1);
    return 0x5F54A4;
}
DEFINE_HOOK(0x5F5505,SF_NativeDamageMaxHealth,6) {
    auto obj=R->ESI<ObjectClass*>();
    if(auto p=UnitHealth::find(obj->UniqueID)) {
        int damage=*R->EDI<int*>();
        // Subsequent native notifications compare the old and new display-scale fields.
        R->Stack(0x14,p->native);
        p->set(static_cast<TechnoClass*>(obj),std::max(0,R->EDX<int>()-damage));
        R->EAX(p->native);
        R->EDX(damage);
        return 0x5F550B;
    }
    return 0;
}
namespace { std::map<DWORD,int> healthBefore; }
DEFINE_HOOK(0x701DCC, SF_BeforeDamage, 7) {
    healthBefore[R->ESP()]=UnitHealth::current(R->ESI<TechnoClass*>());
    return 0;
}
DEFINE_HOOK(0x701DFF, SF_AfterDamage, 7) {
    auto i=healthBefore.find(R->ESP());
    if(i==healthBefore.end()) return 0;
    auto victim=R->ESI<TechnoClass*>(), attacker=R->Stack<TechnoClass*>(0xD4);
    int damage=i->second-std::max(UnitHealth::current(victim),0);
    healthBefore.erase(i);
    // Some engine attacks deliver damage directly without detonating a bullet.
    if(damage!=0 && !grantRules.empty()) {
        auto wh=R->Stack<WarheadTypeClass*>(0xD0);
        if(auto found=grantRules.find(wh);found!=grantRules.end())
            tryGrantUnit(victim,wh,found->second,attacker?attacker->Owner:R->Stack<HouseClass*>(0xE0));
    }
    if(R->EDI<int>()==static_cast<int>(DamageState::NowDead)) queueDeath(victim,attacker);
    if(damage>0) {
        capture("ondealdamage",attacker,victim,attacker,victim,damage);
        capture("ontakedamage",victim,attacker,attacker,victim,damage);
    }
    return 0;
}
// Each site reads ObjectTypeClass::Immune into CL. Leave the native type
// check intact for other objects; forced damage already bypasses this read.
DEFINE_HOOK_AGAIN(0x417F59,SF_ImmuneFlag,6) // aircraft attack cursor: target in EDI
DEFINE_HOOK_AGAIN(0x44751D,SF_ImmuneFlag,6) // building attack cursor: target in EBP
DEFINE_HOOK_AGAIN(0x51F16F,SF_ImmuneFlag,6) // infantry attack cursor: target in ESI
DEFINE_HOOK_AGAIN(0x740490,SF_ImmuneFlag,6) // vehicle attack cursor: target in EDI
DEFINE_HOOK_AGAIN(0x4423AD,SF_ImmuneFlag,6) // building-specific damage check: victim in ESI
DEFINE_HOOK(0x5F53CD,SF_ImmuneFlag,6) { // common damage check: victim in ESI
    if(immuneUnits.empty()) return 0;
    const auto site=R->Origin();
    ObjectClass* object=R->ESI<ObjectClass*>();
    if(site==0x417F59 || site==0x740490) object=R->EDI<ObjectClass*>();
    else if(site==0x44751D) object=R->EBP<ObjectClass*>();
    if(!immuneUnits.contains(object->UniqueID)) return 0;
    R->CL(1);
    return site+6;
}
// Stop ordinary mind-control projectiles before Phobos replaces the capture operation.
DEFINE_HOOK(0x46921F,SF_MindImmuneBullet,6) {
    auto bullet=R->ESI<BulletClass*>();
    auto target=bullet?bullet->Target:nullptr;
    if(target && (target->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None &&
       mindImmuneUnits.contains(static_cast<TechnoClass*>(target)->UniqueID)) return 0x469AA4;
    return 0;
}
// The engine and Phobos both expose capture eligibility through this entry.
DEFINE_HOOK(0x471C90,SF_MindImmuneCanCapture,6) {
    auto target=R->Stack<TechnoClass*>(4);
    if(target && mindImmuneUnits.contains(target->UniqueID)) {R->AL(0);return 0x471D39;}
    return 0;
}
DEFINE_HOOK(0x471D40,SF_MindImmuneCapture,7) {
    auto target=R->Stack<TechnoClass*>(4);
    if(target && mindImmuneUnits.contains(target->UniqueID)) {R->AL(0);return 0x471D5A;}
    return 0;
}
// Native Psychic Dominator selection; its damage is handled independently.
DEFINE_HOOK(0x53B270,SF_MindImmuneDominator,6) {
    auto target=R->ESI<TechnoClass*>();
    return target && mindImmuneUnits.contains(target->UniqueID)?0x53B364:0;
}
// Run the native mirage update for affected vehicles without changing their shared type.
DEFINE_HOOK(0x736486, SF_MirageUpdate, 6) {
    auto unit=R->ESI<UnitClass*>();
    return unit && mirageUnits.contains(unit->UniqueID)?0x73649A:0;
}
// The native update still checks movement, nearby detectors, firing and its own timers.
DEFINE_HOOK(0x7468FB, SF_MirageWhenStill, 6) {
    auto unit=R->ESI<UnitClass*>();
    return unit && mirageUnits.contains(unit->UniqueID)?0x74690B:0;
}
void clearCompletedVolleys() {
    if(volleyWeapons.empty()) return;
    unsigned frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    for(auto unit:TechnoClass::Array) if(unit) {
        auto it=volleyWeapons.find(unit->UniqueID);
        if(it!=volleyWeapons.end() && (!alive(unit) || (unit->CurrentBurstIndex==0 && it->second.frame!=frame))) volleyWeapons.erase(it);
    }
}
DEFINE_HOOK(0x55B4E1, SF_Frame, 5) { incomingDamage.clear();clearCompletedVolleys();UnitHealth::syncAll();tickBuildings(); refreshSpawnGrants(); expireWarheadGrants(); refreshAuras(); refreshTransportGrants(); reconcileAllStacks(); tickGains(); observeTransitions(); tickStates(); tickIntervals(); dispatch(); return 0; }
DEFINE_HOOK(0x6DD8B0, SF_MapAction, 6) {
    auto action=R->ECX<TActionClass*>();
    auto kind=static_cast<int>(action->ActionKind);
    if(kind!=9600 && kind!=9601) return 0;
    int target=action->Param4;
    auto house=R->Stack<HouseClass*>(4);
    if(kind==9601) {
        Research::ensureReady();
        auto preset=grantPresets.find(action->Param3);
        if(preset!=grantPresets.end()) grantFromTrigger(preset->second,target,house,R->Stack<ObjectClass*>(8),R->Stack<TriggerClass*>(12));
    } else {
        if(target!=0) house=HouseClass::Index_IsMP(target)?HouseClass::FindByPlayerAt(target):nullptr;
        if(eligibleHouse(house)) Research::award(house->ArrayIndex,action->Param3);
    }
    R->AL(1); return 0x6DD910;
}

// After Phobos' TeamClass_AI hook: EAX is the action and ECX points to the script node.
DEFINE_HOOK(0x6E944B, SF_TeamGrantPassives, 6) {
    if(R->EAX<int>()!=9601) return 0;
    auto team=R->ESI<TeamClass*>();
    auto node=R->ECX<ScriptActionNode*>();
    Research::ensureReady();
    auto preset=grantPresets.find(node->Argument);
    if(preset!=grantPresets.end())
        for(auto unit=team->FirstUnit;unit;unit=unit->NextTeamMember) grantFromPreset(unit,preset->second);
    return 0x6E94BA; // native "step completed" epilogue, including for unknown presets
}

// Private stream in the save container, independent of the game compression stream.
// No process address or unordered-container iteration order is persisted.
namespace {
constexpr wchar_t stateStreamName[]=L"Starfall.Passives";
constexpr DWORD passengerPassivesSaveTag=0x59504653; // SFPY
constexpr DWORD signalSaveTag=0x5A504653; // SFPZ
constexpr DWORD auraSaveTag=0x58504653; // SFPX
constexpr DWORD selfCostSaveTag=0x57504653; // SFPW
constexpr DWORD targetCostSaveTag=0x56504653; // SFPV
constexpr DWORD saveTag=0x31504653; // SFP1
struct Writer {
    vector<BYTE> bytes;
    void word(DWORD n) {
        const BYTE* p=reinterpret_cast<const BYTE*>(&n);
        bytes.insert(bytes.end(),p,p+sizeof(n));
    }
    void optionalInt(std::optional<int> value) {
        word(value.has_value());
        if(value) word(*value);
    }
    void real(double value) {
        auto bits=std::bit_cast<unsigned long long>(value);
        word(static_cast<DWORD>(bits));
        word(static_cast<DWORD>(bits>>32));
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
    std::optional<int> optionalInt() {
        DWORD present=word();
        if(present==1) return static_cast<int>(word());
        if(present!=0) ok=false;
        return std::nullopt;
    }
    double real() {
        unsigned long long bits=word();
        bits|=static_cast<unsigned long long>(word())<<32;
        auto value=std::bit_cast<double>(bits);
        if(!std::isfinite(value)) ok=false;
        return value;
    }
    double percent(bool fractional) {
        double value=fractional?real():static_cast<int>(word());
        if(value<1 || value>INT_MAX) ok=false;
        return value;
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
        w.word(e.damage); w.word(e.hp); w.word(e.maxHp); w.word(e.state);
        w.word(e.hasImpact);w.word(e.impact.X);w.word(e.impact.Y);w.word(e.impact.Z);
        w.word(e.ammo);w.word(e.passengers);w.word(e.owner);w.word(e.origin.X);w.word(e.origin.Y);w.word(e.origin.Z);
        w.word(static_cast<DWORD>(e.snapshot.size()));for(const auto& id:e.snapshot) w.str(id);
        w.word(e.attackerOwner);w.word(e.victimOwner);
        w.real(e.distance);
        w.word(static_cast<DWORD>(e.stackSnapshot.size()));
        for(const auto& [skill,value]:e.stackSnapshot) {w.str(skill);w.word(value);}
        w.word(e.targetHp);w.word(e.targetMaxHp);
        w.optionalInt(e.targetCost);
        w.optionalInt(e.selfCost);
        if(e.trigger=="onpassengerconsumed") {
            w.word(e.passengerPassives?static_cast<DWORD>(e.passengerPassives->size()):0);
            if(e.passengerPassives)
                for(const auto& [id,stacks]:*e.passengerPassives) {w.str(id);w.word(stacks);}
        }
        w.str(e.senderKind);
        if(e.trigger=="onsignal") {
            w.str(e.signal);w.str(e.senderRelation);w.word(e.senderHp);w.word(e.senderMaxHp);w.optionalInt(e.senderCost);
            w.word(e.signalSenderPosition.X);w.word(e.signalSenderPosition.Y);w.word(e.signalSenderPosition.Z);
            w.real(e.signalValue);w.word(e.signalDepth);w.word(e.signalChain?e.signalChain->id:0);w.word(e.signalChain?e.signalChain->remaining:0);
        }
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
    w.word(static_cast<DWORD>(applied.size()));
    for(const auto& [id,a]:applied) {
        w.word(id);
        for(int k=0;k<3;++k) {
            w.word(a.active[k]);
            w.real(a.base[k]);
            w.real(a.last[k]);
            w.real(a.percent[k]);
        }
    }
    w.word(static_cast<DWORD>(stateTimers.size()));
    for(const auto& [key,timer]:stateTimers) { w.word(key.first); w.str(key.second); w.word(timer.start); }
    w.word(static_cast<DWORD>(removedPassives.size()));
    for(const auto& [id,skills]:removedPassives) {
        w.word(id);w.word(static_cast<DWORD>(skills.size()));for(const auto& skill:skills) w.str(skill);
    }
    w.word(static_cast<DWORD>(passiveBullets.size()));for(auto id:passiveBullets) w.word(id);
    w.word(static_cast<DWORD>(previousState.size()));
    for(auto [id,v]:previousState){w.word(id);w.word(v.rank);w.word(v.deployed);}
    w.word(static_cast<DWORD>(reportedDeaths.size()));for(auto id:reportedDeaths) w.word(id);
    w.word(static_cast<DWORD>(periodic.size()));
    for(const auto& [key,timer]:periodic){w.word(key.first); w.str(key.second); w.word(timer.start);}
    w.word(static_cast<DWORD>(nearbySamples.size()));
    for(const auto& [key,sample]:nearbySamples) {
        w.word(key.first);w.str(key.second);w.word(sample.next);w.word(sample.owner);w.word(sample.matches);
    }
    w.word(static_cast<DWORD>(Research::unlockOrder.size()));
    for(const auto& [house,ids]:Research::unlockOrder) {
        w.word(house);w.word(static_cast<DWORD>(ids.size()));for(const auto& id:ids)w.str(id);
    }
    w.word(static_cast<DWORD>(attachedAnims.size()));
    for(const auto& [key,anim]:attachedAnims) { w.word(key.first); w.str(key.second); w.word(anim.id); }
    w.word(static_cast<DWORD>(activeAnimStates.size()));
    for(const auto& key:activeAnimStates) { w.word(key.first); w.str(key.second); }
    w.word(static_cast<DWORD>(Research::buildingRewards.size()));
    for(const auto& [id,state]:Research::buildingRewards) {w.word(id);w.word(state.owner);w.word(state.authorized);w.word(state.claimed);}
    w.word(static_cast<DWORD>(VisualScale::base.size()));
    for(const auto& [id,size]:VisualScale::base) {w.word(id);w.word(static_cast<DWORD>(size));w.word(static_cast<DWORD>(size>>32));}
    w.word(static_cast<DWORD>(mirageUnits.size()));for(auto id:mirageUnits) w.word(id);
    w.word(static_cast<DWORD>(warheadGrants.size()));
    for(const auto& [unit,group]:warheadGrants) {
        w.word(unit);w.word(static_cast<DWORD>(group.skills.size()));
        for(const auto& [id,time]:group.skills) {w.str(id);w.word(time.start);w.word(time.duration);}
    }
    for(const auto* values:{&projectileBase,&VisualScale::projectiles}) {
        w.word(static_cast<DWORD>(values->size()));
        for(auto [id,size]:*values) {w.word(id);w.word(size);}
    }
    for(const auto* values:{&rangeBase,&fireRateBase}) {
        w.word(static_cast<DWORD>(values->size()));
        for(auto [id,percent]:*values) {w.word(id);w.real(percent);}
    }
    w.word(static_cast<DWORD>(immuneUnits.size()));
    for(auto id:immuneUnits) w.word(id);
    w.word(static_cast<DWORD>(Research::refreshedSlots.size()));
    for(auto [house,mask]:Research::refreshedSlots) {w.word(house);w.word(mask);}
    w.word(Research::globalIncome.initialized?1:0);
    w.word(Research::globalIncome.lastFrame);
    w.word(static_cast<DWORD>(UnitHealth::pools.size()));
    for(const auto& [id,p]:UnitHealth::pools) {
        w.word(id);w.word(p.base);w.word(p.maximum);w.word(p.current);w.word(p.native);
    }
    w.word(static_cast<DWORD>(gainSeen.size()));
    for(const auto& [unit,ids]:gainSeen) {
        w.word(unit);w.word(static_cast<DWORD>(ids.size()));
        for(const auto& id:ids) w.str(id);
    }
    w.word(0); // Reserved legacy shield-grant records, kept for save layout compatibility.
    w.word(static_cast<DWORD>(VisualScale::explosions.size()));
    for(auto [id,size]:VisualScale::explosions) {w.word(id);w.word(size);}
    w.word(static_cast<DWORD>(spawnGrants.size()));
    for(const auto& [id,grant]:spawnGrants) {
        w.word(id);w.word(grant.parent);w.word(static_cast<DWORD>(grant.ids.size()));
        for(const auto& skill:grant.ids) w.str(skill);
    }
    w.word(static_cast<DWORD>(triggerTotals.size()));
    for(const auto& [unit,entries]:triggerTotals) {
        w.word(unit);w.word(static_cast<DWORD>(entries.size()));
        for(const auto& [skill,total]:entries) {w.str(skill);w.word(total);}
    }
    w.word(static_cast<DWORD>(mindImmuneUnits.size()));
    for(auto id:mindImmuneUnits) w.word(id);
    w.word(static_cast<DWORD>(volleyWeapons.size()));
    for(const auto& [unit,choice]:volleyWeapons) {
        w.word(unit);w.word(choice.slot);w.str(choice.replacement?lower(choice.replacement->ID):string{});w.word(choice.frame);
    }
    w.word(static_cast<DWORD>(passiveStacks.size()));
    for(const auto& [unit,entries]:passiveStacks) {
        w.word(unit);w.word(static_cast<DWORD>(entries.size()));
        for(const auto& [skill,value]:entries) {w.str(skill);w.word(value);}
    }
    // Keep the sampled membership between pulses. Re-scanning here or on load
    // would change gains for units which just crossed the edge of an aura.
    w.word(discoverAuras);
    w.word(static_cast<DWORD>(auraSources.size()));
    for(const auto& [key,source]:auraSources) {
        w.word(key.first);w.str(key.second);w.word(source.owner);
        w.word(static_cast<DWORD>(source.grants.size()));for(const auto& id:source.grants) w.str(id);
        w.word(static_cast<DWORD>(source.targets.size()));for(auto id:source.targets) w.word(id);
    }
    w.word(static_cast<DWORD>(auraLosses.size()));
    for(const auto& [target,ids]:auraLosses) {
        w.word(target);w.word(static_cast<DWORD>(ids.size()));for(const auto& id:ids) w.str(id);
    }
    DWORD header[]={signalSaveTag,static_cast<DWORD>(w.bytes.size())};
    ULONG written=0;
    return SUCCEEDED(stream->Write(header,sizeof(header),&written)) && written==sizeof(header)
        && SUCCEEDED(stream->Write(w.bytes.data(),header[1],&written)) && written==header[1];
}
void loadState(IStream* stream) {
    reset();
    // Existing saves predate global income: never issue the initial grant again.
    Research::globalIncome.initialized=true;
    Research::globalIncome.lastFrame=static_cast<unsigned>(Unsorted::CurrentFrame);
    if(!stream) return;
    DWORD header[2]{};
    ULONG got=0;
    if(FAILED(stream->Read(header,sizeof(header),&got)) || got!=sizeof(header) ||
       (header[0]&0xFFFFFF)!=(saveTag&0xFFFFFF) || header[0]<saveTag || header[0]>signalSaveTag) return;
    if(header[1]>64*1024*1024) return;
    vector<BYTE> data(header[1]);
    if(FAILED(stream->Read(data.data(),header[1],&got)) || got!=header[1]) return;
    Reader r{data};
    Counts restored;
    vector<Event> events;
    std::map<DWORD,std::shared_ptr<SignalChain>> signalChains;
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
        if(header[0]>=0x35504653) e.state=static_cast<int>(r.word());
        if(header[0]>=0x37504653) {
            e.hasImpact=r.word()!=0;e.impact={static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
        }
        if(header[0]>=0x38504653) {
            e.ammo=r.word();e.passengers=r.word();e.owner=r.word();
            e.origin={static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
            for(DWORD n=r.word(); r.ok && n--;) e.snapshot.push_back(r.str());
        }
        if(header[0]>=0x3C504653) {
            e.attackerOwner=static_cast<int>(r.word());e.victimOwner=static_cast<int>(r.word());
            if(e.attackerOwner< -1 || e.attackerOwner>127 || e.victimOwner< -1 || e.victimOwner>127) r.ok=false;
        }
        if(header[0]>=0x51504653) {e.distance=r.real();if(e.distance<0 && e.distance!=-1) r.ok=false;}
        if(header[0]>=0x54504653) for(DWORD k=r.word();r.ok && k--;) {
            auto skill=r.str();int value=static_cast<int>(r.word());
            if(skill.empty() || value<0 || !e.stackSnapshot.emplace(skill,value).second) r.ok=false;
        }
        if(header[0]>=0x55504653) {
            e.targetHp=static_cast<int>(r.word());e.targetMaxHp=static_cast<int>(r.word());
            if(e.targetHp<0 || e.targetMaxHp<0 || e.targetHp>e.targetMaxHp) r.ok=false;
        }
        if(header[0]>=targetCostSaveTag) e.targetCost=r.optionalInt();
        if(header[0]>=selfCostSaveTag) e.selfCost=r.optionalInt();
        if(e.trigger=="onpassengerconsumed") {
            e.consumedTarget=e.victim;
            if(header[0]>=passengerPassivesSaveTag) {
                e.passengerPassives=std::make_shared<PassengerPassives>();
                for(DWORD k=r.word();r.ok && k--;) {
                    auto id=r.str();int stacks=static_cast<int>(r.word());
                    if(id.empty() || stacks<0 || !e.passengerPassives->emplace(id,stacks).second) r.ok=false;
                }
            }
        }
        if(header[0]>=signalSaveTag) {
            e.senderKind=r.str();
            if(e.trigger=="onsignal") {
                e.signal=r.str();e.senderRelation=r.str();e.senderHp=static_cast<int>(r.word());e.senderMaxHp=static_cast<int>(r.word());e.senderCost=r.optionalInt();
                e.signalSenderPosition={static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
                e.signalValue=r.real();e.signalDepth=r.word();DWORD id=r.word(),remaining=r.word();
                if(e.signal.empty() || e.senderKind.empty() || e.senderRelation.empty() || e.senderHp<0 || e.senderMaxHp<=0 || e.senderHp>e.senderMaxHp ||
                   !id || remaining>1024 || e.signalDepth<1 || e.signalDepth>16) r.ok=false;
                auto [it,created]=signalChains.try_emplace(id,std::make_shared<SignalChain>(SignalChain{id,remaining}));
                if(!created && it->second->remaining!=remaining) r.ok=false;
                e.signalChain=it->second;
                if(id>=nextSignalChain) nextSignalChain=id+1;
            }
        }
        events.push_back(std::move(e));
    }
    Research::Unlocks upgrades;
    if(r.ok && r.pos<data.size()) {
        DWORD n=r.word();
        if(n>256) r.ok=false;
        while(r.ok && n--) {
            int house=static_cast<int>(r.word());
            if(house<0 || house>127 || upgrades.contains(house)) { r.ok=false; break; }
            DWORD size=header[0]>=0x34504653?r.word():1;
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
    if(header[0]>=0x34504653) {
        for(DWORD n=r.word();r.ok && n--;){
            int house=r.word(), amount=r.word();
            if(house<0 || house>127 || amount<0 || !points.emplace(house,amount).second) r.ok=false;
        }
        for(DWORD n=r.word();r.ok && n--;){
            DWORD id=r.word(); int owner=r.word(), elapsed=r.word();
            if(owner<0 || owner>127 || elapsed<0 || elapsed==INT_MAX || !producers.emplace(id,Research::Producer{owner,elapsed}).second) r.ok=false;
        }
    }
    decltype(applied) states;
    if(header[0]>=0x35504653) {
        for(DWORD n=r.word();r.ok && n--;) {
            DWORD id=r.word(); Applied a;
            for(int k=0;k<3;++k) {
                DWORD active=r.word(); if(active>1) r.ok=false;
                a.active[k]=active!=0;
                a.base[k]=r.real();
                a.last[k]=r.real();
                if(header[0]>=0x42504653) a.percent[k]=r.percent(header[0]>=0x48504653);
            }
            if(!states.emplace(id,a).second) r.ok=false;
        }
    }
    decltype(stateTimers) timers;
    if(header[0]>=0x36504653) {
        for(DWORD n=r.word();r.ok && n--;) {
            auto id=r.word(); auto skill=r.str(); auto start=r.word();
            if(skill.empty() || !timers.emplace(TimerKey{id,skill},StateTimer{start,false}).second) r.ok=false;
        }
    }
    decltype(removedPassives) removed; decltype(passiveBullets) bullets;
    if(header[0]>=0x37504653) {
        for(DWORD n=r.word(); r.ok && n--;) {
            DWORD id=r.word(),size=r.word(); auto& ids=removed[id];
            while(r.ok && size--) { auto key=r.str(); if(key.empty() || !ids.insert(key).second) r.ok=false; }
        }
        for(DWORD n=r.word();r.ok && n--;) if(!bullets.insert(r.word()).second) r.ok=false;
    }
    decltype(previousState) statesBefore;decltype(reportedDeaths) deaths;
    if(header[0]>=0x38504653) {
        for(DWORD n=r.word();r.ok && n--;) {DWORD id=r.word();int rank=r.word(); auto flag=r.word();
            if(rank<0 || rank>2 || flag>1 || !statesBefore.emplace(id,Transition{rank,flag!=0}).second) r.ok=false;}
        for(DWORD n=r.word();r.ok && n--;) if(!deaths.insert(r.word()).second) r.ok=false;
    }
    decltype(periodic) clocks;
    if(header[0]>=0x39504653) {
        for(DWORD n=r.word();r.ok && n--;) {
            DWORD id=r.word();auto skill=r.str();DWORD start=r.word();
            if(skill.empty() || !clocks.emplace(TimerKey{id,skill},PeriodicClock{start,false}).second) r.ok=false;
        }
    }
    decltype(nearbySamples) samples;
    if(header[0]>=0x3A504653) {
        for(DWORD n=r.word();r.ok && n--;) {
            auto id=r.word();auto skill=r.str();unsigned next=r.word();int house=r.word();DWORD match=r.word();
            if(skill.empty() || match>1 || house< -1 || house>127 || !samples.emplace(TimerKey{id,skill},NearbySample{next,house,match!=0,false}).second) r.ok=false;
        }
    }
    decltype(Research::unlockOrder) order;
    if(header[0]>=0x3B504653) {
        for(DWORD n=r.word();r.ok && n--;) {
            int house=static_cast<int>(r.word());DWORD size=r.word();
            if(house<0 || house>127 || order.contains(house) || size>10000) {r.ok=false;break;}
            auto& ids=order[house];
            while(r.ok && size--) {
                auto id=r.str();
                if(id.empty() || !upgrades[house].contains(id) || includes(ids,id)) r.ok=false;
                else ids.push_back(std::move(id));
            }
        }
    }
    decltype(attachedAnims) attachments;
    decltype(activeAnimStates) animStates;
    if(header[0]>=0x3D504653) {
        std::set<DWORD> used;
        for(DWORD n=r.word();r.ok && n--;) {
            TimerKey key{r.word(),r.str()}; DWORD id=r.word();
            if(key.second.empty() || !used.insert(id).second || !attachments.emplace(key,AttachedAnim{id}).second) r.ok=false;
        }
        for(DWORD n=r.word();r.ok && n--;) {
            TimerKey key{r.word(),r.str()};
            if(key.second.empty() || !animStates.insert(key).second) r.ok=false;
        }
    }
    decltype(Research::buildingRewards) rewards;
    if(header[0]>=0x3E504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();int owner=static_cast<int>(r.word()),authorized=static_cast<int>(r.word());DWORD claimed=r.word();
        if(owner<0 || owner>127 || authorized< -1 || authorized>127 || claimed>1 || !rewards.emplace(id,Research::BuildingReward{owner,authorized,claimed!=0}).second) r.ok=false;
    }
    decltype(VisualScale::base) sizes;
    if(header[0]>=0x3F504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();uint64_t size=r.word();
        if(header[0]>=0x41504653) size|=static_cast<uint64_t>(r.word())<<32;
        if(!size || size>INT64_MAX || !sizes.emplace(id,static_cast<int64_t>(size)).second) r.ok=false;
    }
    decltype(mirageUnits) mirages;
    if(header[0]>=0x40504653) for(DWORD n=r.word();r.ok && n--;) if(!mirages.insert(r.word()).second) r.ok=false;
    decltype(warheadGrants) grants;
    if(header[0]>=0x43504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD unit=r.word(),count=r.word();
        if(!count || grants.contains(unit)) {r.ok=false;break;}
        auto& group=grants[unit];
        for(DWORD i=0;i<count && r.ok;++i) {
            auto id=r.str();unsigned start=r.word();int duration=static_cast<int>(r.word());
            if(id.empty() || (duration!=-1 && duration<=0) ||
               !group.skills.emplace(id,GrantTime{start,duration}).second) r.ok=false;
        }
    }
    std::map<DWORD,int> launchSizes,shotSizes;
    if(header[0]>=0x44504653) for(auto* values:{&launchSizes,&shotSizes}) {
        for(DWORD n=r.word();r.ok && n--;) {
            DWORD id=r.word();
            int size=static_cast<int>(r.word());
            if(size<=0 || !values->emplace(id,size).second) r.ok=false;
        }
    }
    decltype(rangeBase) weaponRanges,fireRates;
    if(header[0]>=0x45504653) for(auto* values:{&weaponRanges,&fireRates}) {
        if(values==&fireRates && header[0]<0x46504653) break;
        for(DWORD n=r.word();r.ok && n--;) {
            DWORD id=r.word();
            double value=r.percent(header[0]>=0x48504653);
            if(!values->emplace(id,value).second) r.ok=false;
        }
    }
    decltype(immuneUnits) immunes;
    if(header[0]>=0x47504653) for(DWORD n=r.word();r.ok && n--;) {
        if(!immunes.insert(r.word()).second) r.ok=false;
    }
    decltype(Research::refreshedSlots) refreshes;
    if(header[0]>=0x49504653) {
        DWORD houses=r.word();
        if(houses>128) r.ok=false;
        while(r.ok && houses--) {
            DWORD house=r.word(),mask=r.word();
            if(house>127 || !mask || mask>=(1u<<Research::choices) || !refreshes.emplace(house,mask).second) r.ok=false;
        }
    }
    DWORD incomeInitialized=1,incomeLast=static_cast<DWORD>(Unsorted::CurrentFrame);
    if(header[0]>=0x4A504653) {incomeInitialized=r.word();incomeLast=r.word();if(incomeInitialized>1) r.ok=false;}
    decltype(UnitHealth::pools) healthPools;
    if(header[0]>=0x4B504653) for(DWORD n=r.word();r.ok && n--;) {
        auto id=r.word();
        UnitHealth::Pool p{static_cast<int>(r.word()),static_cast<int>(r.word()),
            static_cast<int>(r.word()),static_cast<int>(r.word())};
        if(p.base<1 || p.maximum<1 || p.current<0 || p.current>p.maximum ||
           p.native!=p.project(p.current) || !healthPools.emplace(id,p).second) r.ok=false;
    }
    decltype(gainSeen) acquired;
    if(header[0]>=0x4C504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD unit=r.word(),count=r.word();
        if(!count || acquired.contains(unit)) {r.ok=false;break;}
        auto& ids=acquired[unit];
        while(r.ok && count--) {
            auto id=r.str();
            if(id.empty() || includes(ids,id)) r.ok=false;
            else ids.push_back(std::move(id));
        }
    }
    std::set<DWORD> legacyShieldUnits;
    if(header[0]>=0x4D504653) for(DWORD n=r.word();r.ok && n--;) {
        auto unit=r.word();
        auto type=r.str();r.word();int duration=static_cast<int>(r.word());
        if(type.empty() || type.size()>31 || (duration!=-1 && duration<=0) ||
           !legacyShieldUnits.insert(unit).second) r.ok=false;
    }
    decltype(VisualScale::explosions) explosions;
    if(header[0]>=0x4E504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();
        int size=static_cast<int>(r.word());
        if(size<=0 || !explosions.emplace(id,size).second) r.ok=false;
    }
    decltype(spawnGrants) restoredSpawns;
    if(header[0]>=0x4F504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();
        SpawnGrant grant{r.word(),{}};
        for(DWORD k=r.word();r.ok && k--;) {
            auto skill=r.str();
            if(skill.empty() || includes(grant.ids,skill)) r.ok=false;
            else grant.ids.push_back(std::move(skill));
        }
        if(grant.ids.empty() || !restoredSpawns.emplace(id,std::move(grant)).second) r.ok=false;
    }
    decltype(triggerTotals) restoredTotals;
    if(header[0]>=0x50504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD unit=r.word(),size=r.word();
        if(restoredTotals.contains(unit)) {r.ok=false;break;}
        auto& entries=restoredTotals[unit];
        while(r.ok && size--) {
            auto skill=r.str();auto total=r.word();
            if(skill.empty() || !total || total>INT_MAX || !entries.emplace(skill,total).second) r.ok=false;
        }
    }
    decltype(mindImmuneUnits) restoredMindImmunes;
    if(header[0]>=0x52504653) for(DWORD n=r.word();r.ok && n--;) {
        if(!restoredMindImmunes.insert(r.word()).second) r.ok=false;
    }
    decltype(volleyWeapons) restoredVolleys;
    if(header[0]>=0x53504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();int slot=static_cast<int>(r.word());auto name=r.str();unsigned frame=r.word();
        auto weapon=name.empty()?nullptr:WeaponTypeClass::Find(name.c_str());
        if(!name.empty() && !weapon) std::ofstream("Starfall.log",std::ios::app)<<"Starfall: missing saved volley weapon "<<name<<", using original\n";
        if(slot<0 || slot>1 || !restoredVolleys.emplace(id,VolleyWeapon{slot,weapon,frame}).second) r.ok=false;
    }
    decltype(passiveStacks) restoredStacks;
    if(header[0]>=0x54504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD unit=r.word(),size=r.word();
        if(!size || restoredStacks.contains(unit)) {r.ok=false;break;}
        auto& entries=restoredStacks[unit];
        while(r.ok && size--) {
            auto skill=r.str();int value=static_cast<int>(r.word());
            if(skill.empty() || value<=0 || !entries.emplace(skill,value).second) r.ok=false;
        }
    }
    decltype(auraSources) restoredAuras;
    decltype(auraLosses) restoredLosses;
    bool discoverAfterLoad=true;
    if(header[0]>=auraSaveTag && r.ok) {
        auto discovery=r.word();
        if(discovery>1) r.ok=false;
        discoverAfterLoad=discovery!=0;
        for(DWORD n=r.word();r.ok && n--;) {
            DWORD emitter=r.word();auto id=r.str();
            auto [it,added]=restoredAuras.try_emplace({emitter,id});
            auto& source=it->second;
            source.owner=static_cast<int>(r.word());
            if(!added || id.empty() || source.owner< -1 || source.owner>127) r.ok=false;
            for(DWORD k=r.word();r.ok && k--;) {
                auto skill=r.str();
                if(skill.empty() || includes(source.grants,skill)) r.ok=false;
                else source.grants.push_back(std::move(skill));
            }
            for(DWORD k=r.word();r.ok && k--;)
                if(source.grants.empty() || !source.targets.insert(r.word()).second) r.ok=false;
        }
        for(DWORD n=r.word();r.ok && n--;) {
            auto target=r.word(),size=r.word();
            if(!size || restoredLosses.contains(target)) {r.ok=false;break;}
            auto& ids=restoredLosses[target];
            while(r.ok && size--) {
                auto id=r.str();if(id.empty() || !ids.insert(id).second) r.ok=false;
            }
        }
    }
    if(r.ok && r.pos==data.size()) {
        auraSources.swap(restoredAuras);auraLosses.swap(restoredLosses);discoverAuras=discoverAfterLoad;
        if(!auraSources.empty()) {
            for(auto unit:TechnoClass::Array) if(unit) {
                auto it=auraSources.lower_bound({unit->UniqueID,{}});
                while(it!=auraSources.end() && it->first.first==unit->UniqueID) {
                    it->second.unit=unit;
                    ++it;
                }
            }
            for(const auto& entry:auraSources) for(auto target:entry.second.targets)
                for(const auto& id:entry.second.grants) ++auraGrants[target][id];
        }
    }
    if(r.ok && r.pos==data.size()) {triggerTotals.swap(restoredTotals);mindImmuneUnits.swap(restoredMindImmunes);volleyWeapons.swap(restoredVolleys);passiveStacks.swap(restoredStacks);}
    if(r.ok && r.pos==data.size()) { spawnGrants.swap(restoredSpawns);VisualScale::explosions.swap(explosions);Research::clearOfferCache();gainSeen.swap(acquired);UnitHealth::pools.swap(healthPools);Research::globalIncome.initialized=incomeInitialized!=0;Research::globalIncome.lastFrame=incomeLast; Research::refreshedSlots.swap(refreshes); immuneUnits.swap(immunes);fireRateBase.swap(fireRates);rangeBase.swap(weaponRanges);projectileBase.swap(launchSizes);VisualScale::projectiles.swap(shotSizes);warheadGrants.swap(grants); rebindGrants=!warheadGrants.empty(); mirageUnits.swap(mirages); VisualScale::base.swap(sizes); Research::buildingRewards.swap(rewards); attachedAnims.swap(attachments); activeAnimStates.swap(animStates); Research::unlockOrder.swap(order); nearbySamples.swap(samples); periodic.swap(clocks); previousState.swap(statesBefore);reportedDeaths.swap(deaths);removedPassives.swap(removed);passiveBullets.swap(bullets);stateTimers.swap(timers); applied.swap(states); Research::points.swap(points); Research::producers.swap(producers); Research::savedOffers.swap(offers); counts.swap(restored); pending.swap(events); Research::unlocked.swap(upgrades); }
    if(r.ok && r.pos==data.size()) {
        VisualScale::attachedAnims.clear();
        for(const auto& [key,anim]:attachedAnims) VisualScale::attachedAnims[anim.id]=key.first;
    }
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
        // Per-building multipliers are derived from the restored passives and timers.
        for(auto house:HouseClass::Array) if(house) house->RecheckPower=true;
    } else {
        reset();
        Research::globalIncome.initialized=true;
        Research::globalIncome.lastFrame=static_cast<unsigned>(Unsorted::CurrentFrame);
    }
    return 0;
}
SYRINGE_HANDSHAKE(info) {
    if(!info || info->cbSize<sizeof(SyringeHandshakeInfo)) return E_INVALIDARG;
    if(info->exeCRC!=0x54CC0A13) return E_FAIL;
    if(info->Message && info->cchMessage>0)
        _snprintf_s(info->Message,info->cchMessage,_TRUNCATE,"Starfall Lab 0.35 - spawn passives");
    return S_OK;
}

