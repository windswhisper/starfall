// Included alongside the native action executor so both entry points share its rules.
namespace ScriptActions {
struct Frame {
    const Skill& skill;
    Event& event;
    const Objects& objects;
    int stacks;
    int* damage;
    bool changed=false;
    unsigned operations=0;
    const LuaTaskData::Value* taskData=nullptr;
    uint32_t taskId=0;
};
Frame& frame(lua_State* L) { return *static_cast<Frame*>(ScriptRuntime::call(L).context); }
DWORD handle(lua_State* L,int index) {
    return *static_cast<DWORD*>(luaL_checkudata(L,index,"Starfall.Unit"));
}
void pushUnit(lua_State* L,DWORD id) {
    if(!id) {lua_pushnil(L);return;}
    *static_cast<DWORD*>(lua_newuserdatauv(L,sizeof(DWORD),0))=id;
    luaL_setmetatable(L,"Starfall.Unit");
}
TechnoClass* live(lua_State* L,DWORD id) {return findLive(frame(L).objects,id);}
void pushPoint(lua_State* L,CellStruct cell) {
    lua_createtable(L,0,2);
    lua_pushinteger(L,cell.X);lua_setfield(L,-2,"x");
    lua_pushinteger(L,cell.Y);lua_setfield(L,-2,"y");
}
CellStruct pointAt(lua_State* L,int index) {
    luaL_checktype(L,index,LUA_TTABLE);
    index=lua_absindex(L,index);
    auto coordinate=[&](const char* key) {
        lua_getfield(L,index,key);auto n=luaL_checkinteger(L,-1);lua_pop(L,1);
        if(n<0 || n>=512) luaL_error(L,"invalid map cell coordinate");
        return static_cast<short>(n);
    };
    CellStruct p{coordinate("x"),coordinate("y")};
    if(!MapClass::Instance.TryGetCellAt(p) || !MapClass::Instance.IsWithinUsableArea(p,false))
        luaL_error(L,"point outside playable map");
    return p;
}
int unitProperty(lua_State* L) {
    DWORD id=handle(L,1);
    auto unit=live(L,id);
    string prop=lua_tostring(L,lua_upvalueindex(1));
    if(prop=="id") lua_pushinteger(L,id);
    else if(prop=="alive") lua_pushboolean(L,unit!=nullptr);
    else if(!unit) lua_pushnil(L);
    else if(prop=="position") {
        if(unit->IsOnMap && !unit->InLimbo) pushPoint(L,unit->GetMapCoords());else lua_pushnil(L);
    }
    else if(prop=="health") lua_pushinteger(L,UnitHealth::read(unit).current);
    else if(prop=="maxHealth") lua_pushinteger(L,UnitHealth::read(unit).maximum);
    else if(prop=="ammo") lua_pushinteger(L,unit->GetTechnoType()->Ammo<0?-1:unit->Ammo);
    else if(prop=="passengers") lua_pushinteger(L,unit->Passengers.NumPassengers);
    else if(prop=="rank") lua_pushinteger(L,unit->Veterancy.IsElite()?2:unit->Veterancy.IsVeteran()?1:0);
    else if(prop=="type") lua_pushstring(L,unit->GetTechnoType()->ID);
    else if(prop=="kind") lua_pushstring(L,kind(unit));
    else if(prop=="cost") lua_pushinteger(L,unit->GetTechnoType()->Cost);
    else if(prop=="owner") lua_pushinteger(L,unit->Owner?unit->Owner->ArrayIndex:-1);
    else if(prop=="trainable") lua_pushboolean(L,unit->GetTechnoType()->Trainable);
    else {
        auto passive=lower(luaL_checkstring(L,2));
        if(prop=="hasPassive") lua_pushboolean(L,ownsPassive(unit,passive));
        else lua_pushinteger(L,ownsPassive(unit,passive)?stackCount(id,passive):0);
    }
    return 1;
}
int sameUnit(lua_State* L) {
    lua_pushboolean(L,handle(L,1)==handle(L,2));
    return 1;
}
void units(lua_State* L) {
    luaL_newmetatable(L,"Starfall.Unit");
    lua_pushcfunction(L,sameUnit);lua_setfield(L,-2,"__eq");
    lua_newtable(L);
    for(auto name:{"id","alive","position","health","maxHealth","cost","ammo","passengers","rank","type","kind","owner","trainable","hasPassive","stack"}) {
        lua_pushstring(L,name);lua_pushcclosure(L,unitProperty,1);lua_setfield(L,-2,name);
    }
    lua_setfield(L,-2,"__index");
    lua_pop(L,1);
}
vector<string> list(lua_State* L,int index) {
    if(lua_type(L,index)==LUA_TSTRING) return split(lua_tostring(L,index));
    luaL_checktype(L,index,LUA_TTABLE);
    vector<string> result;
    index=lua_absindex(L,index);
    for(size_t i=1;i<=lua_rawlen(L,index);++i) {
        lua_geti(L,index,i);
        if(lua_type(L,-1)!=LUA_TSTRING) luaL_error(L,"expected a list of strings");
        result.push_back(lower(trim(lua_tostring(L,-1))));lua_pop(L,1);
    }
    return result;
}
void charge(lua_State* L,unsigned cost) {
    auto& count=frame(L).operations;
    if(cost>4096-count) luaL_error(L,"native action/query budget exceeded");
    count+=cost;
}
// Return a fresh snapshot on each call; the numeric house index remains
// compatible with Unit:owner() and the superweapon context's ctx.owner.
int houseInfo(lua_State* L) {
    auto& f=frame(L);
    lua_Integer index;
    if(lua_isnoneornil(L,2)) {
        auto unit=live(L,f.event.holder);
        index=f.event.trigger=="onmaptrigger"?f.event.owner:unit && unit->Owner?unit->Owner->ArrayIndex:f.event.owner;
    } else index=luaL_checkinteger(L,2);
    if(index<0 || index>=HouseClass::Array.Count || !HouseClass::Array.Items[index]) {
        lua_pushnil(L);return 1;
    }
    auto house=HouseClass::Array.Items[index];
    lua_createtable(L,0,6);
    auto set=[&](const char* key,lua_Integer value){lua_pushinteger(L,value);lua_setfield(L,-2,key);};
    set("index",index);
    set("powerOutput",house->PowerOutput);
    set("powerDrain",house->PowerDrain);
    set("powerBalance",static_cast<lua_Integer>(house->PowerOutput)-house->PowerDrain);
    set("credits",house->Available_Money());
    set("researchPoints",Research::balance(static_cast<int>(index)));
    return 1;
}
int nearby(lua_State* L) {
    bool point=lua_istable(L,2);
    TechnoClass* center=nullptr;
    CoordStruct pos{};
    if(point) pos=MapClass::Instance.GetCellAt(pointAt(L,2))->GetCoordsWithBridge();
    else center=live(L,handle(L,2));
    auto radius=luaL_checknumber(L,3);
    if(!std::isfinite(radius) || radius<0 || radius>128) return luaL_error(L,"nearbyUnits range must be 0..128 cells");
    vector<string> houses{"all"},types{"*"};
    bool includeSelf=false;
    if(!lua_isnoneornil(L,4)) {
        luaL_checktype(L,4,LUA_TTABLE);
        lua_getfield(L,4,"houses");if(!lua_isnil(L,-1)) houses=list(L,-1);lua_pop(L,1);
        lua_getfield(L,4,"types");if(!lua_isnil(L,-1)) types=list(L,-1);lua_pop(L,1);
        lua_getfield(L,4,"includeSelf");includeSelf=lua_toboolean(L,-1)!=0;lua_pop(L,1);
        for(const auto& h:houses) if(!includes({"all","self","allies","enemies","neutral"},h))
            return luaL_error(L,"unknown house filter: %s",h.c_str());
    }
    lua_newtable(L);
    auto& f=frame(L);
    auto owner=f.event.owner>=0 && f.event.owner<HouseClass::Array.Count?HouseClass::Array.Items[f.event.owner]:nullptr;
    if(!owner || (!point && (!center || center->InLimbo || !center->IsOnMap))) return 1;
    if(center) pos=center->GetCenterCoords();
    int n=0;
    charge(L,1+static_cast<unsigned>(frame(L).objects.size()/16));
    LuaNearby::visit(f.objects,pos,radius,[&](DWORD id,TechnoClass* unit) {
        if(!alive(unit) || !unit->Owner || unit->InLimbo || !unit->IsOnMap || (!includeSelf && center==unit)) return;
        if(distanceCells(pos,unit->GetCenterCoords())>radius) return;
        const char* house=unit->Owner==owner?"self":owner->IsAlliedWith(unit->Owner)?"allies":unit->Owner->IsNeutral()?"neutral":"enemies";
        if(!includes(houses,"all") && !includes(houses,house)) return;
        if(!Research::matchesTarget(types,lower(unit->GetTechnoType()->ID),kind(unit))) return;
        pushUnit(L,id);lua_seti(L,-2,++n);
    });
    return 1;
}
void arguments(lua_State* L,int table,ScriptFields& fields,std::optional<DWORD>& selected,std::optional<CellStruct>& point,bool fromSection) {
    if(lua_isnoneornil(L,table)) return;
    luaL_checktype(L,table,LUA_TTABLE);
    table=lua_absindex(L,table);
    std::set<string> supplied;
    lua_pushnil(L);
    while(lua_next(L,table)) {
        if(lua_type(L,-2)!=LUA_TSTRING) luaL_error(L,"expected named action fields");
        string key=lower(lua_tostring(L,-2));
        if(key!="action" && !key.starts_with("action.")) key="action."+key;
        if(key=="action.offset" || key=="action.mode" || key=="action.center" || key=="action.distance" || key=="action.height" || key=="action.tilt" || key=="action.easing" || key=="action.ignorecollision" ||
           key=="action.blowweight" || key=="action.affectsironcurtain")
            key="action.forcemove."+key.substr(7);
        const auto canonical=key=="action.duration"?string("action.forcemove.duration"):key;
        if(!supplied.insert(canonical).second)
            luaL_error(L,"duplicate action field %s",canonical.c_str());
        bool moveField=key.starts_with("action.forcemove.");
        bool moveSection=fromSection && fields["action"]=="forcemove";
        if(!actionField(key) || (fromSection && key!="action.target" && key!="action.destination" &&
            !(moveSection && (moveField || key=="action.duration"))))
            luaL_error(L,"unsupported action field %s",key.c_str());
        string value;
        if(key=="action.forcemove.offset" && lua_istable(L,-1)) {
            int tableIndex=lua_absindex(L,-1);
            for(auto axis:{"x","y"}) {
                lua_getfield(L,tableIndex,axis);
                luaL_checktype(L,-1,LUA_TNUMBER);
                if(!value.empty()) value+=",";
                value+=lua_tostring(L,-1);lua_pop(L,1);
            }
        } else if(key=="action.destination" && lua_istable(L,-1)) {
            auto dest=pointAt(L,-1);value=std::to_string(dest.X)+","+std::to_string(dest.Y);
        } else if(key=="action.target" && lua_istable(L,-1)) {
            point=pointAt(L,-1);value="victimlocation";
        } else if(key=="action.target" && luaL_testudata(L,-1,"Starfall.Unit")) {
            selected=handle(L,-1);
            value=*selected==frame(L).event.holder?"self":"target";
        } else if(lua_type(L,-1)==LUA_TBOOLEAN) value=lua_toboolean(L,-1)?"yes":"no";
        else if(lua_type(L,-1)==LUA_TTABLE) {
            for(const auto& item:list(L,-1)) {if(!value.empty()) value+=",";value+=item;}
        } else {
            if(!lua_isstring(L,-1)) luaL_error(L,"invalid value for %s",key.c_str());
            value=lua_tostring(L,-1);
        }
        fields[key]=std::move(value);lua_pop(L,1);
    }
}
bool action(lua_State* L,ScriptFields& fields,const string& label,std::optional<DWORD> target,std::optional<CellStruct> point) {
    charge(L,1);
    auto& f=frame(L);
    Skill parsed{};parsed.id=f.skill.id;parsed.trigger=f.event.trigger;
    auto name=lower(fields["action"]);
    if(name=="forcemove" && fields.contains("action.duration")) {
        fields["action.forcemove.duration"]=fields.at("action.duration");
        fields.erase("action.duration");
    }
    if(name=="runscript" || name=="replaceweapon" ||
       (f.damage && name!="setdamage" && name!="blockdamage"))
        luaL_error(L,"action %s unavailable in this callback",name.c_str());
    if(f.event.sequenceCast && !ActionSequence::prepare(fields,target.has_value()))
        return luaL_error(L,"action %s requires a valid superweapon target",name.c_str());
    if(!readAction(parsed,label,&fields)) luaL_error(L,"invalid action %s in %s",name.c_str(),label.c_str());
    const auto& a=parsed.actions.front();
    if((name=="grantpassive" || name=="changepassivestack") && !skills.contains(a.passive))
        luaL_error(L,"unknown passive %s",a.passive.c_str());
    for(const auto& id:a.passiveList) if(name=="stealpassive" && !skills.contains(id))
        luaL_error(L,"unknown passive %s",id.c_str());
    bool changed=false;
    if(f.damage) {
        if(target && *target!=f.event.victim) luaL_error(L,"damage interception only affects this victim");
        if(*f.damage>0) {
            auto amount=pendingDamageAmount(a,f.event,*f.damage,f.stacks);
            if(!amount) return false;
            *f.damage=*amount;f.event.damage=*amount;changed=true;
        }
    } else {
        auto holder=f.event.trigger=="ondeath"?nullptr:live(L,f.event.holder);
        if(!holder && !f.event.sequenceCast && f.event.trigger!="ondeath") return false;
        changed=apply(a,f.event,holder,f.objects,f.stacks,target,point);
    }
    f.changed|=changed;
    return changed;
}
int result(lua_State* L,bool changed) {
    if(auto damage=frame(L).damage) {lua_pushinteger(L,*damage);lua_setfield(L,1,"damage");}
    lua_pushboolean(L,changed);
    return 1;
}
int direct(lua_State* L) {
    string name=luaL_checkstring(L,2);
    ScriptFields fields;std::optional<DWORD> target;
    std::optional<CellStruct> point;
    arguments(L,3,fields,target,point,false);
    fields["action"]=name;
    return result(L,action(L,fields,frame(L).skill.id,target,point));
}
int sequence(lua_State* L) {
    luaL_checktype(L,2,LUA_TTABLE);
    bool changed=false;
    for(size_t i=1;i<=lua_rawlen(L,2);++i) {
        lua_geti(L,2,i);
        ScriptFields fields;std::optional<DWORD> target;
        std::optional<CellStruct> point;
        arguments(L,-1,fields,target,point,false);lua_pop(L,1);
        bool result=action(L,fields,frame(L).skill.id,target,point);
        changed|=result;
        if(!result && lower(fields["action"])=="consumepassenger") break;
    }
    return result(L,changed);
}
int fromIni(lua_State* L) {
    string section=lower(luaL_checkstring(L,2));
    ScriptFields fields;std::optional<DWORD> target;
    for(const auto& key:sectionKeys(section)) if(actionField(key)) fields[key]=read(section,key.c_str());
    if(frame(L).event.sequenceCast) {
        for(auto& [key,value]:ActionSequence::fields({},section))
            if(actionField(key)) fields[key]=std::move(value);
    }
    if(!fields.contains("action")) return luaL_error(L,"missing Action in [%s]",section.c_str());
    std::optional<CellStruct> point;
    arguments(L,3,fields,target,point,true);
    return result(L,action(L,fields,section,target,point));
}
DWORD eventTarget(const Event& e) {
    if(e.kind=="none") return 0;
    if(e.trigger=="ontakedamage" || e.trigger=="onbeforetakedamage" || e.trigger=="ondeath") return e.attacker;
    return e.victim;
}
int schedule(lua_State* L) {
    auto& f=frame(L);
    auto cast=f.event.sequenceCast;auto& timers=cast?ActionSequence::timers:scriptTimers;
    if(!cast && !live(L,f.event.holder)) {lua_pushnil(L);return 1;}
    auto delay=luaL_checkinteger(L,2);
    const auto now=static_cast<uint32_t>(Unsorted::CurrentFrame);
    if(delay<1 || delay>INT_MAX || delay>UINT32_MAX-now) return luaL_error(L,"invalid timer delay in frames");
    string name=luaL_checkstring(L,3);
    auto& module=*ScriptRuntime::call(L).module;
    if(name.empty() || name.size()>127 || module.file.size()>127 || !includes(module.functions,name))
        return luaL_error(L,"unknown or invalid timer callback %s",name.c_str());
    if(timers.queue.size()>=LuaTimers::maxTasks || timers.lastId==UINT32_MAX)
        return luaL_error(L,"timer limit reached");
    charge(L,1);
    auto data=LuaTaskData::copy(L,4);
    auto id=++timers.lastId;
    timers.insert({now+static_cast<uint32_t>(delay),id},
        {cast?cast:f.event.holder,eventTarget(f.event),f.skill.id,lower(module.file),name,std::move(data)});
    f.changed=true;lua_pushinteger(L,id);return 1;
}
int cancel(lua_State* L) {
    auto& f=frame(L);auto id=luaL_checkinteger(L,2);
    auto cast=f.event.sequenceCast;auto& timers=cast?ActionSequence::timers:scriptTimers;
    bool ok=id>0 && id<=UINT32_MAX && timers.cancel(static_cast<uint32_t>(id),cast?cast:f.event.holder,f.skill.id);
    f.changed|=ok;lua_pushboolean(L,ok);return 1;
}
bool animationPoint(lua_State* L,int index,CoordStruct& out) {
    luaL_checktype(L,index,LUA_TTABLE);index=lua_absindex(L,index);
    lua_getfield(L,index,"x");double x=luaL_checknumber(L,-1);lua_pop(L,1);
    lua_getfield(L,index,"y");double y=luaL_checknumber(L,-1);lua_pop(L,1);
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=512||y>=512)return false;
    CellStruct cell{static_cast<short>(std::floor(x)),static_cast<short>(std::floor(y))};
    auto c=MapClass::Instance.TryGetCellAt(cell);
    if(!c || !MapClass::Instance.IsWithinUsableArea(cell,false))return false;
    out=c->GetCoordsWithBridge();out.X=static_cast<int>(std::lround((x+0.5)*256));out.Y=static_cast<int>(std::lround((y+0.5)*256));return true;
}
int spawnAnim(lua_State* L) {
    auto& f=frame(L);auto name=luaL_checkstring(L,2);CoordStruct where{};
    if(!animationPoint(L,3,where)){lua_pushnil(L);return 1;}
    unsigned duration=150;bool loop=false;int zOffset=0;
    if(!lua_isnoneornil(L,4)){
        luaL_checktype(L,4,LUA_TTABLE);lua_getfield(L,4,"duration");
        if(!lua_isnil(L,-1)){auto n=luaL_checkinteger(L,-1);if(n<1||n>LuaAnim::maxDuration)return luaL_error(L,"animation duration out of range");duration=static_cast<unsigned>(n);}lua_pop(L,1);
        lua_getfield(L,4,"loop");if(!lua_isnil(L,-1))loop=lua_toboolean(L,-1)!=0;lua_pop(L,1);
        lua_getfield(L,4,"height");if(!lua_isnil(L,-1)){double n=luaL_checknumber(L,-1);if(!std::isfinite(n)||n< -64||n>64)return luaL_error(L,"animation height out of range");zOffset=static_cast<int>(std::lround(n*256));}lua_pop(L,1);
    }
    auto type=AnimTypeClass::Find(name);auto owner=live(L,f.event.holder);DWORD cast=f.event.sequenceCast;
    if(!type || !type->GetImage() || (!cast && !owner) || LuaAnim::entries.size()>=LuaAnim::maxEntries){lua_pushnil(L);return 1;}
    charge(L,1);HouseClass* house=owner?owner->Owner:(f.event.owner>=0 && f.event.owner<HouseClass::Array.Count?HouseClass::Array.Items[f.event.owner]:nullptr);
    where.Z+=zOffset;auto anim=playSkillAnim(type,nullptr,where,house,loop);if(!anim){lua_pushnil(L);return 1;}
    auto id=anim->UniqueID;if(!id || !LuaAnim::entries.emplace(id,LuaAnim::Entry{f.event.holder,cast,f.skill.id,duration,loop,zOffset,where,where,0,0,static_cast<unsigned>(Unsorted::CurrentFrame)}).second){anim->UnInit();lua_pushnil(L);return 1;}
    f.changed=true;lua_pushinteger(L,id);return 1;
}
int moveAnim(lua_State* L) {
    if(lua_isnoneornil(L,2)){lua_pushboolean(L,false);return 1;}
    auto& f=frame(L);auto raw=luaL_checkinteger(L,2);CoordStruct target{};
    auto duration=luaL_checkinteger(L,4);if(duration<1||duration>LuaAnim::maxDuration)return luaL_error(L,"animation move duration out of range");
    if(raw<=0||raw>UINT32_MAX || !animationPoint(L,3,target)){lua_pushboolean(L,false);return 1;}
    auto it=LuaAnim::entries.find(static_cast<DWORD>(raw));
    if(it==LuaAnim::entries.end() || !LuaAnim::allowed(it->second,f.event.holder,f.event.sequenceCast,f.skill.id)){lua_pushboolean(L,false);return 1;}
    auto anim=LuaAnim::find(it->first);if(!anim){LuaAnim::entries.erase(it);lua_pushboolean(L,false);return 1;}
    charge(L,1);auto& e=it->second;target.Z+=e.zOffset;e.start=anim->Location;e.finish=target;e.duration=static_cast<unsigned>(duration);e.elapsed=0;
    f.changed=true;lua_pushboolean(L,true);return 1;
}
int removeAnim(lua_State* L) {
    if(lua_isnoneornil(L,2)){lua_pushboolean(L,false);return 1;}
    auto& f=frame(L);auto raw=luaL_checkinteger(L,2);auto it=raw>0 && raw<=UINT32_MAX?LuaAnim::entries.find(static_cast<DWORD>(raw)):LuaAnim::entries.end();
    bool ok=it!=LuaAnim::entries.end() && LuaAnim::allowed(it->second,f.event.holder,f.event.sequenceCast,f.skill.id);
    if(ok && !LuaAnim::find(it->first)){LuaAnim::entries.erase(it);ok=false;}
    if(ok){charge(L,1);LuaAnim::remove(it->first);f.changed=true;}lua_pushboolean(L,ok);return 1;
}
int stateAccess(lua_State* L) {
    auto& f=frame(L);
    luaL_checktype(L,2,LUA_TSTRING);
    size_t size=0;const char* key=lua_tolstring(L,2,&size);
    if(!size || size>127) return luaL_error(L,"state key must contain 1..127 bytes");
    auto cast=f.event.sequenceCast;auto& state=(cast?ActionSequence::timers:scriptTimers).state;
    LuaTimers::StateKey index{cast?cast:f.event.holder,cast?"cast":f.skill.id,string(key,size)};
    auto it=state.find(index);
    if(lua_gettop(L)==2) {
        if(it==state.end()) lua_pushnil(L);else LuaTaskData::push(L,it->second);
        return 1;
    }
    if(!cast && !live(L,f.event.holder)) return luaL_error(L,"cannot set state for a removed holder");
    if(lua_isnil(L,3)) {
        if(it!=state.end()) {state.erase(it);f.changed=true;}
    } else {
        if(it==state.end() && state.size()>=LuaTimers::maxStateKeys)
            return luaL_error(L,"skill state limit reached");
        auto value=LuaTaskData::copy(L,3);
        state.insert_or_assign(std::move(index),std::move(value));f.changed=true;
    }
    return 0;
}
int randomInt(lua_State* L) {
    auto lo=luaL_checkinteger(L,2),hi=luaL_checkinteger(L,3);
    if(lo<INT_MIN || hi>INT_MAX || lo>hi) return luaL_error(L,"expected an ordered 32-bit integer range");
    charge(L,1);
    lua_pushinteger(L,scriptTimers.range(static_cast<int>(lo),static_cast<int>(hi),Game::Seed));return 1;
}
int randomPoint(lua_State* L) {
    luaL_checktype(L,2,LUA_TTABLE);
    lua_getfield(L,2,"center");auto center=pointAt(L,-1);lua_pop(L,1);
    lua_getfield(L,2,"radius");auto radius=luaL_checkinteger(L,-1);lua_pop(L,1);
    if(radius<0 || radius>128) return luaL_error(L,"circle radius must be 0..128 cells");
    int r=static_cast<int>(radius);
    charge(L,1);
    auto usable=[&](int x,int y,CellStruct& p) {
        if(x*x+y*y>r*r) return false;
        if(center.X+x<0 || center.X+x>=512 || center.Y+y<0 || center.Y+y>=512) return false;
        p={static_cast<short>(center.X+x),static_cast<short>(center.Y+y)};
        return MapClass::Instance.TryGetCellAt(p) && MapClass::Instance.IsWithinUsableArea(p,false);
    };
    CellStruct p=center;
    for(int i=0;i<16 && r;++i) {
        int x=scriptTimers.range(-r,r,Game::Seed);
        int y=scriptTimers.range(-r,r,Game::Seed);
        if(usable(x,y,p)) {pushPoint(L,p);return 1;}
    }
    if(!r) {pushPoint(L,center);return 1;}
    // Near very small maps, sample the exact intersection rather than retrying indefinitely.
    charge(L,1+static_cast<unsigned>(r*r/16));
    vector<CellStruct> cells;
    for(int y=-r;y<=r;++y) for(int x=-r;x<=r;++x) if(usable(x,y,p)) cells.push_back(p);
    pushPoint(L,cells[scriptTimers.range(0,static_cast<int>(cells.size())-1,Game::Seed)]);return 1;
}
int invoke(lua_State* L) {
    auto& f=frame(L);auto& e=f.event;
    units(L);
    lua_newtable(L);
    lua_createtable(L,0,static_cast<int>(f.skill.scriptParams.size()));
    for(const auto& [key,value]:f.skill.scriptParams) {
        if(!lua_stringtonumber(L,value.c_str())) lua_pushlstring(L,value.data(),value.size());
        lua_setfield(L,-2,key.c_str());
    }
    lua_setfield(L,-2,"params");
    pushUnit(L,e.trigger=="onmaptrigger" && !live(L,e.holder)?0:e.holder);lua_setfield(L,-2,"self");
    DWORD other=eventTarget(e);
    pushUnit(L,other);lua_setfield(L,-2,"target");
    lua_pushinteger(L,e.damage);lua_setfield(L,-2,"damage");
    lua_pushinteger(L,f.stacks);lua_setfield(L,-2,"stack");
    if(e.sequenceCast) {
        const auto& cast=ActionSequence::casts.at(e.sequenceCast);
        if(cast.point.X>=0 && cast.point.Y>=0) {pushPoint(L,cast.point);lua_setfield(L,-2,"point");}
        lua_pushinteger(L,cast.owner);lua_setfield(L,-2,"owner");
        if(!cast.typeId.starts_with("@map:")) {lua_pushstring(L,cast.typeId.c_str());lua_setfield(L,-2,"superWeapon");}
        lua_pushinteger(L,e.sequenceCast);lua_setfield(L,-2,"castId");
    }
    if(e.trigger=="oncast") {
        auto def=std::lower_bound(ActiveSkills::specs.begin(),ActiveSkills::specs.end(),f.skill.id,
            [](const auto& spec,const string& id){return spec.id<id;});
        if(def!=ActiveSkills::specs.end() && def->id==f.skill.id) {
            auto point=CellClass::Coord2Cell(e.impact);
            pushPoint(L,point);lua_setfield(L,-2,"point");
            if(def->kind==ActiveSkills::Kind::Area) {
                lua_createtable(L,0,2);pushPoint(L,point);lua_setfield(L,-2,"center");
                lua_pushinteger(L,def->radius);lua_setfield(L,-2,"radius");lua_setfield(L,-2,"area");
            } else if(def->kind==ActiveSkills::Kind::Direction) {
                auto start=CellClass::Coord2Cell(e.origin);
                double dx=e.castAim.X-start.X,dy=e.castAim.Y-start.Y,len=std::hypot(dx,dy);
                if(len>0) {
                    lua_createtable(L,0,2);lua_pushnumber(L,dx/len);lua_setfield(L,-2,"x");
                    lua_pushnumber(L,dy/len);lua_setfield(L,-2,"y");lua_setfield(L,-2,"direction");
                }
            }
        }
    }
    lua_newtable(L);
    if(e.trigger=="onfire" && e.hasImpact) {
        pushPoint(L,CellClass::Coord2Cell(e.impact));lua_setfield(L,-2,"targetPoint");
    }
    lua_pushinteger(L,e.hp);lua_setfield(L,-2,"selfHealth");
    lua_pushinteger(L,e.maxHp);lua_setfield(L,-2,"selfMaxHealth");
    if(e.targetMaxHp>0) {
        lua_pushinteger(L,e.targetHp);lua_setfield(L,-2,"targetHealth");
        lua_pushinteger(L,e.targetMaxHp);lua_setfield(L,-2,"targetMaxHealth");
    }
    lua_setfield(L,-2,"event");
    lua_newtable(L);lua_newtable(L);
    lua_pushcfunction(L,stateAccess);lua_setfield(L,-2,"__index");
    lua_pushcfunction(L,stateAccess);lua_setfield(L,-2,"__newindex");
    lua_setmetatable(L,-2);lua_setfield(L,-2,"state");
    lua_pushinteger(L,Unsorted::CurrentFrame);lua_setfield(L,-2,"frame");
    if(f.taskId) {lua_pushinteger(L,f.taskId);lua_setfield(L,-2,"taskId");}
    static const luaL_Reg methods[]={{"schedule",schedule},{"cancel",cancel},{"randomInt",randomInt},{"randomPoint",randomPoint},{"nearbyUnits",nearby},{"house",houseInfo},{"action",direct},{"actions",sequence},{"runAction",fromIni},{"spawnAnim",spawnAnim},{"moveAnim",moveAnim},{"removeAnim",removeAnim},{nullptr,nullptr}};
    luaL_setfuncs(L,methods,0);
    if(f.taskData) LuaTaskData::push(L,*f.taskData);
    lua_call(L,f.taskData?2:1,0);return 0;
}
}
bool runScript(const PassiveAction& a,Event& event,const Objects& objects,int stacks,int* pendingDamage) {
    auto mod=scriptModules.find(a.script);
    auto skill=skills.find(a.skillId);
    const Skill* definition=event.sequenceCast?ActionSequence::stepFor(ActionSequence::casts.at(event.sequenceCast),a.skillId):
        skill==skills.end()?nullptr:&skill->second;
    if(mod==scriptModules.end() || !definition) return false;
    ScriptActions::Frame frame{*definition,event,objects,stacks,pendingDamage};
    ScriptRuntime::Call call{&mod->second,nullptr,&a.function,ScriptActions::invoke,&frame};
    string error;
    if(!ScriptRuntime::run(call,error)) configLog("Lua ["+a.skillId+"] "+a.script+"."+a.function+": "+error);
    return frame.changed;
}

