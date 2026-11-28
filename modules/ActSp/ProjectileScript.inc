// Projectile callbacks use the shared skill module catalogue and serializable Lua values.
namespace ProjectileScript {
namespace {
using Vec=BulletVelocity;
struct Step {
    CoordStruct next{}, sweepEnd{};
    EntityWalls::ScriptImpact impact{};
    bool piercing=false,sweep=false,reached=false,end=false,remove=false;
};
struct Flight {
    Step plan{};
    int age=0,life=90,nextTick=0,weaponSpeed=0;
    double launchSpeed=0;
    Vec velocity{}, remainder{};
    CoordStruct aim{};
    TargetClass target;
    LuaTaskData::Value state{LuaTaskData::Type::Table};
};
struct Rules { string script; int life=90; };
std::map<BulletTypeClass*,Rules> types;
std::map<DWORD,Flight> flights;
std::map<string,int> mapSpeeds;
std::map<WeaponTypeClass*,int> weaponSpeeds;
int rawWeaponSpeed(BulletClass* b) {
    auto w=b->WeaponType;
    if(!w) return b->Speed;
    auto [it,added]=weaponSpeeds.try_emplace(w);
    if(added) {
        auto override=mapSpeeds.find(lower(w->ID));
        it->second=override!=mapSpeeds.end()?override->second:
            CCINIClass::INI_Rules->ReadInteger(w->ID,"Speed",100);
    }
    return it->second;
}
struct Invocation {
    BulletClass* bullet;
    Flight& flight;
    bool detonate=false;
    std::optional<CoordStruct> destination,travelEnd;
    unsigned operations=0;
    CoordStruct position() const {return travelEnd.value_or(bullet->Location);}
};
double length(Vec v) { return std::hypot(v.X,v.Y,v.Z); }
Invocation& current(lua_State* L) {return *static_cast<Invocation*>(ScriptRuntime::call(L).context);}
Invocation& motion(lua_State* L) {
    auto& ctx=current(L);
    if(ctx.travelEnd) luaL_error(L,"control motion in onLaunch or onTick");
    return ctx;
}
double numberAt(lua_State* L,int i) {
    double n=luaL_checknumber(L,i);
    if(!std::isfinite(n) || std::abs(n)>4096) luaL_error(L,"coordinate or velocity outside supported world");
    return n;
}
void vectorTable(lua_State* L,double x,double y,double z) {
    lua_createtable(L,0,3);
    lua_pushnumber(L,x);lua_setfield(L,-2,"x");
    lua_pushnumber(L,y);lua_setfield(L,-2,"y");
    lua_pushnumber(L,z);lua_setfield(L,-2,"z");
}
void point(lua_State* L,CoordStruct c) {vectorTable(L,c.X/256.-.5,c.Y/256.-.5,c.Z/256.);}
TechnoClass* tracked(TargetClass ref) {
    auto t=ref.As_Techno();
    return alive(t) && t->IsOnMap && !t->InLimbo?t:nullptr;
}
int unitInfo(lua_State* L) {
    auto id=ScriptActions::handle(L,1);
    TargetClass ref;ref.m_ID=id;ref.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
    auto t=tracked(ref);
    switch(lua_tointeger(L,lua_upvalueindex(1))) {
    case 0: lua_pushinteger(L,id);break;
    case 1: lua_pushboolean(L,t!=nullptr);break;
    case 2: if(t) point(L,t->GetCoords());else lua_pushnil(L);break;
    default: if(t) lua_pushstring(L,t->GetTechnoType()->ID);else lua_pushnil(L);
    }
    return 1;
}
#include "ProjectileScriptActions.inc"
int steer(lua_State* L) {
    auto& ctx=motion(L);
    luaL_checktype(L,2,LUA_TTABLE);
    auto axis=[&](const char* key) {lua_getfield(L,2,key);double v=numberAt(L,-1);lua_pop(L,1);return v;};
    const auto pos=ctx.bullet->Location;
    Vec dir{(axis("x")+.5)*256-pos.X,(axis("y")+.5)*256-pos.Y,axis("z")*256-pos.Z};
    const double speed=numberAt(L,3)*256, degrees=numberAt(L,4);
    if(speed<0 || degrees<0 || degrees>180) return luaL_error(L,"steerTo needs nonnegative speed and 0..180 degrees");
    auto& v=ctx.flight.velocity;
    const double dest=length(dir),old=length(v);
    if(!dest) {v={};return 0;}
    dir={dir.X/dest,dir.Y/dest,dir.Z/dest};
    if(old>0) {
        Vec forward{v.X/old,v.Y/old,v.Z/old};
        const double dot=std::clamp(dir.X*forward.X+dir.Y*forward.Y+dir.Z*forward.Z,-1.,1.);
        const double turn=degrees*3.141592653589793/180.;
        if(std::acos(dot)>turn) {
            Vec side{dir.X-dot*forward.X,dir.Y-dot*forward.Y,dir.Z-dot*forward.Z};
            if(length(side)<1e-10) side=std::abs(forward.Z)<.9?Vec{-forward.Y,forward.X,0}:Vec{0,-forward.Z,forward.Y};
            const double k=std::sin(turn)/length(side),c=std::cos(turn);
            dir={forward.X*c+side.X*k,forward.Y*c+side.Y*k,forward.Z*c+side.Z*k};
        }
    }
    v={dir.X*speed,dir.Y*speed,dir.Z*speed};return 0;
}
int invoke(lua_State* L) {
    auto& ctx=current(L);auto& f=ctx.flight;
    if(luaL_newmetatable(L,"Starfall.Unit")) {
        lua_pushcfunction(L,ScriptActions::sameUnit);lua_setfield(L,-2,"__eq");
        lua_newtable(L);int index=0;
        for(auto name:{"id","alive","position","type"}) {
            lua_pushinteger(L,index++);lua_pushcclosure(L,unitInfo,1);lua_setfield(L,-2,name);
        }
        lua_setfield(L,-2,"__index");
    }
    lua_pop(L,1);
    lua_newtable(L);
    static const luaL_Reg methods[]={
        {"action",action},{"nearbyUnits",nearbyUnits},{"groundPoint",groundPoint},
        {"firer",[](lua_State* L){auto u=current(L).bullet->Owner;ScriptActions::pushUnit(L,alive(u)?u->UniqueID:0);return 1;}},
        {"speed",[](lua_State* L){lua_pushnumber(L,length(current(L).flight.velocity)/256);return 1;}},
        {"weaponSpeed",[](lua_State* L){lua_pushinteger(L,current(L).flight.weaponSpeed);return 1;}},
        {"launchSpeed",[](lua_State* L){lua_pushnumber(L,current(L).flight.launchSpeed);return 1;}},
        {"velocity",[](lua_State* L){auto v=current(L).flight.velocity;vectorTable(L,v.X/256,v.Y/256,v.Z/256);return 1;}},
        {"position",[](lua_State* L){point(L,current(L).position());return 1;}},
        {"heightAboveGround",[](lua_State* L){
            auto& ctx=current(L);
            auto at=ctx.position();
            lua_pushnumber(L,(ctx.travelEnd?at.Z-surface(at).Z:ctx.bullet->GetHeight())/256.);
            return 1;}},
        {"targetPoint",[](lua_State* L){point(L,current(L).flight.aim);return 1;}},
        {"target",[](lua_State* L){auto t=tracked(current(L).flight.target);ScriptActions::pushUnit(L,t?t->UniqueID:0);return 1;}},
        {"setVelocity",[](lua_State* L){motion(L).flight.velocity={numberAt(L,2)*256,numberAt(L,3)*256,numberAt(L,4)*256};return 0;}},
        {"moveTo",[](lua_State* L){auto& c=motion(L);
            c.destination=CoordStruct{int(std::round((numberAt(L,2)+.5)*256)),int(std::round((numberAt(L,3)+.5)*256)),int(std::round(numberAt(L,4)*256))};
            c.flight.remainder={};return 0;}},
        {"steerTo",steer},
        {"detonate",[](lua_State* L){motion(L).detonate=true;return 0;}},
        {nullptr,nullptr}};
    luaL_setfuncs(L,methods,0);
    lua_pushinteger(L,f.age);lua_setfield(L,-2,"age");
    lua_pushinteger(L,f.life);lua_setfield(L,-2,"life");
    LuaTaskData::push(L,f.state);lua_setfield(L,-2,"state");
    lua_insert(L,-2); // keep context below the function and its argument
    lua_pushvalue(L,-2);
    if(ctx.travelEnd) {
        point(L,ctx.bullet->Location);point(L,*ctx.travelEnd);lua_call(L,3,1);
    } else lua_call(L,1,1);
    if(!lua_isnil(L,-1)) {
        auto delay=luaL_checkinteger(L,-1);
        if(ctx.travelEnd || delay<1 || delay>INT_MAX-f.age) return luaL_error(L,"invalid callback delay");
        f.nextTick=f.age+static_cast<int>(delay);
    }
    lua_pop(L,1);lua_getfield(L,-1,"state");luaL_checktype(L,-1,LUA_TTABLE);
    f.state=LuaTaskData::copy(L,-1);
    return 0;
}
bool callback(Invocation& ctx,ScriptRuntime::Module& m,const string& fn) {
    if(!includes(m.functions,fn)) return true;
    ScriptRuntime::Call call{&m,nullptr,&fn,invoke,&ctx};
    string error;
    if(ScriptRuntime::run(call,error)) return true;
    configLog("ProjectileScript "+m.file+"."+fn+": "+error);
    return false;
}
const Rules& rules(BulletTypeClass* type) {
    auto [it,fresh]=types.try_emplace(type);
    auto ini=CCINIClass::INI_Rules;
    if(!fresh || !ini) return it->second;
    char name[200]{},trajectory[80]{};
    ini->ReadString(type->ID,"ProjectileScript","",name);
    if(!*name) return it->second;
    ini->ReadString(type->ID,"Trajectory","",trajectory);
    auto& s=it->second;s.life=ini->ReadInteger(type->ID,"ProjectileScript.Life",90);
    auto key=lower(trim(name));if(!key.ends_with(".lua")) key+=".lua";
    Research::ensureReady();
    auto itScript=scriptModules.find(key);
    if(type->Inviso || (*trajectory && _stricmp(trajectory,"none")) ||
       s.life<1 || s.life>1000000 || itScript==scriptModules.end() ||
       (!includes(itScript->second.functions,"onLaunch") && !includes(itScript->second.functions,"onTick") &&
        !includes(itScript->second.functions,"onTravel")))
        configLog(string("[")+type->ID+"] invalid ProjectileScript, lifetime or competing trajectory");
    else s.script=key;
    return s;
}
}
void reset() {types.clear();flights.clear();mapSpeeds.clear();weaponSpeeds.clear();}
void forget(DWORD id) {flights.erase(id);}
bool launch(BulletClass* b) {
    const auto& spec=rules(b->Type);
    auto w=b->WeaponType;
    if(spec.script.empty() || (w && (w->IsLaser || w->DiskLaser || w->IsElectricBolt || w->IsRadBeam || w->IsRailgun || w->IsSonic || w->LimboLaunch))) return false;
    if(b->Owner && b->Owner->Owner) passiveBulletOwners.try_emplace(b->UniqueID,b->Owner->Owner->ArrayIndex);
    Flight f;f.life=spec.life;f.velocity=b->Velocity;
    f.target=TargetClass(b->Target);
    f.aim=b->Target?b->Target->GetCoords():b->TargetCoords;
    if(auto t=tracked(f.target)) f.aim=t->GetTargetCoords();
    if(auto cell=MapClass::Instance.TryGetCellAt(CellClass::Coord2Cell(f.aim))) b->SetTarget(cell);
    b->TargetCoords=f.aim;
    flights.insert_or_assign(b->UniqueID,std::move(f));
    Piercing::startScripted(b,spec.life);
    return true;
}
// Clip a conventional shot at the first contact with its 32-lepton target fuse.
bool clipAtAim(CoordStruct pos,CoordStruct aim,CoordStruct& next) {
    Vec delta{double(next.X-pos.X),double(next.Y-pos.Y),double(next.Z-pos.Z)};
    Vec rel{double(pos.X-aim.X),double(pos.Y-aim.Y),double(pos.Z-aim.Z)};
    double a=delta.X*delta.X+delta.Y*delta.Y+delta.Z*delta.Z;
    double dot=delta.X*rel.X+delta.Y*rel.Y+delta.Z*rel.Z;
    double c=rel.X*rel.X+rel.Y*rel.Y+rel.Z*rel.Z-32.*32.;
    double disc=dot*dot-a*c;
    double hit=c<=0?0:a && dot<0 && disc>=0?c/(-dot+std::sqrt(disc)):2;
    if(hit>1) return false;
    next={pos.X+int(std::round(delta.X*hit)),pos.Y+int(std::round(delta.Y*hit)),pos.Z+int(std::round(delta.Z*hit))};
    return true;
}
void plan(BulletClass* b) {
    auto it=flights.find(b->UniqueID);
    if(it==flights.end()) return;
    auto& f=it->second;
    const auto& spec=rules(b->Type);
    auto& plan=f.plan;plan={};plan.remove=true;
    if(spec.script.empty()) return;
    auto& m=scriptModules.at(spec.script);
    auto target=tracked(f.target);
    if(target) f.aim=target->GetTargetCoords();
    auto pos=b->Location;
    Invocation ctx{b,f};
    if(!f.age) {
        f.weaponSpeed=rawWeaponSpeed(b);
        f.launchSpeed=length(f.velocity)/256.;
        if(!callback(ctx,m,"onLaunch")) return;
    }
    ++f.age;
    if(!ctx.detonate && f.age>=f.nextTick && !callback(ctx,m,"onTick")) return;
    Vec step=f.velocity;
    auto advance=[](int start,double speed,double& rem) {
        double distance=speed+rem;int whole=int(std::round(distance));rem=distance-whole;return start+whole;
    };
    CoordStruct next=pos;
    if(!ctx.detonate && ctx.destination) next=*ctx.destination;
    else if(!ctx.detonate) next={advance(pos.X,step.X,f.remainder.X),advance(pos.Y,step.Y,f.remainder.Y),advance(pos.Z,step.Z,f.remainder.Z)};
    plan.piercing=Piercing::active(b->UniqueID);
    bool reached=!plan.piercing && !ctx.detonate && clipAtAim(pos,f.aim,next);
    plan.sweep=plan.piercing && !ctx.detonate;
    plan.sweepEnd=next;
    if(!ctx.detonate) plan.impact=EntityWalls::scriptedImpact(b,next);
    if(plan.impact.id) next=plan.impact.point;
    if(next.X<0 || next.Y<0 || next.X>=512*256 || next.Y>=512*256 || std::abs(double(next.Z))>4096*256 ||
       !MapClass::Instance.IsWithinUsableArea(CellClass::Coord2Cell(next),false)) {b->Velocity={};return;}
    plan.next=next;plan.reached=reached;plan.end=ctx.detonate || reached || f.age>=f.life;plan.remove=false;
    b->Velocity={double(next.X-pos.X),double(next.Y-pos.Y),double(next.Z-pos.Z)};
}
// Supply the clipped displacement before the native and Phobos trail updates.
DEFINE_HOOK(0x4666E6,SF_PrepareProjectileScript,6) {
    plan(R->ECX<BulletClass*>());
    return 0;
}
DWORD move(BulletClass* b) {
    auto it=flights.find(b->UniqueID);
    if(it==flights.end()) return 0;
    const auto plan=it->second.plan;
    if(!plan.remove && b->Location!=plan.next) {
        auto& module=scriptModules.at(rules(b->Type).script);
        Invocation ctx{b,it->second};
        ctx.travelEnd=plan.next;
        if(!callback(ctx,module,"onTravel")) return 0x467FAF;
    }
    if(plan.sweep && EntityWalls::piercingStep(b,plan.sweepEnd,plan.impact)) return 0x467FAF;
    if(plan.remove) return 0x467FAF;
    if(plan.impact.id) {
        // A travel effect may already have destroyed the blocker. Still stop at contact.
        EntityWalls::applyScriptedImpact(b,plan.impact);
        return 0x467FAF;
    }
    auto next=plan.next;
    b->Mark(MarkType::Up);b->SetLocation(next);b->Mark(MarkType::Down);
    b->LastMapCoords=CellClass::Coord2Cell(next);
    if(plan.end) {
        if(plan.piercing) {
            Piercing::detonateEnd(b,next,1,0);
            return 0x467FAF;
        }
        auto target=tracked(it->second.target);
        b->SetTarget(plan.reached && target?static_cast<AbstractClass*>(target):MapClass::Instance.GetCellAt(b->LastMapCoords));
        b->TargetCoords=next;b->Detonate(next);return 0x467FAF;
    }
    return 0x467FEE;
}
}

