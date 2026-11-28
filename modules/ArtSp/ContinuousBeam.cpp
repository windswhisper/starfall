#include "ContinuousBeam.h"
#include "../../src/ConfigText.h"
#include "../../src/ProjectileShield.h"
#include "../ActSp/FireOrigin.h"
#include <FootClass.h>
#include <HouseClass.h>
#include <CCINIClass.h>
#include <MapClass.h>
#include <RulesClass.h>
#include <WeaponTypeClass.h>
#include <WarheadTypeClass.h>
#include <Surface.h>
#include <TacticalClass.h>
#include <FileSystem.h>
#include <ConvertClass.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace ArtSp::ContinuousBeam {
namespace {
enum class Kind { Laser,Bolt,Radiation };
struct Appearance {
    Kind kind;
    std::array<ColorStruct,3> colors{};
    std::array<bool,3> disabled{};
    bool houseColor=false;
    int width=1, period=15;
    double amplitude=40;
};
struct Ray {
    TechnoClass* target;
    unsigned started, seed;
    CoordStruct from,to;
};
struct Lock {
    WeaponTypeClass* weapon;
    int slot;
    Ray primary;
    std::vector<Ray> extras;
};
std::unordered_map<WeaponTypeClass*,Appearance> styles;
std::unordered_map<TechnoClass*,Lock> locks;
// Successful launches are known before the engine resolves the main visual target.
std::unordered_map<unsigned,std::vector<TechnoClass*>> pendingExtras;

ColorStruct boltColor(int index) {
    auto palette=FileSystem::PALETTE_PAL;
    if(palette && palette->PaletteData && palette->BytesPerPixel==2)
        return Drawing::Int_To_RGB(static_cast<const WORD*>(static_cast<const void*>(palette->PaletteData))[index]);
    return index==15?ColorStruct{255,255,255}:index==5?ColorStruct{255,96,72}:ColorStruct{96,128,255};
}

bool onMap(TechnoClass* unit) {
    return unit->IsAlive && unit->Health>0 && unit->IsOnMap && !unit->InLimbo && !unit->IsSinking;
}
bool detects(TechnoClass* target,HouseClass* viewer) {
    if(target->CloakState==CloakState::Uncloaked) return true;
    if(!viewer) return false;
    if(target->Owner==viewer || (target->Owner && viewer->IsAlliedWith(target->Owner)) || viewer->IsObserver()) return true;
    auto cell=target->GetCell();
    return cell && viewer->ArrayIndex>=0 && viewer->ArrayIndex<24 && cell->Sensors_InclHouse(viewer->ArrayIndex);
}
bool visibleAt(const CoordStruct& pos) {
    auto cell=MapClass::Instance.TryGetCellAt(pos);
    return cell && !cell->IsShrouded() && !cell->IsFogged();
}
double noise(unsigned key) {
    key^=key>>16; key*=0x7feb352du; key^=key>>15; key*=0x846ca68bu; key^=key>>16;
    return (key&1023)/511.5-1.0;
}
void paint(Surface* surface,RectangleStruct clip,const CoordStruct& from,const Ray& ray,const Appearance& style,
           ColorStruct main,unsigned frame) {
    auto tactical=TacticalClass::Instance;
    const auto a=tactical->CoordsToClient(from).first, b=tactical->CoordsToClient(ray.to).first;
    // The world pass uses tactical-surface coordinates. Reject offscreen locks before sampling.
    constexpr int margin=32;
    int x0=std::max(clip.X,std::min(a.X,b.X)-margin),x1=std::min(clip.X+clip.Width-1,std::max(a.X,b.X)+margin);
    int y0=std::max(clip.Y,std::min(a.Y,b.Y)-margin),y1=std::min(clip.Y+clip.Height-1,std::max(a.Y,b.Y)+margin);
    if(x0>x1 || y0>y1) return;
    const auto delta=ray.to-from;
    const auto dx=b.X-a.X,dy=b.Y-a.Y;
    const double length=std::hypot(dx,dy);
    if(length<1) return;
    double nx=-dy/length,ny=dx/length;
    const unsigned age=frame-ray.started;
    const int steps=std::clamp(static_cast<int>(std::ceil(std::max({length/7.0,std::abs(delta.X)/96.0,std::abs(delta.Y)/96.0}))),1,2048);
    const int strands=style.kind==Kind::Bolt?3:1;
    const auto edgeColor=Drawing::RGB_To_Int(style.colors[1]);
    bool painted=false;
    std::array<Point2D,3> previous{a,a,a};
    bool lastVisible=visibleAt(from);
    for(int i=1;i<=steps;++i) {
        const double t=double(i)/steps;
        const bool visible=visibleAt(from+delta*t);
        const double envelope=(i==steps || style.kind==Kind::Laser)?0.:std::sin(3.14159265*t);
        for(int s=0;s<strands;++s) {
            if(style.disabled[s]) continue;
            double offset=0.;
            if(style.kind==Kind::Bolt)
                offset=noise(ray.seed+age/2*977u+i*37u+s*137u)*std::min(9.,length/12)*envelope;
            else if(style.kind==Kind::Radiation)
                offset=std::sin(6.2831853*(length*t/40.-double(age%style.period)/style.period))*style.amplitude*0.13*envelope;
            Point2D next{a.X+static_cast<int>(std::lround(dx*t+nx*offset)),a.Y+static_cast<int>(std::lround(dy*t+ny*offset))};
            if(lastVisible && visible) {
                const auto ink=Drawing::RGB_To_Int(style.kind==Kind::Bolt?style.colors[s]:main);
                for(int k=0;k<style.width;++k) {
                    const int d=k-style.width/2;
                    Point2D shift=std::abs(dx)>=std::abs(dy)?Point2D{0,d}:Point2D{d,0};
                    auto start=previous[s]+shift,end=next+shift;
                    const bool outer=style.kind==Kind::Laser && d && !style.houseColor && edgeColor;
                    surface->DrawLineEx(&clip,&start,&end,outer?edgeColor:ink);
                }
                painted=true;
            }
            previous[s]=next;
        }
        lastVisible=visible;
    }
    if(painted) tactical->RegisterDirtyArea({x0,y0,x1-x0+1,y1-y0+1},false);
}
}

