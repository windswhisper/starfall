#include "ResearchInternal.h"
#include "BuildingResearch.h"
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
        for(const auto& id:option.excludes)
            if(!index.contains(id)) warn(option.id,"Excludes","未注册科研项目 ["+id+"]，忽略此互斥项");
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
bool compileDetail(Research::Option& option) {
    const auto& text=option.detail;
    bool valid=true;
    for(size_t start=text.find(L"$(");start!=text.npos;) {
        auto end=ConfigText::formulaEnd(text,start);
        auto& part=option.detailFormulas.emplace_back(start,std::min(end,text.size()),HealthFormula{});
        const std::wstring_view expression(text.data()+start,part.end-start);
        string ascii;
        for(const auto c:expression) ascii.push_back(c<=127?static_cast<char>(c):'?');
        if(end==text.npos || !part.expression.compile(lower(ascii)) ||
            !part.expression.validForResearch() || !std::all_of(part.expression.counterNames().begin(),
                part.expression.counterNames().end(),[](const string& key){return Research::knownResearchVariable(key);})) {
            part.expression={};valid=false;
        }
        start=text.find(L"$(",part.end);
    }
    return valid;
}
} // namespace

void Research::readConfiguration(const char* configPath,void (*loadPassives)()) {
    BuildingResearch::clearTypeCaches();
    Research::clearPresentationCaches();
    using Research::Option;
    const Option defaults[]={
        {"sf_repair","sf_researchrepair",{},{"mtnk"},L"战场维修",L"击杀时修复自身",L"击杀敌人时，额外恢复 {amount} 点生命"},
        {"sf_drain","sf_lifesteal",{},{"mtnk"},L"伤害吸血",L"造成伤害时吸取生命",L"按造成的实际伤害恢复生命"},
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
    auto hasField=[&](const string& sec,const char* key) {
        auto it=fields.find(sec);
        if(it!=fields.end() && it->second.contains(lower(key))) return true;
        if(auto ini=CCINIClass::INI_Rules;ini && ini->GetSection(rulesSection(sec)))
            for(int i=0;i<ini->GetKeyCount(rulesSection(sec));++i)
                if(!_stricmp(ini->GetKeyName(rulesSection(sec),i),key)) return true;
        return false;
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
    warnUnknown("starfall.research",{"enabled","allowcardrefresh","allowairesearch","showavailableanimation","sharedraritysequence","announceselection","title","initialpoints","periodicpoints","periodicintervalseconds",
        "repeatable","mixedraritypool","raritycolor.common","raritycolor.rare","raritycolor.epic","raritycolor.insane",
        "rarityweight.common","rarityweight.rare","rarityweight.epic","rarityweight.insane",
        "rarityname.common","rarityname.rare","rarityname.epic","rarityname.insane"});
    warnUnknown("starfall.researchsounds",{"button","cards","refresh","select","available"});
    auto enabled=lower(field("starfall.research","Enabled","yes"));
    Research::enabled=enabled!="no" && enabled!="false" && enabled!="0";
    if(!Research::enabled) {
        Research::expanded=false;
        Research::rewardPanel=0;
        Research::pressed=Research::pressedOption=-1;
    }
    Research::options.clear();
    auto readRepeatable=[&](const string& section,bool fallback) {
        auto value=lower(field(section,"Repeatable",fallback?"yes":"no"));
        if(includes({"yes","true","1"},value)) return true;
        if(includes({"no","false","0"},value)) return false;
        warn(section,"Repeatable",string("无效开关值，按 ")+(fallback?"yes":"no")+" 处理");
        return fallback;
    };
    const bool defaultRepeatable=readRepeatable("starfall.research",false);
    constexpr std::array<const char*,5> soundKeys={"Button","Cards","Refresh","Select","Available"};
    for(size_t i=0;i<soundKeys.size();++i) Research::soundFiles[i]=field("starfall.researchsounds",soundKeys[i]);
    Research::freeRewardSound=field("starfall.researchsounds","Available","builtin:available");
    auto refresh=lower(trim(field("starfall.research","AllowCardRefresh","yes")));
    Research::allowCardRefresh=refresh=="yes" || refresh=="true" || refresh=="1";
    auto ai=lower(trim(field("starfall.research","AllowAIResearch","no")));
    Research::allowAIResearch=includes({"yes","true","1"},ai);
    if(!Research::allowAIResearch && !includes({"no","false","0"},ai))
        warn("starfall.research","AllowAIResearch","无效开关值，按 no 处理");
    auto shared=lower(trim(field("starfall.research","SharedRaritySequence","no")));
    Research::sharedRaritySequence=includes({"yes","true","1"},shared);
    if(!Research::sharedRaritySequence && !includes({"no","false","0"},shared))
        warn("starfall.research","SharedRaritySequence","无效开关值，按 no 处理");
    auto mixed=lower(field("starfall.research","MixedRarityPool","no"));
    mixedRarityPool=includes({"yes","true","1"},mixed);
    if(!mixedRarityPool && !includes({"no","false","0"},mixed))
        warn("starfall.research","MixedRarityPool","无效开关值，按 no 处理");
    if(mixedRarityPool && sharedRaritySequence)
        warn("starfall.research","SharedRaritySequence","混池模式按项目抽取，优先使用 MixedRarityPool");
    auto announce=lower(trim(field("starfall.research","AnnounceSelection","yes")));
    Research::announceSelection=!includes({"no","false","0"},announce);
    auto animation=lower(trim(field("starfall.research","ShowAvailableAnimation","yes")));
    Research::showAvailableAnimation=!includes({"no","false","0"},animation);
    for(const auto key:{"Enabled","AllowCardRefresh","AnnounceSelection","ShowAvailableAnimation"}) {
        auto value=lower(field("starfall.research",key,"yes"));
        if(!includes({"yes","true","1","no","false","0"},value))
            warn("starfall.research",key,string("无效开关值，当前按 ")+(string(key)=="AllowCardRefresh"?"no":"yes")+" 处理");
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
    constexpr std::array<const char*,Research::rarityCount> nameKeys={"RarityName.Common","RarityName.Rare","RarityName.Epic","RarityName.Insane"};
    const auto& defaultWeights=Research::defaultRarityWeights;
    constexpr std::array<const char*,Research::rarityCount> colorKeys={"RarityColor.Common","RarityColor.Rare","RarityColor.Epic","RarityColor.Insane"};
    for(size_t i=0;i<weightKeys.size();++i) {
        rarityNameOverrides[i]=displayText(field("starfall.research",nameKeys[i]));
        auto colorText=field("starfall.research",colorKeys[i]);
        rarityColors[i]=ConfigText::rgb(colorText);
        if(!colorText.empty() && !rarityColors[i])
            warn("starfall.research",colorKeys[i],"应填写三个 0～255 的 RGB 整数，使用此档默认配色");
        int weight=number(field("starfall.research",weightKeys[i],std::to_string(defaultWeights[i])));
        if(weight<0) {
            warn("starfall.research",weightKeys[i],"无效权重，使用默认值 "+std::to_string(defaultWeights[i]));
            weight=defaultWeights[i];
        }
        Research::rarityWeights[i]=weight;
    }
    const string list="starfall.researchtypes";
    auto registryIds=[&](const string& list) {
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
        return ids;
    };
    const auto ids=registryIds(list);
    const std::set<string> projects(ids.begin(),ids.end());
    ++bondRevision;bonds.clear();bondIndex.clear();memberBonds.clear();
    for(const auto& id:registryIds("starfall.researchbondtypes")) {
        Bond bond;bond.id=id;bond.name=displayText(field(id,"Name",id));
        bond.members=split(field(id,"Members"));
        if(id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")!=id.npos || bond.members.empty() ||
            !std::all_of(bond.members.begin(),bond.members.end(),[&](const auto& m){return projects.contains(m);})) {
            warn(id,"Members","羁绊 ID 无效、缺少成员或成员科技未注册，已忽略羁绊");continue;
        }
        bondIndex[id]=static_cast<int>(bonds.size());
        bonds.push_back(std::move(bond));
    }
    counterProjects.clear();
    for(const auto& id:ids) {
        auto raw=lower(field(id,"Counters"));
        if(raw.empty()) continue;
        auto names=split(raw);
        auto valid=[](const string& s){return !s.empty() && s.size()<=127 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==string::npos;};
        std::set<string> unique(names.begin(),names.end());
        if(!valid(id) || names.size()!=1+std::count(raw.begin(),raw.end(),',') || unique.size()!=names.size() ||
           !std::all_of(names.begin(),names.end(),valid)) {
            warn(id,"Counters","计数变量声明无效，请使用不重复的字母、数字或下划线名称");continue;
        }
        for(const auto& name:names) counterProjects.emplace(id+"."+name,id);
    }
    // Passives reference counter declarations; research rewards in turn validate passive IDs.
    if(loadPassives) loadPassives();
    using E=Research::Effect;
    // Expand only explicit research targets while loading. Combat and price lookups
    // can keep using their existing matching and per-house caches.
    std::map<string,vector<string>> researchVariants;
    for(auto type:TechnoTypeClass::Array) {
        const auto variant=lower(type->ID);
        const auto parent=lower(field(variant,"Starfall.ResearchAs"));
        if(parent.empty()) continue;
        if(parent==variant || !TechnoTypeClass::Find(parent.c_str())) {
            warn(variant,"Starfall.ResearchAs","须填写一个已注册的其他单位或建筑 ID，已忽略本项");
            continue;
        }
        researchVariants[parent].push_back(variant);
    }
    auto researchTargets=[&](E effect,const string& raw) {
        auto targets=split(raw);
        if(effect!=E::Passive && effect!=E::TypeCost) return targets;
        const auto explicitCount=targets.size();
        for(size_t i=0;i<explicitCount;++i) {
            auto variants=researchVariants.find(targets[i]);
            if(variants==researchVariants.end()) continue;
            for(const auto& variant:variants->second)
                if(!includes(targets,variant)) targets.push_back(variant);
        }
        return targets;
    };
    auto parseEffect=[](const string& effect) {
        return effect=="grantpassive"?E::Passive:effect=="givecredits"?E::Credits:
            (effect=="unlocktech" || effect=="unlockteck")?E::Technology:effect=="convertinfantry"?E::ConvertInfantry:
            effect=="convertvehicle"?E::ConvertVehicle:effect=="modifytypecost"?E::TypeCost:
            effect=="modifycategorycost"?E::CategoryCost:effect=="replaceproduction"?E::ReplaceProduction:
            effect=="grantrandomresearch"?E::RandomResearch:effect=="grantfreeresearch"?E::FreeResearch:E::Invalid;
    };
    auto costFields=[&](auto& effect,const string& section) {
        if(effect.effect==E::TypeCost || effect.effect==E::CategoryCost)
            effect.cost=readCostChange(field(section,"CostModifier"),lower(field(section,"Category")));
        if(effect.effect==E::RandomResearch || effect.effect==E::FreeResearch) {
            const auto rarity=lower(field(section,"Reward.Rarity","Any"));
            if(rarity!="any") effect.reward.rarity=rarity=="common"?Rarity::Common:rarity=="rare"?Rarity::Rare:
                rarity=="epic"?Rarity::Epic:rarity=="insane"?Rarity::Insane:Rarity::Invalid;
            effect.reward.count=number(field(section,"Reward.Count","1"));
            if(effect.effect==E::FreeResearch) {
                const auto raw=trim(field(section,"Reward.ResearchOptions"));
                effect.reward.projects=split(raw);
                if(!raw.empty() && effect.reward.projects.empty()) {
                    warn(section,"Reward.ResearchOptions","候选范围须填写科技 ID");
                    effect.effect=E::Invalid;
                }
            }
        }
    };
    auto readNestedEffect=[&](const string& sectionId) {
        StageEffect spec;spec.id=sectionId;
        spec.effect=parseEffect(lower(field(sectionId,"Effect")));
        costFields(spec,sectionId);
        spec.passive=lower(field(sectionId,"Passive"));spec.types=researchTargets(spec.effect,field(sectionId,"TargetTypes"));
        spec.credits=number(field(sectionId,"Credits","0"));
        spec.technology=lower(field(sectionId,"Technology"));
        spec.from=lower(field(sectionId,"FromType"));spec.to=lower(field(sectionId,"ToType"));
        return spec;
    };
    auto readTierEffects=[&](const string& stageId,vector<StageEffect>& effects) {
        auto effectIds=hasField(stageId,"Effects")?split(field(stageId,"Effects")):vector<string>{stageId};
        bool valid=!effectIds.empty();
        std::set<string> replacementTypes;
        for(const auto& effectId:effectIds) {
            auto spec=readNestedEffect(effectId);
            if(effectId.size()>127 || !validateEffect(spec,[&](const char* key,const string& reason){
                warn(effectId,key,reason+"；阶段 ["+stageId+"] 已忽略");
            })) valid=false;
            if(spec.effect==E::ReplaceProduction &&
               (!replacementTypes.insert(spec.from).second || !replacementTypes.insert(spec.to).second)) valid=false;
            effects.push_back(std::move(spec));
        }
        if(!valid) warn(stageId,"Effects","阶段效果列表无效，已忽略该阶段");
        return valid;
    };
    for(auto& bond:bonds) {
        for(const auto& id:split(field(bond.id,"Stages"))) {
            BondTier tier;tier.id=id;tier.required=number(field(id,"RequiredCount"));
            if(id.size()>127 || tier.required<1 || tier.required>static_cast<int>(bond.members.size())) {
                warn(id,"RequiredCount","须填写 1 到不同成员项目数量之间的整数，已忽略该档");continue;
            }
            tier.summary=displayText(field(id,"Summary"));
            if(readTierEffects(id,tier.effects)) bond.tiers.push_back(std::move(tier));
        }
        if(bond.tiers.empty()) {warn(bond.id,"Stages","没有有效的羁绊档位，已忽略羁绊");continue;}
        for(const auto& member:bond.members) memberBonds[member].push_back(bondIndex.at(bond.id));
        for(const auto& tier:bond.tiers) for(const auto& effect:tier.effects)
            if(effect.effect==E::Technology || effect.effect==E::ReplaceProduction) installProductionCheck();
    }
    for(const auto& id:ids) {
        if(Research::options.size()>SHRT_MAX) break;
        if(!hasSection(id)) configLog("["+list+"] "+id+" - 找不到同名科研项目段");
        Option o{}; o.id=id; o.name=displayText(id);
        for(const auto& d:defaults) if(id==d.id) o=d;
        o.repeatable=readRepeatable(id,defaultRepeatable);
        o.effect=parseEffect(lower(field(id,"Effect","GrantPassive")));
        costFields(o,id);
        o.credits=number(field(id,"Credits","0"));
        o.technology=lower(field(id,"Technology"));
        o.from=lower(field(id,"FromType")); o.to=lower(field(id,"ToType"));
        o.passive=lower(field(id,"Passive",o.passive));
        o.icon=field(id,"Icon");
        o.countries=split(field(id,"Countries"));
        o.unlockWithTech=split(field(id,"UnlockWithTech"));
        for(const auto& tech:o.unlockWithTech)
            if(!TechnoTypeClass::Find(tech.c_str())) warn(id,"UnlockWithTech","未注册的科技类型 ["+tech+"] 无法解锁此项目");
        o.prerequisites=split(field(id,"Prerequisites"));
        o.excludes=split(field(id,"Excludes"));
        auto grantOnly=lower(trim(field(id,"GrantOnly","no")));
        o.grantOnly=!includes({"no","false","0"},grantOnly);
        if(o.grantOnly && !includes({"yes","true","1"},grantOnly))
            warn(id,"GrantOnly","无效开关值，按 yes 处理");
        o.replaces=split(field(id,"Replaces"));
        for(const auto& key:o.replaces)
            if(!includes(o.prerequisites,key)) o.prerequisites.push_back(key);
        auto rarity=lower(field(id,"Rarity","Common"));
        o.rarity=rarity=="common"?Research::Rarity::Common:rarity=="rare"?Research::Rarity::Rare:
            rarity=="epic"?Research::Rarity::Epic:rarity=="insane"?Research::Rarity::Insane:Research::Rarity::Invalid;
        if(o.rarity==Research::Rarity::Invalid) warn(id,"Rarity","应为 Common、Rare、Epic 或 Insane，项目无法抽取");
        o.types=researchTargets(o.effect,field(id,"TargetTypes",o.types.empty()?"":"MTNK"));
        auto listValue=field(id,"Effects");
        const bool hasList=hasField(id,"Effects");
        static const std::set<string> projectKeys={"stages","counters","name","summary","detail","detail.decimals","targetlabel","icon","countries","unlockwithtech","prerequisites","excludes","replaces","grantonly","rarity","effects","effect",
            "repeatable","costmodifier","category","credits","technology","fromtype","totype","passive","targettypes","reward.rarity","reward.count","reward.researchoptions"};
        const auto stageIds=split(field(id,"Stages"));
        o.stagesOnly=!stageIds.empty() && !hasList && field(id,"Effect").empty() && o.passive.empty();
        if((hasList || o.stagesOnly) && !Runtime::hasPassive(id) && !TechnoTypeClass::Find(id.c_str())) warnUnknown(id,projectKeys);
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
                auto spec=readNestedEffect(sectionId);
                if(!hasSection(sectionId)) warn(id,"Effects","找不到效果段 ["+sectionId+"]，项目无法抽取");
                else auditEffect(sectionId,spec);
                o.effects.push_back(std::move(spec));
            }
        } else if(!o.stagesOnly) auditEffect(id,singleEffect(o));
        for(const auto& stageId:stageIds) {
            Stage stage;stage.id=stageId;
            if(stageId.size()>127 || !hasSection(stageId)) {
                warn(id,"Stages","阶段 ["+stageId+"] 不存在或 ID 超过 127 字节，已忽略");continue;
            }
            if(!stage.condition.compile(lower(field(stageId,"Condition.Formula"))) || !stage.condition.validForResearch() ||
               !std::all_of(stage.condition.counterNames().begin(),stage.condition.counterNames().end(),
                    [](const string& key){return knownResearchVariable(key);})) {
                warn(stageId,"Condition.Formula","阶段公式无效或引用未声明的科研计数，已忽略该阶段");continue;
            }
            if(readTierEffects(stageId,stage.effects)) o.stages.push_back(std::move(stage));
        }
        auto label=field(id,"Name"),brief=field(id,"Summary"),detail=field(id,"Detail");
        o.targetLabel=displayText(field(id,"TargetLabel"));
        if(!label.empty()) o.name=displayText(label);
        if(!brief.empty()) o.summary=displayText(brief);
        if(!detail.empty()) o.detail=displayText(detail);
        if(!compileDetail(o)) warn(id,"Detail","公式无效，请使用已声明的科研计数和数值运算；无效公式显示为 —");
        const auto precision=field(id,"Detail.Decimals");
        if(!precision.empty()) {
            int decimals=number(precision);
            if(decimals>=0 && decimals<=6) o.detailDecimals=decimals;
            else warn(id,"Detail.Decimals","请填写 0 至 6 的整数，已使用默认显示精度");
        }
        Research::options.emplace_back(std::move(o));
    }
    // Resolve license sources once. Ownership checks then also work when a reward
    // previews the house's state after acquiring or replacing a project.
    std::map<string,vector<string>> providers;
    for(const auto& project:options) {
        auto index=[&](const auto& effect) {
            if(effect.effect==Effect::Technology) providers[effect.technology].push_back(project.id);
        };
        if(project.effectList) {for(const auto& effect:project.effects) index(effect);}
        else index(project);
    }
    for(auto& project:options) for(const auto& tech:project.unlockWithTech) {
        auto it=providers.find(tech);
        if(it!=providers.end() && TechnoTypeClass::Find(tech.c_str()))
            project.techProviders.insert(project.techProviders.end(),it->second.begin(),it->second.end());
    }
    indexStages();
    validateResearchChains(warn);
    Research::clearOfferCache();
    for(const auto& project:Research::options) {
        const bool grantsTech=project.effectList ? std::any_of(project.effects.begin(),project.effects.end(),
            [](const auto& effect){return effect.effect==Research::Effect::Technology || effect.effect==Research::Effect::ReplaceProduction;}) :
            project.effect==Research::Effect::Technology || project.effect==Research::Effect::ReplaceProduction;
        const bool stageTech=std::any_of(project.stages.begin(),project.stages.end(),[](const Stage& stage){
            return std::any_of(stage.effects.begin(),stage.effects.end(),[](const auto& e){
                return e.effect==Effect::Technology || e.effect==Effect::ReplaceProduction;
            });
        });
        if(grantsTech || stageTech) {installProductionCheck();break;}
    }
}
