#include "Research.h"
#include "PassiveTypes.h"
#include "ConfigText.h"
#include <AnimTypeClass.h>
#include <BuildingTypeClass.h>
#include <ParticleSystemTypeClass.h>
#include <Powerups.h>
#include <WeaponTypeClass.h>

namespace {
using namespace PassiveRules;
using ConfigText::displayText;

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
        if(a.action=="removeattachedunit") return L"移除挂载子单位";
        if(a.action=="attachunit" && a.unitType) return L"挂载子单位："+displayText(a.unitType->ID);
        if(a.action=="spawnunit") return a.unitType?L"生成单位："+displayText(a.unitType->ID):L"随机生成单位";
        if(a.action=="spawnbuilding") return L"生成建筑："+displayText(a.buildingType->ID);
        if(a.action=="transform") return L"原地变形为："+(a.unitType->UIName && *a.unitType->UIName ?
            std::wstring(a.unitType->UIName) : displayText(a.unitType->ID));
        if(a.action=="changeowner") return L"修改目标所属方";
        if(a.action=="changemaxhealth") return a.maxHealthPercent?
            L"永久提高当前最大生命的 "+std::to_wstring(a.maxHealthPercent)+L"%":
            L"永久改变最大生命："+std::to_wstring(a.amount)+L" 点";
        if(a.action=="playanim") return L"播放动画 "+displayText(a.playAnim->ID);
        if(a.action=="playsound") return L"播放音效";
        if(a.action=="shakescreen") return L"屏幕抖动";
        if(a.action=="spawnparticlesystem") return L"生成粒子系统 "+displayText(a.particleSystem->ID);
        if(a.action=="forcemove") return L"强制位移 "+std::to_wstring(a.forceMove.duration)+L" 帧";
        if(a.action=="chronoteleport" && a.destinationIsEventTarget) return L"超时空传送至事件目标附近";
        if(a.action=="chronoteleport") return L"超时空传送至 ("+std::to_wstring(a.teleportCell.X)+L", "+std::to_wstring(a.teleportCell.Y)+L") 附近";
        if(a.action=="applyironcurtain") return L"施加铁幕 "+std::to_wstring(a.duration)+L" 帧";
        if(a.action=="changeammo") return L"弹药变化："+std::to_wstring(a.amount);
        if(a.action=="removepickup") return L"清除拾取物";
        if(a.action=="spawnpickup") return L"生成拾取物："+displayText(a.pickupType);
        if(a.action=="spawncrate") {
            if(a.crateType==randomCrate) return L"生成一个随机升级箱";
            return L"生成升级箱："+displayText(Powerups::Effects[static_cast<unsigned>(a.crateType)]);
        }
        if(a.action=="selfdestruct") return L"触发后摧毁自身";
        if(a.action=="grantpassive") return L"授予被动："+displayText(a.passive)+(a.duration<0?L"（永久）":L"（"+std::to_wstring(a.duration)+L" 帧）")+(a.stackOnReapply?L"；重复授予加 1 层":L"");
        if(a.action=="promote") return L"晋升 "+std::to_wstring(a.amount)+L" 级";
        if(a.action=="grantresearch" && !a.researchOptions.empty()) return L"直接获得科研："+displayText(a.researchOptions.front());
        if(a.action=="grantfreeresearch") return L"获得一次免费科研，可从候选科技中选择一项";
        if(a.action=="grantrandomresearch") {
            return a.researchRarity?std::wstring(L"随机获得一项")+Research::rarityName(*a.researchRarity)+L"科技":L"随机获得一项科技（全部稀有度）";
        }
        if(a.action=="changeresearchcounter") return L"修改科研计数："+displayText(a.counter);
        if(a.action=="runscript") return L"执行技能脚本："+displayText(a.script)+L"."+displayText(a.function);
        if(a.action=="removepassive") return L"移除目标的指定被动";
        if(a.action=="stealpassive") return L"夺取目标的指定被动，每项最多 "+std::to_wstring(a.amount)+L" 层";
        if(a.action=="resetstatetimer") return L"重置被动持续计时："+displayText(a.passive);
        if(a.action=="changepassivestack") return L"修改被动层数："+displayText(a.passive)+L" "+(a.amount>0?L"+":L"")+std::to_wstring(a.amount);
        if(a.action=="consumetarget") return L"直接消耗目标";
        if(a.action=="consumepassenger") return L"消耗一名乘客";
        if(a.action=="blockdamage") return L"抵挡本次伤害";
        if(a.action=="setdamage") return a.amountFormula.expression.empty() ?
            L"本次伤害设为 "+std::to_wstring(a.amount) : L"按公式调整本次伤害";
        if(a.action=="fireweapon") return L"向目标发射一枚指定武器弹体";
        if(a.action=="replaceweapon") return L"开火时替换武器："+displayText(a.weapon);
        if(a.action=="detonatewarhead") return L"在目标位置引爆指定弹头";
        text=a.action=="heal"?L"恢复生命":a.action=="damage"?L"造成伤害":a.action=="giveresearchpoints"?L"获得科研点":L"获得资金";
        if(!a.amountFormula.expression.empty()) return text+L"（按公式计算）";
        text+=L"：{amount}";
    }
    replaceNumber(text,L"{amount}",a.amount);
    return text;
}
static std::wstring formatDetail(const Research::Option& option,int house) {
    if(option.detailFormulas.empty()) return option.detail;
    std::wstring text;
    size_t pos=0;
    for(const auto& part:option.detailFormulas) {
        text.append(option.detail,pos,part.start-pos);
        auto value=part.expression.evaluateResearch(house,Research::counter);
        if(!value || !ConfigText::appendTooltipNumber(text,*value,option.detailDecimals)) text+=L'—';
        pos=part.end;
    }
    text.append(option.detail,pos);
    return text;
}
static std::wstring describeResearchEffect(const Research::Option& o,const std::wstring& custom,wchar_t (&conditions)[96]) {
    using Research::Effect;
    if(o.effect!=Effect::Passive) {
        swprintf_s(conditions,L"即时生效  ·  每位玩家限购一次");
        auto detail=custom;
        if(o.effect==Effect::TypeCost || o.effect==Effect::CategoryCost) {
            if(!detail.empty()) return detail;
            return std::wstring(L"生产价格")+(o.cost.delta<0?L"降低 ":L"提高 ")+
                std::to_wstring(std::abs(o.cost.delta))+(o.cost.percent?L"%":L" 资金");
        }
        if(detail.empty()) detail=o.effect==Effect::Credits?L"立即获得 {credits} 资金":
            o.effect==Effect::FreeResearch?L"获得一次免费科研，从候选科技中选择一项":
            o.effect==Effect::RandomResearch?L"随机获得 "+std::to_wstring(o.reward.count)+L" 项符合条件且尚未拥有的科技":
            o.effect==Effect::Technology?L"获得指定单位或建筑的生产权限":
            o.effect==Effect::ReplaceProduction?L"替换可生产型号，保留原型号的建造前置":
            o.effect==Effect::ConvertVehicle?L"转换当前全部符合条件的己方载具":L"转换当前全部符合条件的己方步兵";
        replaceNumber(detail,L"{credits}",o.credits);
        return detail;
    }
    const auto* existing=findSkill(o.passive);
    if(!existing) {
        swprintf_s(conditions,L"原被动配置不可用");
        return custom.empty()?L"已研究项目的被动配置不存在":custom;
    }
    const auto& s=*existing;
    if(s.trigger.empty()) {
        swprintf_s(conditions,L"持有被动期间显示");
        return custom.empty()?L"在单位身上挂载动画":custom;
    }
    if(s.trigger=="whilestate") {
        if(s.stateDelayFrames) swprintf_s(conditions,L"连续满足 %u 帧后生效，条件中断即重置",s.stateDelayFrames);
        else swprintf_s(conditions,L"条件满足期间持续生效");
        if(!custom.empty()) return custom;
        wchar_t info[96]{};
        constexpr std::array names={L"移速",L"火力",L"护甲",L"武器射程",L"射速",L"建筑电力",L"击杀经验"};
        static_assert(names.size()==stateMultiplierKeys.size());
        std::wstring summary;
        for(size_t k=0;k<s.multipliers.size();++k) if(s.multipliers[k]!=1.0) {
            swprintf_s(info,L"%s ×%.2f  ",names[k],s.multipliers[k]);
            summary+=info;
        }
        for(const auto& entry:s.stateFormulas) summary+=std::wstring(names[entry.first])+L"按公式变化  ";
        if(s.triggerAnim) summary+=L"进入状态时播放动画  ";
        if(s.projectileScale!=100) summary=L"弹体、爆炸和范围 "+std::to_wstring(s.projectileScale)+L"%  "+summary;
        if(s.visualScale!=100) summary=L"体型 "+std::to_wstring(s.visualScale)+L"%  "+summary;
        if(s.extraShotCount>0) summary+=L"额外射击 "+std::to_wstring(s.extraShotCount)+L" 个目标  ";
        if(!s.multiShotFormula.expression.empty()) summary+=L"额外射击目标数按公式变化  ";
        if(s.berserk) summary=L"进入混乱状态  "+summary;
        if(s.emp) summary=L"进入 EMP 停机  "+summary;
        if(s.aura.range) {
            summary+=s.aura.range==-1?L"全图光环 ":L"光环 "+std::to_wstring(s.aura.range)+L" 格 ";
            for(const auto& id:s.aura.passives) summary+=displayText(id)+L" ";
        }
        for(const auto& id:s.linkedPassives) summary+=L" 关联被动 "+displayText(id);
        for(const auto& id:s.spawnPassives) summary+=L" 子机获得 "+displayText(id);
        if(s.immune) summary=L"免疫常规伤害  "+summary;
        if(s.mindImmune) summary=L"免疫心灵控制  "+summary;
        if(s.cloakable) summary=L"获得隐形能力  "+summary;
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
        text=custom.empty()?(s.triggerAnim?L"触发时播放动画":L"持有期间显示动画"):custom;
    } else if(!custom.empty()) {
        text=describeAction(s.actions.front(),custom);
    } else {
        for(const auto& action:s.actions) {
            if(!text.empty()) text+=L'\n';
            text+=describeAction(action,{});
        }
    }
    replaceNumber(text,L"{count}",s.count); replaceNumber(text,L"{chance}",s.chance); replaceNumber(text,L"{cooldown}",s.cooldown);
    return text;
}
} // namespace