void reset() { locks.clear(); styles.clear(); pendingExtras.clear(); }
void readRules() {
    reset();
    auto ini=CCINIClass::INI_Rules;
    if(!ini) return;
    for(auto weapon:WeaponTypeClass::Array) {
        const auto id=weapon->ID;
        if(!ini->ReadBool(id,"ContinuousBeam",false)) continue;
        Appearance s{};
        if(weapon->IsLaser) {
            s.kind=Kind::Laser;
            s.colors={weapon->LaserInnerColor,weapon->LaserOuterColor,weapon->LaserOuterSpread};
            s.width=std::clamp(ini->ReadInteger(id,"LaserThickness",weapon->IsBigLaser?3:1),1,16);
            s.houseColor=weapon->IsHouseColor;
        } else if(weapon->IsElectricBolt) {
            s.kind=Kind::Bolt;
            for(int i=0;i<3;++i) {
                const auto n=std::to_string(i+1);
                s.colors[i]=ini->ReadColor(id,("Bolt.Color"+n).c_str(),boltColor(i==2?15:weapon->IsAlternateColor?5:10));
                s.disabled[i]=ini->ReadBool(id,("Bolt.Disable"+n).c_str(),false);
            }
        } else if(weapon->IsRadBeam) {
            s.kind=Kind::Radiation;
            auto rules=RulesClass::Instance;
            const bool temporal=weapon->Warhead && weapon->Warhead->Temporal;
            s.colors[0]=ini->ReadColor(id,"Beam.Color",temporal?rules->ChronoBeamColor:rules->RadColor);
            s.houseColor=ini->ReadBool(id,"Beam.IsHouseColor",false);
            s.period=std::max(1,ini->ReadInteger(id,"Beam.Duration",15));
            auto amplitude=ini->ReadDouble(id,"Beam.Amplitude",40.);
            s.amplitude=std::isfinite(amplitude)?std::clamp(amplitude,-160.,160.):40.;
        } else {
            ConfigText::configLog(std::string("ArtSp: [")+id+"] ContinuousBeam requires IsLaser, IsElectricBolt or IsRadBeam");
            continue;
        }
        styles.emplace(weapon,s);
    }
}
void stop(TechnoClass* firer) { locks.erase(firer); }
void forget(TechnoClass* unit) {
    std::erase_if(locks,[unit](const auto& item){return item.first==unit || item.second.primary.target==unit;});
    for(auto& [owner,lock]:locks) std::erase_if(lock.extras,[unit](const Ray& ray){return ray.target==unit;});
    for(auto& [shot,extras]:pendingExtras) std::erase(extras,unit);
}
bool collectExtra(unsigned shot,TechnoClass* firer,AbstractClass* primary,WeaponTypeClass* weapon,TechnoClass* target) {
    if(!styles.contains(weapon) || firer->Target!=primary || (primary->AbstractFlags&AbstractFlags::Techno)==AbstractFlags::None) return false;
    pendingExtras[shot].push_back(target);
    return true;
}
std::vector<TechnoClass*> takeExtras(unsigned shot) {
    auto volley=pendingExtras.extract(shot);
    return volley.empty()?std::vector<TechnoClass*>{}:std::move(volley.mapped());
}
static void updateRay(Ray& ray,TechnoClass* firer,WeaponTypeClass* weapon,const CoordStruct& muzzle) {
    ray.from=FireOrigin::resolve(weapon,ray.target,muzzle);
    ray.to=ProjectileShield::beamEnd(firer,weapon,ray.from,ray.target->GetTargetCoords());
}

