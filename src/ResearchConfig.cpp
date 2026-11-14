#include "ResearchInternal.h"
#include "ConfigText.h"
#include <CCINIClass.h>
#include <TechnoTypeClass.h>
#include <fstream>

using std::string;
using std::vector;
using namespace ConfigText;
using namespace Research::Detail;
namespace {
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
} // namespace

void Research::readConfiguration(const char* configPath) {
    Research::clearPresentationCaches();
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
        "rarityweight.common","rarityweight.rare","rarityweight.epic","rarityweight.insane"});
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
    constexpr std::array<const char*,Research::rarityCount> weightKeys={"RarityWeight.Common","RarityWeight.Rare","RarityWeight.Epic","RarityWeight.Insane"};
    const auto& defaultWeights=Research::defaultRarityWeights;
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
            rarity=="epic"?Research::Rarity::Epic:rarity=="insane"?Research::Rarity::Insane:Research::Rarity::Invalid;
        if(o.rarity==Research::Rarity::Invalid) warn(id,"Rarity","应为 Common、Rare、Epic 或 Insane，项目无法抽取");
        o.types=split(field(id,"TargetTypes",o.types.empty()?"":"MTNK"));
        auto listValue=field(id,"Effects");
        bool hasList=fields.contains(id) && fields[id].contains("effects");
        if(auto* ini=CCINIClass::INI_Rules;ini && ini->GetSection(rulesSection(id))) {
            for(int i=0;i<ini->GetKeyCount(rulesSection(id));++i)
                if(!_stricmp(ini->GetKeyName(rulesSection(id),i),"Effects")) {hasList=true;break;}
        }
        static const std::set<string> projectKeys={"name","summary","detail","icon","countries","prerequisites","rarity","effects","effect",
            "costmodifier","category","credits","technology","fromtype","totype","passive","targettypes"};
        if(hasList && !Runtime::hasPassive(id) && !TechnoTypeClass::Find(id.c_str())) warnUnknown(id,projectKeys);
        auto auditEffect=[&](const string& sec,const Research::EffectSpec& spec) {
            if(!Runtime::hasPassive(sec) && !TechnoTypeClass::Find(sec.c_str())) warnUnknown(sec,projectKeys);
            if(spec.effect==E::Passive) for(const auto& type:spec.types)
                if(type!="*" && !type.starts_with('@') && !TechnoTypeClass::Find(type.c_str()))
                    warn(sec,"TargetTypes","未注册的单位类型 ["+type+"] 无法获得此被动");
            validateEffect(spec,[&](const char* key,const string& reason) {
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