std::wstring Research::bondRewardText(const StageEffect& e) {
    switch(e.effect) {
        case Effect::Passive: return L"赋予被动 "+displayText(e.passive);
        case Effect::Technology: return L"解锁 "+displayText(e.technology);
        case Effect::ConvertInfantry: case Effect::ConvertVehicle: return displayText(e.from)+L" 转换为 "+displayText(e.to);
        case Effect::ReplaceProduction: return L"生产替换 "+displayText(e.from)+L" → "+displayText(e.to);
        default: {
            Option part;part.effect=e.effect;part.cost=e.cost;part.reward=e.reward;part.credits=e.credits;
            wchar_t requirement[96]{};
            return describeResearchEffect(part,{},requirement);
        }
    }
}
std::wstring Research::describe(int option,int house) {
    wchar_t conditions[96]{};
    const auto& o=options[option];
    const auto custom=formatDetail(o,house);
    if(o.stagesOnly) return custom.empty()?L"达成阶段条件后获得科研奖励":custom;
    if(!o.effectList) return describeResearchEffect(o,custom,conditions);
    swprintf_s(conditions,L"多个效果按顺序生效");
    if(!custom.empty()) return custom;
    std::wstring result;
    for(const auto& effect:o.effects) {
        Option part;part.effect=effect.effect;part.passive=effect.passive;part.types=effect.types;
        part.cost=effect.cost;part.reward=effect.reward;part.credits=effect.credits;part.technology=effect.technology;part.from=effect.from;part.to=effect.to;
        wchar_t requirement[96]{};
        if(!result.empty()) result+=L'\n';
        result+=describeResearchEffect(part,{},requirement);
        if(effect.effect==Effect::Passive) {result+=L"（";result+=requirement;result+=L"）";}
    }
    return result;
}
