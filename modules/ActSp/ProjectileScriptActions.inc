// Projectile points have fractional cell-center X/Y and absolute Z, in cells.
CoordStruct surface(CoordStruct at) {
    at.Z=MapClass::Instance.GetCellFloorHeight(at);
    if(auto cell=MapClass::Instance.TryGetCellAt(at)) at=cell->FixHeight(at);
    return at;
}
CoordStruct worldPoint(lua_State* L,int index,bool project=false) {
    index=lua_absindex(L,index);
    luaL_checktype(L,index,LUA_TTABLE);
    auto axis=[&](const char* key) {
        lua_getfield(L,index,key);double n=numberAt(L,-1);lua_pop(L,1);
        return int(std::round((n+.5)*256));
    };
    CoordStruct at{axis("x"),axis("y"),0};
    if(at.X<0 || at.Y<0 || at.X>=512*256 || at.Y>=512*256 ||
       !MapClass::Instance.IsWithinUsableArea(CellClass::Coord2Cell(at),false))
        luaL_error(L,"projectile point outside playable map");
    if(project) return surface(at);
    lua_getfield(L,index,"z");
    at.Z=lua_isnil(L,-1)?surface(at).Z:int(std::round(numberAt(L,-1)*256));
    lua_pop(L,1);
    return at;
}
int groundPoint(lua_State* L) {
    point(L,lua_isnoneornil(L,2)?surface(current(L).position()):worldPoint(L,2,true));
    return 1;
}
void chargeOperations(lua_State* L,unsigned cost) {
    auto& count=current(L).operations;
    if(cost>4096-count) luaL_error(L,"projectile action/query budget exceeded");
    count+=cost;
}
int nearbyUnits(lua_State* L) {
    const auto at=worldPoint(L,2);
    const double radius=luaL_checknumber(L,3);
    if(!std::isfinite(radius) || radius<0 || radius>128) return luaL_error(L,"nearbyUnits range must be 0..128 cells");
    ScriptActions::NearbyFilter filter;
    filter.read(L);
    auto b=current(L).bullet;
    auto house=EntityWalls::firingHouse(b);
    lua_newtable(L);
    if(!house) return 1;
    chargeOperations(L,1+TechnoClass::Array.Count/16);
    vector<DWORD> hits;
    for(auto unit:TechnoClass::Array) {
        if(!alive(unit) || !unit->Owner || unit->InLimbo || !unit->IsOnMap || (!filter.includeSelf && b->Owner==unit)) continue;
        if(distanceCells(at,unit->GetCenterCoords())<=radius && filter.matches(unit,house)) hits.push_back(unit->UniqueID);
    }
    std::sort(hits.begin(),hits.end());
    int index=0;
    for(auto id:hits) {ScriptActions::pushUnit(L,id);lua_rawseti(L,-2,++index);}
    return 1;
}
int action(lua_State* L) {
    auto& ctx=current(L);
    const string name=lower(luaL_checkstring(L,2));
    if(!includes({"detonatewarhead","fireweapon","playanim","playsound","spawnparticlesystem"},name))
        return luaL_error(L,"unsupported projectile action %s",name.c_str());
    chargeOperations(L,1);
    luaL_checktype(L,3,LUA_TTABLE);
    ScriptFields fields{{"action",name}};
    auto origin=ctx.position(),destination=origin;
    TechnoClass* unit=nullptr;
    bool lostTarget=false,hasTarget=false,hasOrigin=false;
    lua_pushnil(L);
    while(lua_next(L,3)) {
        if(lua_type(L,-2)!=LUA_TSTRING) return luaL_error(L,"expected named projectile action fields");
        auto key=lower(lua_tostring(L,-2));
        if(key.starts_with("action.")) key.erase(0,7);
        if(key=="target") {
            if(hasTarget) return luaL_error(L,"duplicate Target");
            hasTarget=true;
            if(luaL_testudata(L,-1,"Starfall.Unit")) {
                TargetClass ref;
                ref.m_ID=ScriptActions::handle(L,-1);ref.m_RTTI=static_cast<unsigned char>(AbstractType::Abstract);
                unit=tracked(ref);
                if(unit) destination=unit->GetTargetCoords();
                else lostTarget=true;
            } else destination=worldPoint(L,-1);
        } else if(key=="origin" && name=="fireweapon") {
            if(hasOrigin) return luaL_error(L,"duplicate Origin");
            origin=worldPoint(L,-1);hasOrigin=true;
        } else {
            const bool valid=(name=="detonatewarhead" && (key=="warhead" || key=="amount")) ||
                (name=="fireweapon" && key=="weapon") || (name=="playanim" && key=="anim") ||
                (name=="playsound" && key=="sound") || (name=="spawnparticlesystem" && key=="particlesystem");
            if(!valid || !lua_isstring(L,-1)) return luaL_error(L,"invalid projectile action field %s",key.c_str());
            if(!fields.emplace("action."+key,lua_tostring(L,-1)).second) return luaL_error(L,"duplicate action field");
        }
        lua_pop(L,1);
    }
    Skill parsed{};
    if(!readAction(parsed,"ProjectileScript",&fields)) return luaL_error(L,"invalid %s action",name.c_str());
    auto& effect=parsed.actions.front();
    auto b=ctx.bullet;
    if(!hasTarget && name=="fireweapon") {unit=tracked(ctx.flight.target);destination=ctx.flight.aim;}
    auto cell=MapClass::Instance.TryGetCellAt(destination);
    auto house=EntityWalls::firingHouse(b);
    bool ok=false;
    if(cell && !lostTarget) {
        AbstractClass* aim=unit?static_cast<AbstractClass*>(unit):cell;
        if(name=="playanim") ok=playSkillAnim(effect.playAnim,unit,destination,house)!=nullptr;
        else if(name=="playsound") {playActionSound(effect.sound,destination);ok=true;}
        else if(name=="spawnparticlesystem") ok=particleEffect(effect,b->Owner,aim,origin,destination,house);
        else ok=weaponEffect(effect,b->Owner,aim,destination,origin,house,
                             name=="fireweapon"?std::optional<CoordStruct>{origin}:std::nullopt);
    }
    lua_pushboolean(L,ok);
    return 1;
}