Event sequenceEvent(DWORD id,const ActionSequence::Cast& cast) {
    Event e{};e.trigger="onsuperweapon";e.kind="none";e.owner=cast.owner;e.sequenceCast=id;
    if(cast.typeId.starts_with("@map:")) {
        e.trigger="onmaptrigger";
        auto split=cast.typeId.find(':',5);
        if(split!=string::npos) {
            DWORD object=0;auto value=cast.typeId.substr(split+1);
            auto [end,error]=std::from_chars(value.data(),value.data()+value.size(),object);
            if(error==std::errc{} && end==value.data()+value.size()) e.holder=object;
        }
    }
    if(cast.point.X>=0 && cast.point.Y>=0) {
        if(auto cell=MapClass::Instance.TryGetCellAt(cast.point)) {e.hasImpact=true;e.origin=e.impact=cell->GetCoordsWithBridge();}
    }
    return e;
}
void tickScriptQueue(LuaTimers::Store& store,bool super) {
    const auto now=static_cast<uint32_t>(Unsorted::CurrentFrame);
    auto& queue=store.queue;
    if(queue.empty() || queue.begin()->first.first>now) return;
    if(!ready) readSkills();
    Objects live;
    for(auto unit:TechnoClass::Array) if(alive(unit)) live.emplace(unit->UniqueID,unit);
    auto previous=activeObjects;
    activeObjects=&live;
    while(!queue.empty() && queue.begin()->first.first<=now) {
        auto node=queue.extract(queue.begin());
        auto id=node.key().second;store.byId.erase(id);
        auto& task=node.mapped();
        auto holder=findLive(live,task.owner);
        if(!super && !holder) continue;
        auto mod=scriptModules.find(task.file);
        auto skill=skills.find(task.skill);
        auto cast=ActionSequence::casts.find(task.owner);
        const Skill* definition=super?(cast==ActionSequence::casts.end()?nullptr:ActionSequence::stepFor(cast->second,task.skill)):
            skill==skills.end()?nullptr:&skill->second;
        if(mod==scriptModules.end() || !definition || !includes(mod->second.functions,task.callback)) {
            if(super) ActionSequence::retire(task.owner);
            configLog("Lua timer: missing ["+task.skill+"] "+task.file+"."+task.callback);continue;
        }
        auto other=findLive(live,task.target);
        auto event=super?sequenceEvent(task.owner,cast->second):eventSnapshot("onscheduled",holder,other,holder,other,0);
        ScriptActions::Frame frame{*definition,event,live,super?0:stackCount(task.owner,task.skill),nullptr};
        frame.taskData=&task.data;frame.taskId=id;
        ScriptRuntime::Call call{&mod->second,nullptr,&task.callback,ScriptActions::invoke,&frame};
        string error;
        if(!ScriptRuntime::run(call,error)) configLog("Lua timer ["+task.skill+"] "+task.file+"."+task.callback+": "+error);
        if(super) ActionSequence::retire(task.owner);
    }
    activeObjects=previous;
}
void tickScriptTimers() {tickScriptQueue(scriptTimers,false);tickScriptQueue(ActionSequence::timers,true); }