bool onShot(TechnoClass* firer,AbstractClass* aim,WeaponTypeClass* weapon,int slot,const CoordStruct& muzzle,
            const std::vector<TechnoClass*>& extras) {
    if(styles.empty()) return false;
    if(!styles.contains(weapon) || !aim || aim!=firer->Target || (aim->AbstractFlags&AbstractFlags::Techno)==AbstractFlags::None ||
       slot<0 || slot>=TechnoTypeClass::MaxWeapons) {
        stop(firer);
        return false;
    }
    auto target=static_cast<TechnoClass*>(aim);
    if(!onMap(firer) || !onMap(target) || target==firer) {stop(firer);return false;}
    auto& lock=locks[firer];
    const auto makeRay=[&](TechnoClass* end) {
        return Ray{end,static_cast<unsigned>(Unsorted::CurrentFrame),firer->UniqueID^end->UniqueID,{},{}};
    };
    if(lock.primary.target!=target || lock.weapon!=weapon || lock.slot!=slot)
        lock={weapon,slot,makeRay(target),{}};
    updateRay(lock.primary,firer,weapon,muzzle);
    // Preserve each surviving target's animation phase across consecutive volleys.
    std::unordered_map<TechnoClass*,unsigned> previous;
    for(const auto& ray:lock.extras) previous.emplace(ray.target,ray.started);
    lock.extras.clear();
    for(auto extra:extras) {
        auto ray=makeRay(extra);
        updateRay(ray,firer,weapon,muzzle);
        if(auto it=previous.find(extra);it!=previous.end()) ray.started=it->second;
        lock.extras.push_back(ray);
    }
    return true;
}
void tick() {
    pendingExtras.clear();
    for(auto it=locks.begin();it!=locks.end();) {
        auto firer=it->first;
        auto& lock=it->second;
        auto target=lock.primary.target;
        if(!onMap(firer) || !onMap(target) || firer->Target!=target || firer->Deactivated ||
           !detects(target,firer->Owner)) {it=locks.erase(it);continue;}
        auto mount=firer->GetWeapon(lock.slot);
        if(!mount || mount->WeaponType!=lock.weapon || !firer->IsCloseEnough(target,lock.slot)) {it=locks.erase(it);continue;}
        const auto muzzle=firer->GetFLH(lock.slot,CoordStruct::Empty);
        updateRay(lock.primary,firer,lock.weapon,muzzle);
        std::erase_if(lock.extras,[&](Ray& ray){
            auto extra=ray.target;
            if(!onMap(extra) || !extra->Owner || !firer->Owner || firer->Owner==extra->Owner ||
               firer->Owner->IsAlliedWith(extra->Owner) || extra->Owner->IsNeutral() ||
               (extra->Disguised && !extra->IsClearlyVisibleTo(firer->Owner)) ||
               !detects(extra,firer->Owner) || !firer->IsCloseEnough(extra,lock.slot)) return true;
            updateRay(ray,firer,lock.weapon,muzzle);
            return false;
        });
        ++it;
    }
}
void draw(Surface* surface,RectangleStruct view,unsigned frame) {
    if(locks.empty() || !surface || !TacticalClass::Instance) return;
    view.Width=std::min(view.Width,surface->GetWidth());
    view.Height=std::min(view.Height,surface->GetHeight());
    for(auto& [firer,lock]:locks) {
        if(firer->Target!=lock.primary.target || !detects(firer,HouseClass::CurrentPlayer)) continue;
        const auto& style=styles.at(lock.weapon);
        auto color=style.houseColor && firer->Owner?firer->Owner->LaserColor:style.colors[0];
        auto drawRay=[&](const Ray& ray) {
            if(detects(ray.target,HouseClass::CurrentPlayer)) paint(surface,view,ray.from,ray,style,color,frame);
        };
        drawRay(lock.primary);
        for(const auto& ray:lock.extras) drawRay(ray);
    }
}
}
