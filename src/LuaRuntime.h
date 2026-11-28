#pragma once
#include "../references/lua/lua.h"
#include "../references/lua/lauxlib.h"
#include "../references/lua/lualib.h"
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
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
    uint64_t revision=0;
};
struct Call {
    Module* module;
    const std::string* source;
    const std::string* function;
    lua_CFunction invoke;
    void* context;
    int ticks=100;
};
inline Call& call(lua_State* L) { return **static_cast<Call**>(lua_getextraspace(L)); }
inline void* allocate(void* ud,void* ptr,size_t old,size_t size) {
    auto& used=*static_cast<size_t*>(ud);
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
inline int compile(lua_State* L) {
    auto& c=call(L);
    auto& m=*c.module;
    environment(L);
    lua_sethook(L,budget,LUA_MASKCOUNT,1000);
    const auto& bytes=*c.source;
    lua_pushfstring(L,"@%s",m.file.c_str());
    if(luaL_loadbufferx(L,bytes.data(),bytes.size(),lua_tostring(L,-1),"t")!=LUA_OK) return lua_error(L);
    lua_remove(L,-2);
    if(lua_dump(L,dump,&m.bytecode,0)) return luaL_error(L,"cannot compile script");
    lua_call(L,0,1);
    luaL_checktype(L,1,LUA_TTABLE);
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
struct Machine {
    size_t bytes=0,baseline=0;
    lua_State* L=lua_newstate(allocate,&bytes);
    uint64_t revision=0;
    lua_CFunction binding=nullptr;
    ~Machine() {if(L) lua_close(L);}
    Machine()=default;
    Machine(const Machine&)=delete;
};
// Only idle VMs live in this list. A nested action can never borrow its caller's VM.
struct Pool {
    static constexpr size_t idleLimit=8;
    std::vector<std::unique_ptr<Machine>> idle; // oldest first
    uint64_t generation=0;
    std::unique_ptr<Machine> take(const Call& c) {
        for(size_t n=idle.size();n>0;--n) {
            auto& candidate=idle[n-1];
            if(candidate->revision!=c.module->revision || candidate->binding!=c.invoke) continue;
            auto machine=std::move(candidate);
            idle.erase(idle.begin()+(n-1));
            return machine;
        }
        if(idle.size()>=idleLimit) {
            // Keep the initialized libraries on a module miss. Native metatables
            // belong to their binding (e.g. skill versus projectile unit handles).
            for(auto it=idle.begin();it!=idle.end();++it) if((*it)->binding==c.invoke) {
                auto machine=std::move(*it);
                idle.erase(it);
                return machine;
            }
            idle.erase(idle.begin());
        }
        return std::make_unique<Machine>();
    }
    void put(std::unique_ptr<Machine> machine) {
        if(idle.size()>=idleLimit) idle.erase(idle.begin());
        idle.push_back(std::move(machine));
    }
};
inline Pool& pool() {static Pool p;return p;}
inline void clearPool() {auto& p=pool();++p.generation;p.idle.clear();}
inline void copyTable(lua_State* L,int source,int capacity) {
    source=lua_absindex(L,source);
    lua_createtable(L,0,capacity);
    int dest=lua_gettop(L);
    lua_pushnil(L);
    while(lua_next(L,source)) {
        lua_pushvalue(L,-2);lua_pushvalue(L,-2);lua_rawset(L,dest);lua_pop(L,1);
    }
}
inline int loadFactory(lua_State* L) {
    const auto& m=*call(L).module;
    if(luaL_loadbufferx(L,m.bytecode.data(),m.bytecode.size(),m.file.c_str(),"b")!=LUA_OK) return lua_error(L);
    return 1;
}
inline int prepare(lua_State* L) {
    environment(L);
    loadFactory(L);
    lua_pushglobaltable(L);
    lua_pushliteral(L,"");lua_getmetatable(L,-1);lua_remove(L,-2);
    return 3; // compiled factory, pristine globals, string metatable
}
inline int invokeIsolated(lua_State* L) {
    // Arguments are the factory, pristine globals and the primitive string metatable.
    copyTable(L,2,32);
    const int env=lua_gettop(L);
    lua_pushvalue(L,env);lua_setfield(L,env,"_G");
    for(const char* name:{"math","table","string","utf8"}) {
        lua_getfield(L,2,name);copyTable(L,-1,32);lua_remove(L,-2);lua_setfield(L,env,name);
    }
    lua_getfield(L,env,"string");lua_setfield(L,3,"__index");
    lua_pushvalue(L,env);lua_rawseti(L,LUA_REGISTRYINDEX,LUA_RIDX_GLOBALS);
    lua_setupvalue(L,1,1);
    lua_settop(L,1);
    lua_sethook(L,budget,LUA_MASKCOUNT,1000);
    lua_call(L,0,1);luaL_checktype(L,1,LUA_TTABLE);
    auto& c=call(L);
    lua_getfield(L,1,c.function->c_str());
    if(!lua_isfunction(L,-1)) return luaL_error(L,"missing callback %s",c.function->c_str());
    return c.invoke(L);
}
inline bool run(Call& c,std::string& error) {
    auto& p=pool();
    const auto generation=p.generation;
    auto vm=c.source?std::make_unique<Machine>():p.take(c);
    auto L=vm->L;
    if(!L) {error="cannot allocate Lua state";return false;}
    *static_cast<Call**>(lua_getextraspace(L))=&c;
    if(!lua_gettop(L)) lua_pushcfunction(L,trace); // permanent stack slot 1
    auto checked=[&](int args,int results) {
        if(lua_pcall(L,args,results,1)==LUA_OK) return true;
        auto msg=lua_tostring(L,-1);error=msg?msg:"unknown Lua error";return false;
    };
    if(c.source) {
        Module compiled;
        compiled.file=c.module->file;
        Call validation=c;
        validation.module=&compiled;
        *static_cast<Call**>(lua_getextraspace(L))=&validation;
        lua_pushcfunction(L,compile);
        const bool ok=checked(0,0);
        *static_cast<Call**>(lua_getextraspace(L))=nullptr;
        if(ok) {
            static uint64_t nextRevision=0;
            compiled.revision=++nextRevision;
            *c.module=std::move(compiled);
        }
        return ok;
    }
    if(lua_gettop(L)==1) {
        lua_pushcfunction(L,prepare);
        if(!checked(0,3)) return false;
        vm->binding=c.invoke;vm->baseline=vm->bytes;
    } else if(vm->revision!=c.module->revision) {
        lua_pushnil(L);lua_replace(L,2); // allow emergency GC to reclaim the previous prototype
        lua_pushcfunction(L,loadFactory);
        if(!checked(0,1)) return false;
        lua_replace(L,2);
        if(vm->bytes>512*1024) lua_gc(L,LUA_GCCOLLECT);
        vm->baseline=vm->bytes;
    }
    vm->revision=c.module->revision;
    lua_pushcfunction(L,invokeIsolated);
    lua_pushvalue(L,2);lua_pushvalue(L,3);lua_pushvalue(L,4);
    if(!checked(3,0)) return false; // discard the entire VM after any script error
    lua_sethook(L,nullptr,0,0);
    lua_pushvalue(L,3);lua_setupvalue(L,2,1);
    lua_pushvalue(L,3);lua_rawseti(L,LUA_REGISTRYINDEX,LUA_RIDX_GLOBALS);
    lua_getfield(L,3,"string");lua_setfield(L,4,"__index");
    *static_cast<Call**>(lua_getextraspace(L))=nullptr;
    if(vm->bytes>512*1024 && vm->bytes>vm->baseline*2) lua_gc(L,LUA_GCCOLLECT);
    if(vm->revision && p.generation==generation) p.put(std::move(vm));
    return true;
}
}
