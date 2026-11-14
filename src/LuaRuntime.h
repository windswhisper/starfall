#pragma once
#include "../references/lua/lua.h"
#include "../references/lua/lauxlib.h"
#include "../references/lua/lualib.h"
#include <climits>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

// A module is re-created from its cached prototype for each event. Local variables
// and unit handles therefore cannot leak from one event (or saved game) into another.
namespace ScriptRuntime {
struct Module {
    std::string file,bytecode,id,trigger,callback,units,signal;
    int cooldown=0,interval=0;
    std::string name,icon,castMode,targetKind,shape,houses;
    int castRange=0,radius=0,distance=0;
    std::vector<std::string> functions;
};
struct Call {
    Module* module;
    const std::string* source;
    const std::string* function;
    lua_CFunction invoke;
    void* context;
    size_t bytes=0;
    int ticks=100;
};
inline Call& call(lua_State* L) { return **static_cast<Call**>(lua_getextraspace(L)); }
inline void* allocate(void* ud,void* ptr,size_t old,size_t size) {
    auto& used=static_cast<Call*>(ud)->bytes;
    if(!ptr) old=0;
    if(!size) {std::free(ptr);used-=old;return nullptr;}
    if(size>8*1024*1024-(used-old)) return nullptr;
    auto result=std::realloc(ptr,size);
    if(result) used=used-old+size;
    return result;
}
inline void budget(lua_State* L,lua_Debug*) {
    if(--call(L).ticks<=0) luaL_error(L,"script instruction limit reached");
}
inline void library(lua_State* L,const char* name,lua_CFunction open) {
    luaL_requiref(L,name,open,1);lua_pop(L,1);
}
inline void environment(lua_State* L) {
    library(L,"_G",luaopen_base);
    for(const char* name:{"load","loadfile","dofile","collectgarbage","print","warn","getmetatable","setmetatable",
                          "pcall","xpcall","pairs","next","tostring","rawset"}) {
        lua_pushnil(L);lua_setglobal(L,name);
    }
    library(L,"table",luaopen_table);library(L,"string",luaopen_string);
    library(L,"math",luaopen_math);library(L,"utf8",luaopen_utf8);
    lua_getglobal(L,"math");
    for(auto key:{"random","randomseed"}) {lua_pushnil(L);lua_setfield(L,-2,key);}
    lua_pop(L,1);
    lua_getglobal(L,"string");
    for(auto key:{"dump","format"}) {lua_pushnil(L);lua_setfield(L,-2,key);}
    lua_pop(L,1);
}
inline int dump(lua_State*,const void* data,size_t size,void* ud) {
    static_cast<std::string*>(ud)->append(static_cast<const char*>(data),size);return 0;
}
inline std::string field(lua_State* L,int table,const char* key) {
    lua_getfield(L,table,key);
    std::string value;
    if(!lua_isnil(L,-1)) {
        if(lua_type(L,-1)!=LUA_TSTRING) luaL_error(L,"%s must be a string",key);
        value=lua_tostring(L,-1);
    }
    lua_pop(L,1);return value;
}
inline int integerField(lua_State* L,const char* key,int table=1) {
    lua_getfield(L,table,key);
    lua_Integer value=0;
    if(!lua_isnil(L,-1)) {
        if(!lua_isinteger(L,-1)) luaL_error(L,"%s must be an integer",key);
        value=lua_tointeger(L,-1);
        if(value<0 || value>INT_MAX) luaL_error(L,"%s outside supported range",key);
    }
    lua_pop(L,1);return static_cast<int>(value);
}
inline int readLegacyMetadata(lua_State* L,Module& m) {
    m.id=field(L,1,"id");m.trigger=field(L,1,"trigger");m.callback=field(L,1,"callback");
    m.signal=field(L,1,"signal");m.cooldown=integerField(L,"cooldown");m.interval=integerField(L,"interval");
    m.name=field(L,1,"name");m.icon=field(L,1,"icon");m.castMode=field(L,1,"castMode");
    lua_getfield(L,1,"targeting");
    if(!lua_isnil(L,-1)) {
        luaL_checktype(L,-1,LUA_TTABLE);
        m.targetKind=field(L,2,"kind");m.shape=field(L,2,"shape");
        m.castRange=integerField(L,"castRange",2);m.radius=integerField(L,"radius",2);m.distance=integerField(L,"distance",2);
        lua_getfield(L,2,"houses");
        if(!lua_isnil(L,-1)) {
            luaL_checktype(L,-1,LUA_TTABLE);
            for(size_t i=1;i<=lua_rawlen(L,-1);++i) {
                lua_geti(L,-1,i);
                if(lua_type(L,-1)!=LUA_TSTRING) return luaL_error(L,"targeting.houses needs a list of house filters");
                if(!m.houses.empty()) m.houses+=",";
                m.houses+=lua_tostring(L,-1);lua_pop(L,1);
            }
        }
        lua_pop(L,1);
    }
    lua_pop(L,1);
    lua_getfield(L,1,"units");
    if(!lua_isnil(L,-1)) {
        luaL_checktype(L,-1,LUA_TTABLE);
        for(size_t i=1;i<=lua_rawlen(L,-1);++i) {
            lua_geti(L,-1,i);
            if(lua_type(L,-1)!=LUA_TSTRING) return luaL_error(L,"units must be a list of type IDs");
            if(!m.units.empty()) m.units+=",";
            m.units+=lua_tostring(L,-1);lua_pop(L,1);
        }
    }
    lua_pop(L,1);
    return 0;
}
inline int execute(lua_State* L) {
    auto& c=call(L);
    auto& m=*c.module;
    environment(L);
    lua_sethook(L,budget,LUA_MASKCOUNT,1000);
    const auto& bytes=c.source?*c.source:m.bytecode;
    std::string label="@"+m.file;
    if(luaL_loadbufferx(L,bytes.data(),bytes.size(),label.c_str(),c.source?"t":"b")!=LUA_OK) return lua_error(L);
    if(c.source && lua_dump(L,dump,&m.bytecode,0)) return luaL_error(L,"cannot compile script");
    lua_call(L,0,1);
    luaL_checktype(L,1,LUA_TTABLE);
    if(!c.source) {
        lua_getfield(L,1,c.function->c_str());
        if(!lua_isfunction(L,-1)) return luaL_error(L,"missing callback %s",c.function->c_str());
        return c.invoke(L);
    }
    lua_getfield(L,1,"id");
    bool legacy=!lua_isnil(L,-1) && !lua_isfunction(L,-1);
    lua_pop(L,1);
    if(legacy) readLegacyMetadata(L,m);
    lua_pushnil(L);
    while(lua_next(L,1)) {
        if(lua_type(L,-2)==LUA_TSTRING && lua_isfunction(L,-1)) m.functions.emplace_back(lua_tostring(L,-2));
        lua_pop(L,1);
    }
    return 0;
}
inline int trace(lua_State* L) { luaL_traceback(L,L,lua_tostring(L,1),1);return 1; }
inline bool run(Call& c,std::string& error) {
    auto L=lua_newstate(allocate,&c);
    if(!L) {error="cannot allocate Lua state";return false;}
    *static_cast<Call**>(lua_getextraspace(L))=&c;
    lua_pushcfunction(L,trace);
    lua_pushcfunction(L,execute);
    bool ok=lua_pcall(L,0,0,1)==LUA_OK;
    if(!ok) {auto msg=lua_tostring(L,-1);error=msg?msg:"unknown Lua error";}
    lua_close(L);return ok;
}
}
