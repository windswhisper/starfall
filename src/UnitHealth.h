#pragma once
#include <TechnoClass.h>
#include <algorithm>
#include <cstdint>
#include <map>

namespace UnitHealth {
// YR and other extensions divide ObjectClass::Health by the shared type's Strength.
// Keep that field on the native scale, and keep exact hit points for combat here.
struct Pool {
    int base,maximum,current,native;
    int project(int hp) const {
        if(hp<=0) return 0;
        if(hp>=maximum || base==1) return base;
        return static_cast<int>(std::clamp<int64_t>((int64_t(hp)*base+maximum/2)/maximum,1,base-1));
    }
    void set(TechnoClass* unit,int hp) {
        current=std::clamp(hp,0,maximum);
        native=project(current);
        unit->Health=unit->EstimatedHealth=native;
    }
    void sync(TechnoClass* unit,int strength) {
        if(base!=strength && strength>0) {
            // In-place conversions keep the permanent point bonus on the new type.
            maximum=static_cast<int>(std::clamp<int64_t>(int64_t(maximum)-base+strength,1,INT_MAX));
            base=strength;
            int64_t scaled=(int64_t(std::clamp(unit->Health,0,base))*maximum+base/2)/base;
            set(unit,static_cast<int>(std::max<int64_t>(unit->Health>0?1:0,scaled)));
        } else if(unit->Health!=native) {
            // Native regeneration and external full-heal effects can write Health directly.
            if(unit->Health<=0) set(unit,0);
            else if(unit->Health>=base) set(unit,maximum);
            else set(unit,static_cast<int>(std::clamp<int64_t>(int64_t(current)+unit->Health-native,1,maximum)));
        }
    }
};
inline std::map<DWORD,Pool> pools;
struct Values {int current,maximum;};

inline Pool* find(DWORD id) {
    if(pools.empty()) return nullptr;
    auto it=pools.find(id);
    return it==pools.end()?nullptr:&it->second;
}
inline Values read(TechnoClass* unit,int strength) {
    if(auto p=find(unit->UniqueID)) {
        p->sync(unit,strength);
        return {p->current,p->maximum};
    }
    return {unit->Health,strength};
}
inline Values read(TechnoClass* unit) {return read(unit,unit->GetTechnoType()->Strength);}
inline int current(TechnoClass* unit) {
    if(unit->Health<=0 || !find(unit->UniqueID)) return unit->Health;
    return read(unit).current;
}
inline bool change(TechnoClass* unit,int amount) {
    int strength=unit->GetTechnoType()->Strength;
    if(strength<=0 || unit->Health<=0) return false;
    auto old=read(unit,strength);
    int next=static_cast<int>(std::clamp<int64_t>(int64_t(old.maximum)+amount,1,INT_MAX));
    if(next==old.maximum) return false;
    int current=static_cast<int>(std::clamp<int64_t>(
        int64_t(old.current)+std::max(0,next-old.maximum),1,next));
    if(next==strength) {
        pools.erase(unit->UniqueID);
        unit->Health=unit->EstimatedHealth=current;
    } else {
        auto& p=pools[unit->UniqueID];
        p.base=strength;p.maximum=next;p.set(unit,current);
    }
    unit->MarkForRedraw();
    return true;
}
inline void heal(TechnoClass* unit,int current) {
    if(auto p=find(unit->UniqueID)) p->set(unit,current);
    else unit->Health=unit->EstimatedHealth=current;
}
inline void syncAll() {
    if(pools.empty()) return;
    for(auto unit:TechnoClass::Array) if(unit) {
        if(auto p=find(unit->UniqueID)) p->sync(unit,unit->GetTechnoType()->Strength);
    }
}
}
