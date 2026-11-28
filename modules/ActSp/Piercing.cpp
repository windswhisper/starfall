#include "Piercing.h"
#include "ProjectileScript.h"
#include "EntityWalls.h"
#include "../../src/ConfigText.h"
#include "../../src/ProjectileShield.h"
#include <BulletClass.h>
#include <BuildingClass.h>
#include <CCINIClass.h>
#include <HouseClass.h>
#include <TargetClass.h>
#include <WarheadTypeClass.h>
#include <WeaponTypeClass.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <map>
#include <cstring>
#include <set>
#include <vector>

namespace Piercing {
namespace {
struct Rules {bool enabled=false; int life=0; double radius=0,falloff=1,floor=0; WarheadTypeClass* endWarhead=nullptr;};
struct Flight {
    CoordStruct origin{};
    BulletVelocity step{};
    int age=0,life=0;
    double factor=1;
    std::set<DWORD> hit;
};
std::map<BulletTypeClass*,Rules> types;
std::map<DWORD,Flight> flights;
double setting(CCINIClass* ini,const char* id,const char* key,const char* fallback) {
    char value[64]{};
    ini->ReadString(id,key,fallback,value);
    double number=0;
    auto end=value+std::strlen(value);
    auto parsed=std::from_chars(value,end,number);
    return parsed.ec==std::errc{} && parsed.ptr==end?number:-1;
}
const Rules& rules(BulletTypeClass* type) {
    auto [it,added]=types.try_emplace(type);
    auto ini=CCINIClass::INI_Rules;
    if(added && ini && !type->Inviso && ini->ReadBool(type->ID,"Piercing.Enabled",false)) {
        auto& s=it->second;
        s.life=ini->ReadInteger(type->ID,"Piercing.Lifetime",30);
        s.radius=setting(ini,type->ID,"Piercing.Radius","0.5")*256;
        s.falloff=setting(ini,type->ID,"Piercing.DamageFalloff","1");
        s.floor=setting(ini,type->ID,"Piercing.MinDamageMultiplier","0");
        char warhead[64]{};
        ini->ReadString(type->ID,"Piercing.EndWarhead","",warhead);
        if(*warhead && _stricmp(warhead,"none")) {
            s.endWarhead=WarheadTypeClass::Find(warhead);
            if(!s.endWarhead) ConfigText::configLog(std::string("[")+type->ID+"] unknown Piercing.EndWarhead="+warhead);
        }
        s.enabled=s.radius>=0 && s.radius<=512*256 && s.falloff>=0 && s.falloff<=1 && s.floor>=0 && s.floor<=1;
    }
    return it->second;
}

using Point=std::array<double,3>;
// On each interval between box faces the squared distance to the box is
// quadratic. Its first root gives an exact contact, including rounded corners.
double contact(Point from,Point delta,Point lo,Point hi,double radius) {
    std::array<double,8> cuts{0,1};
    int n=2;
    for(int i=0;i<3;++i) {
        double end=from[i]+delta[i];
        if(std::max(end,from[i])+radius<lo[i] || std::min(end,from[i])-radius>hi[i]) return 2;
        if(delta[i]) for(double face:{lo[i],hi[i]}) {
            double t=(face-from[i])/delta[i];
            if(t>0 && t<1) cuts[n++]=t;
        }
    }
    std::sort(cuts.begin(),cuts.begin()+n);
    for(int j=1;j<n;++j) {
        const double start=cuts[j-1],end=cuts[j];
        if(start==end) continue;
        double a=0,b=0,c=-radius*radius;
        for(int i=0;i<3;++i) {
            const double middle=from[i]+delta[i]*(start+end)*0.5;
            if(middle>=lo[i] && middle<=hi[i]) continue;
            const double gap=from[i]+delta[i]*start-std::clamp(middle,lo[i],hi[i]);
            a+=delta[i]*delta[i]; b+=gap*delta[i]; c+=gap*gap;
        }
        if(c<=0) return start;
        double disc=b*b-a*c;
        if(b<0 && disc>=0) {
            double offset=c/(-b+std::sqrt(disc));
            if(offset<=end-start) return start+offset;
        }
    }
    return 2;
}
double firstContact(TechnoClass* unit,Point from,Point delta,double radius) {
    if(unit->WhatAmI()!=AbstractType::Building) {
        auto p=unit->GetCoords();
        Point center{double(p.X),double(p.Y),double(p.Z)};
        return contact(from,delta,center,center,radius);
    }
    auto b=static_cast<BuildingClass*>(unit);
    const auto anchor=b->GetMapCoords();
    double z=b->Location.Z,top=z+std::max(1,b->Type->Height)*double(Unsorted::LevelHeight);
    double best=2;
    for(auto p=b->GetFoundationData(false);p && *p!=CellStruct{0x7fff,0x7fff};++p) {
        double x=(anchor.X+p->X)*256,y=(anchor.Y+p->Y)*256;
        best=std::min(best,contact(from,delta,{x,y,z},{x+256,y+256,top},radius));
    }
    return best;
}
}

void reset() { types.clear();flights.clear(); }
void forget(DWORD id) { flights.erase(id); }
bool enabled(BulletTypeClass* type) { return type && rules(type).enabled; }
void startScripted(BulletClass* bullet,int scriptLife) {
    if(!enabled(bullet->Type)) return;
    // The script owns motion and age. The existing piercing save record retains
    // the damage factor and hit history under the same bullet ID.
    Flight flight;
    flight.life=scriptLife;
    flights.insert_or_assign(bullet->UniqueID,std::move(flight));
}
bool active(DWORD id) { return flights.contains(id); }
WarheadTypeClass* endWarhead(BulletClass* b) { return rules(b->Type).endWarhead; }
void detonateEnd(BulletClass* b,const CoordStruct& at,double interception,DWORD directTarget) {
    if(auto wh=endWarhead(b)) {
        double factor=flights.at(b->UniqueID).factor*interception;
        ProjectileShield::endBlast(b,at,wh,factor,directTarget);
    }
}
double interceptionFactor(BulletClass* bullet,DWORD target) {
    const auto& f=flights.at(bullet->UniqueID);
    return f.hit.contains(target)?0:f.factor;
}

void sweep(BulletClass* bullet,const CoordStruct& next,double stop,TechnoClass* blocker) {
    auto& flight=flights.at(bullet->UniqueID);
    const auto& s=rules(bullet->Type);
    auto house=EntityWalls::firingHouse(bullet);
    auto eligible=[&](TechnoClass* u) {
        return u && u!=bullet->Owner && u!=blocker && u->IsAlive && u->Health>0 && !u->InLimbo && u->IsOnMap &&
            !flight.hit.contains(u->UniqueID) && (u->IsInAir()?bullet->Type->AA:bullet->Type->AG) &&
            allowed(bullet->WH,house,u);
    };
    const auto p=bullet->Location;
    Point start{double(p.X),double(p.Y),double(p.Z)},delta{double(next.X)-p.X,double(next.Y)-p.Y,double(next.Z)-p.Z};
    struct Candidate { double time; DWORD id; TargetClass ref; };
    std::vector<Candidate> targets;
    for(auto u:TechnoClass::Array) {
        if(!u || u==blocker || !u->IsOnMap || u->InLimbo || flight.hit.contains(u->UniqueID)) continue;
        double t=firstContact(u,start,delta,s.radius);
        if(t<=1 && t<stop && eligible(u)) targets.push_back({t,u->UniqueID,TargetClass(u)});
    }
    std::sort(targets.begin(),targets.end(),[](const auto& a,const auto& b){return a.time==b.time?a.id<b.id:a.time<b.time;});
    for(auto& hit:targets) {
        auto u=hit.ref.As_Techno();
        if(!eligible(u)) continue;
        auto base=(int64_t(bullet->Health)*bullet->DamageMultiplier)>>8;
        int amount=int(std::round(std::clamp(base*flight.factor,double(INT_MIN),double(INT_MAX))));
        if(!amount) continue;
        int hp=u->Health;
        auto result=u->ReceiveDamage(&amount,0,bullet->WH,bullet->Owner,false,false,house);
        auto survivor=hit.ref.As_Techno();
        if(result==DamageState::Unaffected && survivor && survivor->Health==hp) continue;
        flight.hit.insert(hit.id);
        flight.factor=std::max(s.floor,flight.factor*s.falloff);
    }
}

DEFINE_HOOK(0x468B72,ActSp_PiercingLaunch,5) {
    auto b=R->EBX<BulletClass*>();
    if(ProjectileScript::launch(b)) return 0;
    const auto& spec=rules(b->Type);
    if(!spec.enabled || spec.life<=0) return 0;
    const auto origin=b->Location;
    auto aim=b->Target?b->Target->GetCoords():b->TargetCoords;
    BulletVelocity dir{double(aim.X)-origin.X,double(aim.Y)-origin.Y,double(aim.Z)-origin.Z};
    double len=std::hypot(dir.X,dir.Y,dir.Z);
    if(!len) {dir=b->Velocity;len=std::hypot(dir.X,dir.Y,dir.Z);}
    const auto speed=b->WeaponType?b->WeaponType->Speed:b->Speed;
    double k=len?std::max(0,speed)/len:0;
    auto& f=flights[b->UniqueID];
    f={origin,{dir.X*k,dir.Y*k,dir.Z*k},0,spec.life};
    // The native pointer-expiry path can remove a round when its designated
    // object is destroyed. A cell keeps the initial aim valid for the engine.
    if(auto cell=MapClass::Instance.TryGetCellAt(CellClass::Coord2Cell(aim))) {
        b->SetTarget(cell);
        b->TargetCoords=aim;
    }
    b->Velocity=f.step;
    return 0;
}

// After native trailer/animation work, before native steering and target fuse.
DEFINE_HOOK(0x4668C3,ActSp_PiercingMove,8) {
    auto b=R->EBP<BulletClass*>();
    if(auto next=ProjectileScript::move(b)) return next;
    auto it=flights.find(b->UniqueID);
    if(it==flights.end()) return 0;
    auto& f=it->second;
    ++f.age;
    auto advance=[&](int origin,double velocity) {
        return int(std::round(std::clamp(origin+velocity*f.age,double(INT_MIN),double(INT_MAX))));
    };
    CoordStruct next{advance(f.origin.X,f.step.X),advance(f.origin.Y,f.step.Y),advance(f.origin.Z,f.step.Z)};
    if(EntityWalls::piercingStep(b,next,EntityWalls::scriptedImpact(b,next))) return 0x467FAF;
    if(next.X<0 || next.Y<0 || next.X>=512*256 || next.Y>=512*256 ||
       !MapClass::Instance.IsWithinUsableArea(CellClass::Coord2Cell(next),false)) return 0x467FAF;
    if(f.age>=f.life) {
        detonateEnd(b,next,1,0);
        return 0x467FAF;
    }
    b->Mark(MarkType::Up); b->SetLocation(next); b->Mark(MarkType::Down);
    b->LastMapCoords=CellClass::Coord2Cell(next);
    return 0x467FEE;
}

namespace {
constexpr wchar_t streamName[]=L"ActSp.Piercing";
template<typename T> bool write(IStream* s,const T& v) {ULONG n=0;return s->Write(&v,sizeof(v),&n)==S_OK && n==sizeof(v);}
template<typename T> bool read(IStream* s,T& v) {ULONG n=0;return s->Read(&v,sizeof(v),&n)==S_OK && n==sizeof(v);}
}
bool save(IStorage* storage) {
    IStream* s=nullptr;
    if(FAILED(storage->CreateStream(streamName,STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,0,&s))) return false;
    bool ok=write(s,DWORD{1}) && write(s,DWORD(flights.size()));
    for(const auto& [id,f]:flights) {
        ok=ok && write(s,id) && write(s,f.origin) && write(s,f.step) && write(s,f.age) && write(s,f.life) && write(s,f.factor) && write(s,DWORD(f.hit.size()));
        for(auto target:f.hit) ok=ok && write(s,target);
    }
    ok=ok && SUCCEEDED(s->Commit(STGC_DEFAULT));s->Release();return ok;
}
bool load(IStorage* storage) {
    reset();
    IStream* s=nullptr;
    auto hr=storage->OpenStream(streamName,nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&s);
    if(hr==STG_E_FILENOTFOUND) return true;
    if(FAILED(hr)) return false;
    DWORD version=0,count=0;
    bool ok=read(s,version) && version==1 && read(s,count);
    for(DWORD i=0;ok && i<count;++i) {
        DWORD id=0,n=0;
        Flight f;
        ok=read(s,id) && read(s,f.origin) && read(s,f.step) && read(s,f.age) && read(s,f.life) && read(s,f.factor) && read(s,n) &&
            f.age>=0 && f.age<f.life && f.factor>=0 && f.factor<=1 && std::isfinite(f.step.X) && std::isfinite(f.step.Y) && std::isfinite(f.step.Z);
        for(DWORD j=0;ok && j<n;++j) {DWORD target;ok=read(s,target) && f.hit.insert(target).second;}
        ok=ok && flights.emplace(id,std::move(f)).second;
    }
    s->Release();if(!ok) reset();return ok;
}
}
