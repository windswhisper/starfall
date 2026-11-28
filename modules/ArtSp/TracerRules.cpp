#include "Tracers.h"
#include "../../src/ConfigText.h"
#include <CCINIClass.h>
#include <FootClass.h>
#include <BulletClass.h>
#include <WeaponTypeClass.h>
#include <climits>
#include <unordered_map>

namespace ArtSp::Tracers {
namespace {
struct Binding { Style style; int every=1; };
std::unordered_map<WeaponTypeClass*,Binding> bindings;
std::unordered_map<unsigned,std::unordered_map<WeaponTypeClass*,int>> countdowns;

std::string field(CCINIClass& ini,const char* section,const char* key,const char* fallback) {
    char text[512]{};
    ini.ReadString(section,key,fallback,text);
    return ConfigText::trim(text);
}
bool integer(CCINIClass& ini,const char* section,const char* key,int limit,int& value) {
    auto raw=field(ini,section,key,std::to_string(value).c_str());
    int parsed=ConfigText::number(raw);
    if(parsed<1 || parsed>limit) return false;
    value=parsed;
    return true;
}
bool rgb(CCINIClass& ini,const char* section,const char* key,ColorStruct& color) {
    auto fallback=std::to_string(color.R)+","+std::to_string(color.G)+","+std::to_string(color.B);
    auto raw=field(ini,section,key,fallback.c_str());
    size_t start=0;
    for(auto channel:{&color.R,&color.G,&color.B}) {
        auto end=raw.find(',',start);
        if((end==std::string::npos)!=(channel==&color.B)) return false;
        int n=ConfigText::number(ConfigText::trim(raw.substr(start,end-start)));
        if(n<0 || n>255) return false;
        *channel=static_cast<unsigned char>(n);
        if(end!=std::string::npos) start=end+1;
    }
    return true;
}
}

void clearRules() {
    countdowns.clear();
    bindings.clear();
}
void readRules() {
    clearRules();
    auto rules=CCINIClass::INI_Rules;
    if(!rules) return;
    auto& art=CCINIClass::INI_Art;
    for(auto weapon:WeaponTypeClass::Array) {
        auto id=field(*rules,weapon->ID,"Tracer","");
        if(id.empty()) continue;
        Binding binding;
        auto& style=binding.style;
        // The INI reader caches its last section name by pointer. Pass the name
        // owned by the art section, not the temporary weapon setting.
        const auto artSection=art.GetSection(id.c_str());
        if(!artSection) {
            ConfigText::configLog(std::string("ArtSp: [")+weapon->ID+"] Tracer="+id+" - missing artmd.ini section");
            continue;
        }
        const auto section=artSection->Name;
        const auto gradient=ConfigText::lower(field(art,section,"Gradient","no"));
        style.gradient=gradient=="yes";
        std::string error;
        if(!integer(*rules,weapon->ID,"Tracer.Every",INT_MAX,binding.every)) error="invalid Tracer.Every";
        else if(!rgb(art,section,"Color",style.color)) error="invalid Color";
        else if(!style.gradient && gradient!="no") error="invalid Gradient (expected yes or no)";
        else if(style.gradient && !rgb(art,section,"TailColor",style.tailColor)) error="invalid TailColor";
        else if(!integer(art,section,"Length",4096,style.length)) error="invalid Length";
        else if(!integer(art,section,"Width",8,style.width)) error="invalid Width";
        else if(!integer(art,section,"Speed",INT_MAX,style.speed)) error="invalid Speed";
        if(error.empty()) bindings.emplace(weapon,binding);
        else ConfigText::configLog(std::string("ArtSp: [")+weapon->ID+"] Tracer="+id+" - "+error);
    }
}
const Style* select(unsigned firer,WeaponTypeClass* weapon) {
    auto it=bindings.find(weapon);
    if(it==bindings.end()) return nullptr;
    const auto& binding=it->second;
    if(binding.every>1) {
        int& remaining=countdowns[firer][weapon];
        if(remaining>0) { --remaining; return nullptr; }
        remaining=binding.every-1;
    }
    return &binding.style;
}
void forget(unsigned firer) { countdowns.erase(firer); }
const Style* onShot(unsigned firer,BulletClass* bullet,const CoordStruct& muzzle,unsigned frame) {
    const Style* style=bullet?select(firer,bullet->WeaponType):nullptr;
    if(!style) return nullptr;
    CoordStruct end=bullet->TargetCoords;
    if(bullet->Type->Inviso && bullet->Type->FlakScatter) end=bullet->Location;
    else if(auto target=bullet->Target) {
        if((target->AbstractFlags&AbstractFlags::Object)!=AbstractFlags::None)
            end=static_cast<ObjectClass*>(target)->GetTargetCoords();
        else end=target->GetCenterCoords();
    }
    launch(*style,muzzle,end,frame);
    return style;
}
}
