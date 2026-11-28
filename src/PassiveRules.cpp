#include "PassiveRules.h"
#include "BuildingSpawn.h"
#include "ConfigText.h"
#include <AnimTypeClass.h>
#include <BuildingTypeClass.h>
#include <CCINIClass.h>
#include <ParticleSystemTypeClass.h>
#include <Powerups.h>
#include <WarheadTypeClass.h>
#include <WeaponTypeClass.h>
#include <charconv>
#include <cmath>

namespace PassiveRules {
using ConfigText::configLog;
using ConfigText::includes;
using ConfigText::lower;
using ConfigText::number;
using ConfigText::split;
using ConfigText::trim;

char configPath[MAX_PATH]{};
const std::pair<const string,ScriptFields>* parsingScript=nullptr;
std::map<string,string> rulesSkillNames;
const char* rulesName(const string& section) {
    auto it=rulesSkillNames.find(section);
    return it==rulesSkillNames.end()?section.c_str():it->second.c_str();
}
string readRaw(const string& section, const char* key, const char* fallback) {
    if(parsingScript && parsingScript->first==section) {
        auto it=parsingScript->second.find(lower(key));
        return it==parsingScript->second.end()?lower(fallback):it->second;
    }
    char buf[2048]{};
    GetPrivateProfileStringA(section.c_str(), key, "", buf, sizeof(buf), configPath);
    if (!buf[0] && !CCINIClass::INI_Rules) return fallback;
    if (!buf[0]) {
        auto ini=CCINIClass::INI_Rules;
        const char* name=rulesName(section);
        // Enumeration normalizes custom parameter keys. YR may index the original spelling.
        if(std::string_view(key).starts_with("script.param.") || std::string_view(key).starts_with("AttachNode.")) {
            for(int i=0;i<ini->GetKeyCount(name);++i) {
                auto original=ini->GetKeyName(name,i);
                if(original && !_stricmp(original,key)) {key=original;break;}
            }
        }
        ini->ReadString(name,key,fallback,buf,sizeof(buf));
    }
    return trim(string(buf).substr(0,string(buf).find(';')));
}
string read(const string& section,const char* key,const char* fallback) {
    return lower(readRaw(section,key,fallback));
}

namespace {
using ForceMove::Spec;
using ForceMove::Easing;
using ForceMove::Mode;
bool leptons(const string& text,int& result,bool signedValue=false) {
    double value=0;
    auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(error!=std::errc{} || end!=text.data()+text.size() || !std::isfinite(value) ||
       value<(signedValue?-512:0) || value>512) return false;
    result=static_cast<int>(std::lround(value*256));
    return true;
}
template<class Field>
bool parseForceMove(Spec& spec,Field field) {
    auto mode=field("Action.ForceMove.Mode"),center=field("Action.ForceMove.Center");
    auto offset=field("Action.ForceMove.Offset"),distance=field("Action.ForceMove.Distance");
    auto height=field("Action.ForceMove.Height");
    if(!leptons(field("Action.ForceMove.AirborneHeight","1"),spec.airborneHeight)) return false;
    auto tilt=field("Action.ForceMove.Tilt","yes");
    if(tilt!="yes" && tilt!="no") return false;
    spec.tilt=tilt=="yes";
    auto ignore=field("Action.ForceMove.IgnoreCollision","yes");
    if(ignore!="yes" && ignore!="no") return false;
    spec.ignoreCollision=ignore=="yes";
    auto weight=field("Action.ForceMove.BlowWeight");
    if(!weight.empty()) {
        double limit;
        auto [end,error]=std::from_chars(weight.data(),weight.data()+weight.size(),limit);
        if(error!=std::errc{} || end!=weight.data()+weight.size() || !std::isfinite(limit)) return false;
        spec.blowWeight=limit;
    }
    auto iron=field("Action.ForceMove.AffectsIronCurtain","no");
    if(iron!="yes" && iron!="no") return false;
    spec.affectsIronCurtain=iron=="yes";
    auto easing=field("Action.ForceMove.Easing","linear");
    if(easing=="easeout") spec.easing=Easing::EaseOut;
    else if(easing=="easein") spec.easing=Easing::EaseIn;
    else if(easing=="easeinout") spec.easing=Easing::EaseInOut;
    else if(easing!="linear") return false;
    spec.duration=number(field("Action.ForceMove.Duration"));
    if(spec.duration<1) return false;
    if(!offset.empty()) {
        auto comma=offset.find(',');
        if(!mode.empty() || !center.empty() || !distance.empty() || comma==string::npos ||
           !leptons(trim(offset.substr(0,comma)),spec.dx,true) || !leptons(trim(offset.substr(comma+1)),spec.dy,true)) return false;
    } else {
        if(mode=="push") spec.mode=Mode::Push;
        else if(mode=="pull") spec.mode=Mode::Pull;
        else if(mode=="launch") spec.mode=Mode::Launch;
        else return false;
        if(!center.empty() && center!="self" && center!="impact") return false;
        spec.impact=center=="impact";
        if(!distance.empty() && !leptons(distance,spec.distance)) return false;
        if(spec.mode!=Mode::Launch && !spec.distance) return false;
    }
    if(!leptons(height.empty()?(spec.mode==Mode::Launch?"1":"0"):height,spec.height)) return false;
    return spec.dx || spec.dy || spec.distance || spec.height;
}
} // namespace

bool passengerCombatMultiplier(size_t index) {
    return index==1 || index==3 || index==4 || index==experienceMultiplierIndex;
}
bool validStateMultiplier(size_t index,double value) {
    return value<=100 && (value>0 || ((index==powerMultiplierIndex || index==experienceMultiplierIndex) && value==0));
}
bool beforeHealthLoss(const string& trigger) {
    return trigger=="onbeforedealdamage" || trigger=="onbeforetakedamage";
}
bool damageEvent(const string& trigger) {
    return beforeHealthLoss(trigger) || trigger=="ondealdamage" || trigger=="ontakedamage";
}
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
    if(parsingScript && parsingScript->first==section) {
        for(const auto& [key,value]:parsingScript->second) keys.insert(key);
        return keys;
    }
    if(CCINIClass* ini=CCINIClass::INI_Rules)
        for(int i=0;i<ini->GetKeyCount(rulesName(section));++i) keys.insert(lower(ini->GetKeyName(rulesName(section),i)));
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
bool mountOffset(const string& text,CoordStruct& result) {
    int* axes[]={&result.X,&result.Y,&result.Z};
    size_t start=0;
    for(int i=0;i<3;++i) {
        auto comma=text.find(',',start);
        if((i<2)!=(comma!=string::npos)) return false;
        auto component=trim(text.substr(start,comma-start));
        auto parsed=std::from_chars(component.data(),component.data()+component.size(),*axes[i]);
        if(parsed.ec!=std::errc{} || parsed.ptr!=component.data()+component.size() || *axes[i]<-8192 || *axes[i]>8192) return false;
        start=comma+1;
    }
    return true;
}
bool actionField(const string& key) {
    static const std::set<string> fields={"action","action.order","action.counter","action.amount","action.maxhealthpercent","action.target","action.weapon",
        "action.anim","action.sound","action.shakex","action.shakey","action.shakelocal","action.showtext","action.landfirst","action.particlesystem","action.rarity","action.researchoptions","action.research",
        "action.warhead","action.passive","action.duration","action.stackonreapply","action.cratetype","action.pickuptype","action.pickuptypes","action.range","action.owner","action.unittype","action.unittypes","action.weights","action.randomcount","action.buildingtype","action.percent",
        "action.eject.range","action.eject.openheight","action.eject.horizontalspeed","action.eject.interval",
        "action.jump.height","action.jump.duration","action.jump.range","action.jump.takeoffanim","action.jump.takeoffsound","action.jump.landanim","action.jump.landsound",
        "action.forcemove.mode","action.forcemove.center","action.forcemove.offset","action.forcemove.distance","action.forcemove.height","action.forcemove.airborneheight","action.forcemove.duration","action.forcemove.tilt","action.forcemove.easing","action.forcemove.ignorecollision","action.forcemove.blowweight","action.forcemove.affectsironcurtain",
        "action.node","action.slot","action.count","action.anchor","action.flh","action.transportfire","action.excludeparentpassives","action.destination","action.searchradius","action.instant","action.script","action.function","action.levels","action.signal","action.signal.target","action.signal.range","action.signal.houses","action.signal.includeself","action.signal.value"};
    return fields.contains(key);
}
bool activeField(const string& key) {
    static const std::set<string> keys={"active.name","active.icon","active.castmode","active.interruptorders","active.target",
        "active.range","active.radius","active.distance","active.shape","active.houses",
        "active.indicator.texture","active.indicator.opacity","active.indicator.arrow.texture",
        "active.indicator.arrow.width","active.indicator.arrow.head","active.indicator.arrow.tail"};
    return keys.contains(key);
}
static bool readTooltipText(Skill& skill) {
    const auto text=readRaw(skill.id,"Modifier.TooltipText");
    if(text.empty()) return false;
    for(size_t pos=0;pos<text.size();) {
        const auto start=text.find("$(",pos);
        auto& part=skill.tooltipText.emplace_back();
        part.literal=ConfigText::displayText(text.substr(pos,start-pos));
        if(start==string::npos) break;
        const auto end=ConfigText::formulaEnd(text,start);
        if(end==string::npos || !part.number.compile(skill.id,"Modifier.TooltipText",lower(text.substr(start,end-start))) ||
           part.number.expression.usesDamage() || part.number.expression.usesSignal()) return false;
        pos=end;
    }
    return true;
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
        if(key=="tint.color" || key=="tint.strength" || key=="anim" || key=="anim.always" || key=="anim.whileready" || key=="gainanim" || key=="triggeranim" || key=="triggeranim.target" || key=="stack.max") continue;
        if(key.starts_with("script.param.") && key.size()>13 && !continuous) continue;
        if(s.trigger=="oncast" && activeField(key)) continue;
        if(key=="condition.formula") {
            auto value=read(s.id,key.c_str());
            if(!s.conditionFormula.compile(s.id,key,value)) return invalidField(s.id,key+"="+value);
            if(s.conditionFormula.expression.usesDamage() &&
               !damageEvent(s.trigger)) return invalidField(s.id,key+" uses Event.Damage");
        } else if(includes({"condition.selfhealthatleast","condition.selfhealthatmost","condition.selfhealthbelow",
                            "condition.ammoatleast","condition.ammoatmost","condition.passengersatleast","condition.passengersatmost"},key)) {
            return invalidField(s.id,key+" removed; use Condition.Formula");
        } else if(key.starts_with("condition.nearby.")) {
            if(!continuous || !includes({"condition.nearby.range","condition.nearby.houses","condition.nearby.kinds","condition.nearby.types","condition.nearby.countatleast","condition.nearby.countatmost","condition.nearby.scaninterval"},key))
                return invalidField(s.id,key);
            s.nearby.range=-1;
        } else if(key=="condition.selfdeployed") {
            const auto value=read(s.id,key.c_str());
            if(!continuous || (value!="yes" && value!="no")) return invalidField(s.id,key);
            s.deployPosture=value=="yes"?DeployPosture::Deployed:DeployPosture::Undeployed;
        } else if(key=="condition.statedurationseconds") {
            if(!continuous || !parseStateDelay(read(s.id,key.c_str()),s.stateDelayFrames)) return invalidField(s.id,key);
        } else if(key=="modifier.tooltiptext") {
            if(!continuous || !readTooltipText(s)) return invalidField(s.id,"Modifier.TooltipText");
            modifier=true;
        } else if(key=="modifier.tooltipdecimals") {
            s.tooltipDecimals=number(read(s.id,"Modifier.TooltipDecimals"));
            if(!continuous || !keys.contains("modifier.tooltiptext") || s.tooltipDecimals<0 || s.tooltipDecimals>6)
                return invalidField(s.id,"Modifier.TooltipDecimals requires Modifier.TooltipText and 0..6");
        } else if(key=="modifier.visualscale" || key=="modifier.projectilescale") {
            auto& scale=key=="modifier.visualscale"?s.visualScale:s.projectileScale;
            scale=scalePercent(read(s.id,key.c_str()),key=="modifier.visualscale"?0.01:0.5);
            if(!continuous || scale<0) return invalidField(s.id,key);
            modifier=true;
        } else if(key.starts_with("modifier.projectileshield.")) {
            // sectionKeys normalizes names for validation. YR's rules lookup needs their INI spelling.
            static constexpr std::array fields={"Modifier.ProjectileShield.Radius","Modifier.ProjectileShield.HeightOffset",
                "Modifier.ProjectileShield.DamageMultiplier","Modifier.ProjectileShield.BlockInviso"};
            auto field=std::find_if(fields.begin(),fields.end(),[&](const char* name){return !_stricmp(name,key.c_str());});
            if(field==fields.end()) return invalidField(s.id,key);
            const string name=*field;
            if(!continuous || !keys.contains("modifier.projectileshield.radius"))
                return invalidField(s.id,name+" requires WhileState and Modifier.ProjectileShield.Radius");
            const auto raw=read(s.id,name.c_str());
            if(key=="modifier.projectileshield.blockinviso") {
                if(raw!="yes" && raw!="no") return invalidField(s.id,name+"="+raw);
                s.shield.blockInviso=raw=="yes";
            } else {
                double value{};
                const auto result=std::from_chars(raw.data(),raw.data()+raw.size(),value);
                if(result.ec!=std::errc{} || result.ptr!=raw.data()+raw.size() || !std::isfinite(value))
                    return invalidField(s.id,name+"="+raw);
                if(key=="modifier.projectileshield.radius" && value>0 && value<=512) s.shield.radius=value;
                else if(key=="modifier.projectileshield.heightoffset" && std::abs(value)<=512) s.shield.heightOffset=value;
                else if(key=="modifier.projectileshield.damagemultiplier" && value>=0) s.shield.damageMultiplier=value;
                else return invalidField(s.id,name+"="+raw);
            }
            modifier=true;
        } else if(key.starts_with("modifier.aura.")) {
            if(!continuous || !includes({"modifier.aura.range","modifier.aura.houses","modifier.aura.targettypes",
                "modifier.aura.passives","modifier.aura.includeself"},key)) return invalidField(s.id,key);
            modifier=true;
            s.aura.range=1; // Read the grouped fields together after checking their names.
        } else if(key=="modifier.linkedpassives") {
            s.linkedPassives=split(read(s.id,key.c_str()));
            if(!continuous || s.linkedPassives.empty()) return invalidField(s.id,key);
            modifier=true;
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
        } else if(key=="modifier.multishot" || key=="multishot") {
            auto value=read(s.id,key.c_str());
            if(!continuous) return invalidField(s.id,key);
            if(keys.contains("modifier.multishot") && keys.contains("multishot"))
                return invalidField(s.id,"specify only one of MultiShot and Modifier.MultiShot");
            if(value=="yes") value="$(this.stack)";
            if(value.starts_with("$(")) {
                auto& formula=s.multiShotFormula;
                if(!formula.compile(s.id,key,value) || formula.expression.usesDamage() || formula.expression.usesSignal())
                    return invalidField(s.id,key+"="+value);
            } else {
                s.extraShotCount=value=="no"?0:number(value);
                if(s.extraShotCount<0) return invalidField(s.id,key+"="+value);
            }
            s.passengerCombat|=s.hasMultiShot();
            modifier=true;
        } else if(key=="multishot.prefersamelayer" || key=="modifier.multishot.prefersamelayer") {
            if(!continuous || (!keys.contains("multishot") && !keys.contains("modifier.multishot")))
                return invalidField(s.id,"MultiShot.PreferSameLayer requires WhileState and MultiShot");
            if(keys.contains("multishot.prefersamelayer") && keys.contains("modifier.multishot.prefersamelayer"))
                return invalidField(s.id,"specify only one MultiShot.PreferSameLayer alias");
            const auto value=read(s.id,key.c_str());
            if(value!="yes" && value!="no") return invalidField(s.id,key+"="+value);
            s.multiShotPreferSameLayer=value=="yes";
        } else if(key=="modifier.taunt.target") {
            if(!continuous || read(s.id,key.c_str())!="source") return invalidField(s.id,key+" requires WhileState and Source");
            s.taunt=true;
            modifier=true;
        } else if(key=="modifier.taunt.priority") {
            s.tauntPriority=number(read(s.id,key.c_str()));
            if(!continuous || !keys.contains("modifier.taunt.target") || s.tauntPriority<0)
                return invalidField(s.id,key+" requires Modifier.Taunt.Target and a nonnegative integer");
        } else if(key=="modifier.emp") {
            auto value=read(s.id,key.c_str());
            if(!continuous || (value!="yes" && value!="no")) return invalidField(s.id,key);
            s.emp=value=="yes";
            modifier|=s.emp;
        } else if(key=="modifier.berserk") {
            auto value=read(s.id,key.c_str());
            if(!continuous || (value!="yes" && value!="no")) return invalidField(s.id,key);
            s.berserk=value=="yes";
            modifier|=s.berserk;
        } else if(key=="modifier.mirage" || key=="modifier.immune" || key=="modifier.immunetomindcontrol" || key=="modifier.cloakable") {
            auto value=read(s.id,key.c_str());
            if(!continuous || (value!="yes" && value!="no")) return invalidField(s.id,key);
            auto& enabled=key=="modifier.immune"?s.immune:key=="modifier.immunetomindcontrol"?s.mindImmune:key=="modifier.cloakable"?s.cloakable:s.mirage;
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
            static const std::set<string> eventKeys={"trigger","actions","randomactions","randomactions.weights","randomactions.count","interval","maxtriggers","chance","cooldown",
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
        query.types=split(read(s.id,"Condition.Nearby.Types","*"));
        if(query.range<1 || query.range>32 || query.interval<1 || query.least<0 || query.most<query.least)
            return invalidField(s.id,"Condition.Nearby range/count/interval");
        for(auto& h:query.houses) if(!includes({"all","self","allies","enemies","neutral"},h)) return invalidField(s.id,"Condition.Nearby.Houses");
        for(auto& k:query.kinds) if(!includes({"all","infantry","vehicle","aircraft","building"},k)) return invalidField(s.id,"Condition.Nearby.Kinds");
        for(const auto& type:query.types) if(type!="*" && !Research::isTargetCategory(type) && !TechnoTypeClass::Find(type.c_str())) return invalidField(s.id,"Condition.Nearby.Types="+type);
    }
    if(continuous && !modifier && !s.triggerAnim && !s.anim && !s.tint) return invalidField(s.id,"Modifier.*, Anim, Tint.Color or TriggerAnim required");
    if(keys.contains("modifier.transportpassives.stack") && s.transportPassives.empty()) return invalidField(s.id,"Modifier.TransportPassives.Stack requires Modifier.TransportPassives");
    if(!s.transportPassives.empty() && (query.range || s.conditionFormula.expression.usesDistance() || s.conditionFormula.expression.usesTargetValues()))
        return invalidField(s.id,"transport support cannot use spatial or target conditions");
    return true;
}
bool positionalAction(std::string_view name) {
    return name=="spawnunit" || name=="spawnbuilding" || name=="spawncrate" || name=="spawnpickup" || name=="removepickup" || name=="fireweapon" || name=="detonatewarhead" ||
           name=="playanim" || name=="playsound" || name=="shakescreen" || name=="spawnparticlesystem";
}
bool readAction(Skill& skill,const string& section,const ScriptFields* fields) {
    auto field=[&](const char* key,const char* fallback="") {
        if(!fields) return read(section,key,fallback);
        auto it=fields->find(lower(key));
        return it==fields->end()?lower(fallback):lower(trim(it->second));
    };
    PassiveAction a;
    auto invalid=[&](const string& field) {
        return invalidField(skill.id,section==skill.id ? field : "Actions -> ["+section+"] "+field);
    };
    a.action=field("Action");
    a.order=field("Action.Order");
    if(!a.order.empty() && a.action!="startunitorder" && a.action!="cancelunitorder") return invalid("Action.Order requires StartUnitOrder or CancelUnitOrder");
    const bool ejection=a.action=="ejectpassengers" || a.action=="ejectunit";
    if(auto value=field("Action.LandFirst");!value.empty()) {
        if(a.action!="transform" || (value!="yes" && value!="no")) return invalid("Action.LandFirst requires Transform and yes/no");
        a.landFirst=value=="yes";
    }
    if(a.action!="chronoteleport" && a.action!="jumpto" && !ejection && !field("Action.Destination").empty())
        return invalid("Action.Destination requires ChronoTeleport, JumpTo, EjectPassengers or EjectUnit");
    if(auto instant=field("Action.Instant");!instant.empty()) {
        if(a.action!="chronoteleport" || (instant!="yes" && instant!="no")) return invalid("Action.Instant requires ChronoTeleport and yes/no");
        a.teleportInstant=instant=="yes";
    }
    if(a.action=="chronoteleport" || a.action=="spawnbuilding") {
        a.searchRadius=number(field("Action.SearchRadius","3"));
        if(a.searchRadius<0 || a.searchRadius>32) return invalid("Action.SearchRadius (0..32)");
    } else if(!field("Action.SearchRadius").empty()) return invalid("Action.SearchRadius requires ChronoTeleport or SpawnBuilding");
    if(a.action!="promote" && !field("Action.Levels").empty()) return invalid("Action.Levels requires Promote");
    if(a.action!="runscript" && (!field("Action.Script").empty() || !field("Action.Function").empty()))
        return invalid("Action.Script and Action.Function require RunScript");
    bool hasPickupTypes=false,hasPickupRange=false;
    auto validKey=[&](const string& key) {
        if(key=="action.showtext") {
            auto value=field("Action.ShowText");
            if((a.action!="givecredits" && a.action!="giveresearchpoints") || (value!="yes" && value!="no"))
                return invalid("Action.ShowText requires GiveCredits or GiveResearchPoints and yes/no");
            a.showText=value=="yes";
        }
        if(a.action=="removeattachedunit" && key.starts_with("action.") &&
           !includes({"action.target","action.slot","action.unittype","action.count"},key)) return invalid("unsupported RemoveAttachedUnit field "+key);
        if(key=="action.count" && a.action!="removeattachedunit") return invalid("Action.Count requires RemoveAttachedUnit");
        if(key=="action.slot" && a.action!="attachunit" && a.action!="removeattachedunit") return invalid("Action.Slot requires AttachUnit or RemoveAttachedUnit");
        if(includes({"action.node","action.anchor","action.flh","action.transportfire","action.excludeparentpassives"},key) && a.action!="attachunit") return invalid(key+" requires AttachUnit");
        if(key=="action.pickuptypes") hasPickupTypes=true;
        if(key=="action.range") hasPickupRange=true;
        if(key.starts_with("action.") && !actionField(key)) return invalid("unknown field "+key);
        if(key=="action.pickuptype" && a.action!="spawnpickup") return invalid(key+" requires SpawnPickup");
        if((key=="action.pickuptypes" || key=="action.range") && a.action!="removepickup") return invalid(key+" requires RemovePickup");
        if(key=="action.rarity" && a.action!="grantrandomresearch" && a.action!="grantfreeresearch") return invalid(key+" requires a research reward");
        if(key=="action.research" && a.action!="grantresearch") return invalid(key+" requires GrantResearch");
        if(key=="action.researchoptions" && a.action!="grantfreeresearch") return invalid(key+" requires GrantFreeResearch");
        if(key=="action.buildingtype" && a.action!="spawnbuilding") return invalid(key+" requires SpawnBuilding");
        if(key=="action.unittype" && a.action=="spawnbuilding") return invalid("SpawnBuilding requires Action.BuildingType");
        if((key=="action.unittypes" || key=="action.weights" || key=="action.randomcount") && a.action!="spawnunit") return invalid(key+" requires SpawnUnit");
        static const std::map<string,string> presentationFields={
            {"action.anim","playanim"},{"action.sound","playsound"},{"action.particlesystem","spawnparticlesystem"},
            {"action.shakex","shakescreen"},{"action.shakey","shakescreen"},{"action.shakelocal","shakescreen"}};
        if(auto it=presentationFields.find(key);it!=presentationFields.end() && a.action!=it->second)
            return invalid(key+" requires "+it->second);
        if(key=="action.eject.interval") return a.action=="ejectpassengers" || invalid(key+" requires EjectPassengers");
        if(key.starts_with("action.eject.")) return ejection || invalid(key+" requires EjectPassengers or EjectUnit");
        if(key.starts_with("action.jump.")) return a.action=="jumpto" || invalid(key+" requires JumpTo");
        if(key.starts_with("action.forcemove."))
            return (a.action=="forcemove" && actionField(key)) || invalid(key+" requires a valid ForceMove field");
        if(a.action!="sendsignal" && (key=="action.signal" || key.starts_with("action.signal.")))
            return invalid(key+" requires SendSignal");
        return true;
    };
    if(fields) {
        for(const auto& entry:*fields) if(!validKey(entry.first)) return false;
    } else for(const auto& key:sectionKeys(section)) if(!validKey(key)) return false;
    if(a.action=="grantpassive") {
        auto duration=field("Action.Duration","-1");
        if(duration!="-1" && (a.duration=number(duration))<=0) return invalid("Action.Duration="+duration);
    } else if(a.action=="applyironcurtain") {
        a.duration=number(field("Action.Duration"));
        if(a.duration<=0) return invalid("Action.Duration must be a positive frame count");
    } else if(!field("Action.Duration").empty()) return invalid("Action.Duration");
    auto reapply=field("Action.StackOnReapply");
    if(!reapply.empty()) {
        if(a.action!="grantpassive" || (reapply!="yes" && reapply!="no")) return invalid("Action.StackOnReapply="+reapply);
        a.stackOnReapply=reapply=="yes";
    }
    const bool outgoing=skill.trigger=="onbeforedealdamage";
    a.target=field("Action.Target",outgoing || a.action=="stealpassive" || a.action=="consumetarget"?"Target":"Self");
    a.targetIsEventTarget=a.target=="target";
    if(a.target=="target") {
        if(skill.trigger=="ontakedamage" || skill.trigger=="onbeforetakedamage" || skill.trigger=="ondeath") a.target="attacker";
        else if(skill.trigger=="ondeploy" || skill.trigger=="onpromote" || skill.trigger=="onpromoteveteran" || skill.trigger=="onpromoteelite" || skill.trigger=="onpassivegained" || skill.trigger=="onbuildingupgrade") a.target="self";
        else a.target="victim";
    }
    bool positional=positionalAction(a.action);
    if(a.target!="self" && a.target!="self.occupants" && a.target!="attacker" && a.target!="victim" && !(a.target=="sender" && skill.trigger=="onsignal") && !(positional && a.target=="victimlocation"))
        return invalid("Action.Target="+a.target);
    if(a.target=="self.occupants" && (a.landFirst || a.action=="runscript"))
        return invalid("Self.Occupants requires a result applied to individual units");
    if(a.action=="setdamage" || a.action=="blockdamage") {
        if(!beforeHealthLoss(skill.trigger)) return invalid(a.action+" requires OnBeforeDealDamage or OnBeforeTakeDamage");
        if(a.target!=(outgoing?"victim":"self")) return invalid(a.action+" requires "+(outgoing?"Target or Victim":"Self"));
    }
    if(a.action=="damage" || a.action=="detonatewarhead") {
        auto id=field("Action.Warhead");
        if(id.empty() && a.action=="damage") id="Super";
        if(!id.empty()) a.warhead=WarheadTypeClass::Find(id.c_str());
        if(!a.warhead) return invalid("Action.Warhead="+id);
    }
    bool changeCounter=a.action=="changeresearchcounter";
    if(changeCounter) {
        a.counter=field("Action.Counter");
        if(!Research::counterProjects.contains(a.counter)) return invalid("Action.Counter="+a.counter);
        if(a.target!="self") return invalid("ChangeResearchCounter requires Action.Target=Self");
    } else if(!field("Action.Counter").empty()) return invalid("Action.Counter requires ChangeResearchCounter");
    bool setDamage=a.action=="setdamage";
    bool numeric=a.action=="heal" || a.action=="damage" || a.action=="givecredits" || a.action=="giveresearchpoints" || setDamage;
    auto amount=field("Action.Amount",a.action=="stealpassive"?"1":"0");
    bool formulaAmount=amount.starts_with("$(");
    if(formulaAmount && ((!numeric && !changeCounter) || !a.amountFormula.compile(section,"Action.Amount",amount)))
        return invalid("Action.Amount="+amount);
    if(formulaAmount) amount="0";
    auto healthPercent=field("Action.MaxHealthPercent");
    if(!healthPercent.empty()) {
        a.maxHealthPercent=number(healthPercent,true);
        if(a.action!="changemaxhealth" || a.maxHealthPercent<=0 || a.maxHealthPercent>10000 || amount!="0")
            return invalid("Action.MaxHealthPercent="+healthPercent);
    }
    if((changeCounter && !formulaAmount) || a.action=="changeammo" || a.action=="changemaxhealth" || a.action=="changepassivestack") {
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
    if(numeric) {
        if(setDamage) {
            auto raw=field("Action.Amount");
            if(raw.empty())
                return invalid("SetDamage requires Action.Amount");
            if(!formulaAmount) {
                auto first=raw.data(),last=first+raw.size();
                if(first!=last && *first=='+') ++first;
                int value=0;auto parsed=std::from_chars(first,last,value);
                if(first==last || parsed.ec!=std::errc{} || parsed.ptr!=last || value<0) return invalid("Action.Amount="+raw);
                a.amount=value;
            }
        } else if(!formulaAmount && a.amount<=0)
            return invalid("Action.Amount requires a positive integer or formula");
    } else if(a.action=="spawnunit" || a.action=="spawnbuilding" || a.action=="changeowner" || a.action=="transform") {
        if(a.action!="transform") {
            a.owner=field("Action.Owner","Self");
            bool playerSlot=a.owner.size()==7 && a.owner.starts_with("player") && a.owner[6]>='1' && a.owner[6]<='8';
            if(!playerSlot && !includes({"self","attacker","victim","neutral"},a.owner))
                return invalid("Action.Owner="+a.owner);
        }
        if(a.action=="spawnbuilding") {
            auto id=field("Action.BuildingType");
            a.buildingType=id.empty()?nullptr:BuildingTypeClass::Find(id.c_str());
            if(!BuildingSpawn::supported(a.buildingType)) return invalid("Action.BuildingType="+id+" (requires a standalone building, not an upgrade, wall or pavement)");
        } else if(a.action=="spawnunit" && !field("Action.UnitTypes").empty()) {
            if(!field("Action.UnitType").empty()) return invalid("Action.UnitType and Action.UnitTypes are mutually exclusive");
            auto names=split(field("Action.UnitTypes"));
            if(names.empty()) return invalid("Action.UnitTypes");
            for(const auto& name:names) {
                auto type=TechnoTypeClass::Find(name.c_str());
                if(!type || type->WhatAmI()==AbstractType::BuildingType) return invalid("Action.UnitTypes="+name);
                a.randomUnitTypes.push_back(type);
            }
            auto weights=field("Action.Weights");
            auto values=weights.empty()?vector<string>(names.size(),"1"):split(weights);
            if(values.size()!=names.size() || (!weights.empty() && std::count(weights.begin(),weights.end(),',')+1!=values.size())) return invalid("Action.Weights count");
            for(const auto& value:values) {
                int weight=0;auto parsed=std::from_chars(value.data(),value.data()+value.size(),weight);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || weight<0 || weight>INT_MAX-a.randomWeightTotal) return invalid("Action.Weights="+weights);
                a.randomWeightTotal+=weight;a.randomWeights.push_back(weight);
            }
            if(!a.randomWeightTotal) return invalid("Action.Weights all zero");
            auto count=field("Action.RandomCount","1");auto parsed=std::from_chars(count.data(),count.data()+count.size(),a.randomCount);
            if(parsed.ec!=std::errc{} || parsed.ptr!=count.data()+count.size() || a.randomCount<1 || a.randomCount>256) return invalid("Action.RandomCount (1..256)");
        } else if(a.action!="changeowner") {
            if(!field("Action.Weights").empty() || !field("Action.RandomCount").empty()) return invalid("Action.Weights and Action.RandomCount require Action.UnitTypes");
            auto id=field("Action.UnitType");
            a.unitType=id.empty()?nullptr:TechnoTypeClass::Find(id.c_str());
            if(!a.unitType || (a.unitType->WhatAmI()==AbstractType::BuildingType &&
                (a.action!="transform" || !BuildingSpawn::supported(static_cast<BuildingTypeClass*>(a.unitType))))) return invalid("Action.UnitType="+id);
        }
    } else if(auto stat=std::find(percentActions.begin(),percentActions.end(),a.action);stat!=percentActions.end()) {
        a.percentIndex=static_cast<size_t>(stat-percentActions.begin());
        auto text=field("Action.Percent");
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
    } else if(a.action=="forcemove") {
        if(!parseForceMove(a.forceMove,field)) return invalid("Action.ForceMove mode/offset, center, distance, height, AirborneHeight (0..512 cells), duration, BlowWeight (finite number), Tilt/IgnoreCollision/AffectsIronCurtain (yes/no) or Easing (Linear/EaseIn/EaseOut/EaseInOut)");
    } else if(a.action=="jumpto" || ejection || a.action=="chronoteleport") {
        if(ejection) {
            a.eject.range=number(field("Action.Eject.Range","8"));
            a.eject.openHeight=number(field("Action.Eject.OpenHeight","3"));
            auto speed=field("Action.Eject.HorizontalSpeed");
            if(!speed.empty()) {
                const auto end=speed.data()+speed.size();
                auto parsed=std::from_chars(speed.data(),end,a.eject.horizontalSpeed);
                if(parsed.ec!=std::errc{} || parsed.ptr!=end || !ForceMove::EjectSpec::validSpeed(a.eject.horizontalSpeed))
                    return invalid("Action.Eject.HorizontalSpeed (at least 1/256 cell per game second)");
            }
            a.eject.interval=number(field("Action.Eject.Interval","9"));
            if(!a.eject.valid()) return invalid("Action.Eject.Range/OpenHeight (1..128 cells) or Interval (1..3600 frames)");
        }
        if(a.action=="jumpto") {
            auto readEffect=[&](ForceMove::JumpEffect& effect,const char* animKey,const char* soundKey) {
                effect.anim=field(animKey);
                effect.sound=field(soundKey);
                if(!effect.anim.empty() && !AnimTypeClass::Find(effect.anim.c_str())) return invalid(string(animKey)+"="+effect.anim);
                return effect.sound.size()<=2047 || invalid(soundKey);
            };
            if(!readEffect(a.jump.takeoff,"Action.Jump.TakeoffAnim","Action.Jump.TakeoffSound") ||
               !readEffect(a.jump.land,"Action.Jump.LandAnim","Action.Jump.LandSound")) return false;
            a.jump.range=number(field("Action.Jump.Range","6"));
            a.jump.height=number(field("Action.Jump.Height","3"));
            a.jump.duration=number(field("Action.Jump.Duration","15"));
            if(a.jump.range<1 || a.jump.range>128 || a.jump.height<1 || a.jump.height>128 ||
               a.jump.duration<1 || a.jump.duration>3600) return invalid("Action.Jump.Range/Height (1..128 cells) or Duration (1..3600 frames)");
        }
        auto dest=field("Action.Destination");
        a.destinationIsEventTarget=dest=="target";
        if(!a.destinationIsEventTarget && (a.action=="chronoteleport" || !dest.empty())) {
            auto comma=dest.find(',');
            if(comma==string::npos) return invalid("Action.Destination="+dest+" (expected X,Y or Target)");
            int x=number(trim(dest.substr(0,comma))),y=number(trim(dest.substr(comma+1)));
            if(x<0 || x>=512 || y<0 || y>=512) return invalid("Action.Destination="+dest+" (expected X,Y in 0..511)");
            a.teleportCell={static_cast<short>(x),static_cast<short>(y)};
        }
    } else if(a.action=="removepickup") {
        auto range=hasPickupRange?field("Action.Range"):"0";
        a.pickupRange=number(range);
        if(a.pickupRange==-1 && a.target=="self.occupants") return invalid("Global RemovePickup has no occupant target");
        if(a.pickupRange<0 && range!="-1") return invalid("Action.Range="+range+" (expected -1 or a nonnegative integer)");
        auto types=hasPickupTypes?field("Action.PickupTypes"):"*";
        if(types!="*") {
            a.removePickupTypes=split(types);
            if(a.removePickupTypes.empty() || types.size()>=2047) return invalid("Action.PickupTypes="+types);
            for(const auto& id:a.removePickupTypes)
                if(id=="*" || !hasPickupType(id)) return invalid("Action.PickupTypes="+id);
        }
    } else if(a.action=="spawnpickup") {
        a.pickupType=field("Action.PickupType");
        if(!hasPickupType(a.pickupType)) return invalid("Action.PickupType="+a.pickupType);
    } else if(a.action=="spawncrate") {
        auto name=field("Action.CrateType","Random");
        if(name!="random") {
            const auto& names=Powerups::Effects;
            auto found=std::find_if(std::begin(names),std::end(names),[&](const char* type){return !_stricmp(type,name.c_str());});
            if(found==std::end(names)) return invalid("Action.CrateType="+name);
            a.crateType=static_cast<Powerup>(found-std::begin(names));
        }
    } else if(a.action=="sendsignal") {
        a.signal=field("Action.Signal");
        a.signalTarget=field("Action.Signal.Target");
        a.signalHouses=split(field("Action.Signal.Houses","Self"));
        auto range=field("Action.Signal.Range","0");
        auto parsed=std::from_chars(range.data(),range.data()+range.size(),a.signalRange);
        auto include=field("Action.Signal.IncludeSelf","no");
        auto value=field("Action.Signal.Value","0");
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
    } else if(a.action=="startunitorder" || a.action=="cancelunitorder") {
        if(a.action=="startunitorder" && (a.order.empty() || skill.trigger!="oncast"))
            return invalid("StartUnitOrder requires Trigger=Active and Action.Order");
    } else if(a.action=="runscript") {
        a.script=field("Action.Script");
        if(!a.script.ends_with(".lua")) a.script+=".lua";
        const auto* functions=scriptFunctions(a.script);
        if(!functions) return invalid("Action.Script="+a.script);
        auto name=field("Action.Function");
        for(const auto& fn:*functions) if(lower(fn)==name) {
            if(!a.function.empty()) return invalid("ambiguous Action.Function="+name);
            a.function=fn;
        }
        if(a.function.empty()) return invalid("Action.Function="+name);
        a.skillId=skill.id;
    } else if(a.action=="grantresearch") {
        if(!field("Action.Amount").empty()) return invalid("GrantResearch takes no amount");
        a.researchOptions=split(field("Action.Research"));
        if(a.researchOptions.size()!=1) return invalid("GrantResearch requires a single Action.Research ID");
    } else if(a.action=="grantrandomresearch" || a.action=="grantfreeresearch") {
        if(!field("Action.Amount").empty())
            return invalid(a.action+" grants one reward and does not use an amount");
        if(a.action=="grantfreeresearch") {
            const auto raw=field("Action.ResearchOptions");
            a.researchOptions=split(raw);
            if(!raw.empty() && a.researchOptions.empty()) return invalid("Action.ResearchOptions requires project IDs");
        }
        const auto rarity=field("Action.Rarity","All");
        if(rarity!="all") {
            const vector<string> names={"common","rare","epic","insane"};
            const auto found=std::find(names.begin(),names.end(),rarity);
            if(found==names.end()) return invalid("Action.Rarity="+rarity+" (All, Common, Rare, Epic, Insane)");
            a.researchRarity=static_cast<Research::Rarity>(found-names.begin());
        }
    } else if(a.action=="promote") {
        a.amount=number(field("Action.Levels","1"));
        if(a.amount<1 || a.amount>2) return invalid("Action.Levels (1 or 2)");
    } else if(a.action=="selfdestruct") {
        if(a.target!="self") return invalid("Action.Target="+a.target);
    } else if(a.action=="attachunit") {
        auto type=field("Action.UnitType");
        a.unitType=TechnoTypeClass::Find(type.c_str());
        if(!a.unitType || (a.unitType->WhatAmI()!=AbstractType::UnitType && a.unitType->WhatAmI()!=AbstractType::InfantryType))
            return invalid("AttachUnit requires an infantry or vehicle Action.UnitType");
        a.mountNode=field("Action.Node");a.mountSlot=field("Action.Slot");a.mountAnchor=field("Action.Anchor");
        if((!a.mountNode.empty() && !a.mountSlot.empty()) ||
           (!a.mountAnchor.empty() && ((!a.mountNode.empty() || a.mountSlot=="auto") || !includes({"body","turret"},a.mountAnchor))))
            return invalid("conflicting AttachUnit placement fields");
        if(!mountOffset(field("Action.FLH","0,0,0"),a.mountOffset)) return invalid("Action.FLH");
        auto fire=field("Action.TransportFire","followtransport");
        if(fire!="followtransport" && fire!="disabled") return invalid("Action.TransportFire");
        a.transportFire=fire=="followtransport";
        a.excludeParentPassives=split(field("Action.ExcludeParentPassives"));
    } else if(a.action=="removeattachedunit") {
        a.mountSlot=field("Action.Slot");
        if(a.mountSlot=="auto") return invalid("Action.Slot requires an installed slot name");
        auto type=field("Action.UnitType");
        if(!type.empty()) {
            a.unitType=TechnoTypeClass::Find(type.c_str());
            if(!a.unitType || (a.unitType->WhatAmI()!=AbstractType::UnitType && a.unitType->WhatAmI()!=AbstractType::InfantryType))
                return invalid("RemoveAttachedUnit requires an infantry or vehicle Action.UnitType");
        }
        auto count=field("Action.Count","1");
        if(count=="all") a.removeMountCount=INT_MAX;
        else {
            auto end=count.data()+count.size();
            auto parsed=std::from_chars(count.data(),end,a.removeMountCount);
            if(parsed.ec!=std::errc{} || parsed.ptr!=end || a.removeMountCount<1)
                return invalid("Action.Count requires a positive integer or All");
        }
    } else if(a.action=="consumetarget") {
        if(!field("Action.Amount").empty()) return invalid("ConsumeTarget takes one live target and no Action.Amount");
    } else if(a.action=="consumepassenger") {
        if(!field("Action.Amount").empty()) return invalid("ConsumePassenger removes one passenger and does not use Action.Amount");
    } else if(a.action=="blockdamage") {
        if(!field("Action.Amount").empty())
            return invalid("BlockDamage does not use Action.Amount");
    } else if(a.action=="removepassive" || a.action=="stealpassive") {
        auto list=field("Action.Passive");
        a.passiveList=split(list);
        if(a.passiveList.empty() || list.size()>=2047) return invalid("Action.Passive="+list);
        if(a.action=="stealpassive" && a.amount<=0) return invalid("Action.Amount must be a positive stack count");
    } else if(a.action=="grantpassive" || a.action=="changepassivestack" || a.action=="resetstatetimer") {
        a.passive=field("Action.Passive");
        if(a.passive.empty()) return invalid("Action.Passive");
    } else if(a.action=="playanim") {
        auto id=field("Action.Anim");
        if(id.empty() || !(a.playAnim=AnimTypeClass::Find(id.c_str()))) return invalid("Action.Anim="+id);
    } else if(a.action=="playsound") {
        a.sound=field("Action.Sound");
        if(a.sound.empty()) return invalid("Action.Sound");
    } else if(a.action=="spawnparticlesystem") {
        auto id=field("Action.ParticleSystem");
        if(id.empty() || !(a.particleSystem=ParticleSystemTypeClass::Find(id.c_str()))) return invalid("Action.ParticleSystem="+id);
    } else if(a.action=="shakescreen") {
        a.shakeX=number(field("Action.ShakeX","0"));a.shakeY=number(field("Action.ShakeY","4"));
        if(a.shakeX<0 || a.shakeX>32 || a.shakeY<0 || a.shakeY>32 || (!a.shakeX && !a.shakeY))
            return invalid("Action.ShakeX / Action.ShakeY require 0..32 with a nonzero axis");
        auto local=field("Action.ShakeLocal","yes");
        if(local!="yes" && local!="no") return invalid("Action.ShakeLocal="+local);
        a.shakeLocal=local=="yes";
    } else if(a.action=="fireweapon" || a.action=="replaceweapon") {
        a.weapon=field("Action.Weapon");
        if(a.weapon.empty() || !WeaponTypeClass::Find(a.weapon.c_str())) return invalid("Action.Weapon="+a.weapon);
        if(a.action=="replaceweapon" && (skill.trigger!="onbeforefire" || a.target!="self")) return invalid("ReplaceWeapon requires OnBeforeFire and Self");
    } else if(!changeCounter && a.action!="detonatewarhead" && a.action!="changeammo" && a.action!="changemaxhealth" && a.action!="applyironcurtain") return invalid("Action="+a.action);
    if(a.formula.expression.usesLiveState() || a.amountFormula.expression.usesLiveState() || a.signalFormula.expression.usesLiveState())
        return invalid("Live state formula variables require WhileState");
    if((a.formula.expression.usesDamage() || a.amountFormula.expression.usesDamage()) &&
       !damageEvent(skill.trigger)) return invalid("Event.Damage requires a damage trigger");
    if((a.formula.expression.usesSignal() || a.amountFormula.expression.usesSignal() || a.signalFormula.expression.usesSignal()) &&
       skill.trigger!="onsignal") return invalid("Sender and Event.SignalValue require OnSignal");
    skill.actions.push_back(std::move(a));
    return true;
}
bool readActions(Skill& skill,const std::set<string>& keys) {
    bool random=keys.contains("randomactions");
    if(!random && (keys.contains("randomactions.weights") || keys.contains("randomactions.count"))) return invalidField(skill.id,"RandomActions parameters require RandomActions");
    if(random && (keys.contains("actions") || skill.trigger=="onbeforefire" || beforeHealthLoss(skill.trigger) || skill.trigger=="oncast")) return invalidField(skill.id,"RandomActions incompatible with this action or trigger");
    if(!random && !keys.contains("actions")) return readAction(skill,skill.id);
    for(const auto& key:keys)
        if(key=="action" || key.starts_with("action.")) return invalidField(skill.id,"Actions + "+key);
    const string list=read(skill.id,random?"RandomActions":"Actions");
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
        if(end==string::npos) break;
        start=end+1;
    }
    if(random) {
        auto raw=read(skill.id,"RandomActions.Weights");
        auto weights=raw.empty()?vector<string>(skill.actions.size(),"1"):split(raw);
        if(weights.size()!=skill.actions.size() || (!raw.empty() && std::count(raw.begin(),raw.end(),',')+1!=weights.size())) return invalidField(skill.id,"RandomActions.Weights count");
        for(const auto& text:weights) {
            int weight=0;auto result=std::from_chars(text.data(),text.data()+text.size(),weight);
            if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size() || weight<0 || weight>INT_MAX-skill.randomActionTotal) return invalidField(skill.id,"RandomActions.Weights="+raw);
            skill.randomActionTotal+=weight;skill.randomActionWeights.push_back(weight);
        }
        if(!skill.randomActionTotal) return invalidField(skill.id,"RandomActions.Weights all zero");
        auto count=read(skill.id,"RandomActions.Count","1");auto result=std::from_chars(count.data(),count.data()+count.size(),skill.randomActionCount);
        if(result.ec!=std::errc{} || result.ptr!=count.data()+count.size() || skill.randomActionCount<1 || skill.randomActionCount>256) return invalidField(skill.id,"RandomActions.Count (1..256)");
    }
    return true;
}
bool readAnimations(Skill& s,const std::set<string>& keys) {
    if(keys.contains("tint.strength") && !keys.contains("tint.color"))
        return invalidField(s.id,"Tint.Strength requires Tint.Color");
    if(keys.contains("tint.color")) {
        const auto strengthText=keys.contains("tint.strength")?read(s.id,"Tint.Strength"):"50";
        const int strength=number(strengthText,true);
        if(strength<0 || strength>100) return invalidField(s.id,"Tint.Strength="+strengthText+" (expected 0..100%)");
        auto value=read(s.id,"Tint.Color");
        const auto rgb=ConfigText::rgb(value);
        if(!rgb) return invalidField(s.id,"Tint.Color="+value+" (expected R,G,B; each 0..255)");
        s.tintStrength=strength;
        s.tint=ColorStruct{static_cast<BYTE>((*rgb)[0]),static_cast<BYTE>((*rgb)[1]),static_cast<BYTE>((*rgb)[2])};
    }
    for(auto key:{"Anim","TriggerAnim","GainAnim"}) {
        if(!keys.contains(lower(key))) continue;
        auto id=read(s.id,key);
        auto type=id.empty()?nullptr:AnimTypeClass::Find(id.c_str());
        if(!type) return invalidField(s.id,string(key)+"="+id);
        if(key==std::string_view("Anim")) s.anim=type;
        else if(key==std::string_view("GainAnim")) s.gainAnim=type;
        else s.triggerAnim=type;
    }
    if(keys.contains("anim.always")) {
        auto value=read(s.id,"Anim.Always");
        if(!s.anim || (value!="yes" && value!="no")) return invalidField(s.id,"Anim.Always");
        s.animAlways=value=="yes";
    }
    if(keys.contains("anim.whileready")) {
        auto value=read(s.id,"Anim.WhileReady");
        if(!s.anim || s.cooldown<=0 || s.trigger.empty() || s.trigger=="whilestate" || s.trigger=="oncast" ||
           (value!="yes" && value!="no")) return invalidField(s.id,"Anim.WhileReady requires Anim and a passive event with a cooldown");
        s.animWhileReady=value=="yes";
    }
    s.animTarget=read(s.id,"TriggerAnim.Target","Self");
    if((keys.contains("triggeranim.target") && !s.triggerAnim) ||
       (s.animTarget!="self" && s.animTarget!="target")) return invalidField(s.id,"TriggerAnim.Target");
    return true;
}
} // namespace PassiveRules
