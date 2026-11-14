#pragma once
#include "LuaRuntime.h"
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <set>

// Plain values only. Ordered keys make serialized tasks independent of Lua's hash seed.
namespace LuaTaskData {
using std::string;
enum class Type : unsigned { Nil, False, True, Integer, Number, String, Unit, Table };
struct Value {
    Type type=Type::Nil;
    lua_Integer integer=0;
    double number=0;
    string text;
    std::map<lua_Integer,Value> indices;
    std::map<string,Value> fields;
};
constexpr size_t maxBytes=8192;
struct Limits {
    size_t bytes=maxBytes;
    int nodes=256;
    bool take(size_t size) {
        if(size>bytes) return false;
        bytes-=size;return true;
    }
};
inline string readString(lua_State* L,int index,Limits& limits) {
    size_t length=0;
    auto s=lua_tolstring(L,index,&length);
    if(!limits.take(length)) luaL_error(L,"task data exceeds 8 KiB");
    return string(s,length);
}
inline Value copy(lua_State* L,int index,Limits& limits,std::set<const void*>& ancestors,int depth) {
    if(depth>12 || --limits.nodes<0) luaL_error(L,"task data is too deep or too large");
    luaL_checkstack(L,4,"copying task data");
    index=lua_absindex(L,index);
    Value v;
    switch(lua_type(L,index)) {
    case LUA_TNONE: case LUA_TNIL: break;
    case LUA_TBOOLEAN: v.type=lua_toboolean(L,index)?Type::True:Type::False;break;
    case LUA_TNUMBER:
        if(lua_isinteger(L,index)) {v.type=Type::Integer;v.integer=lua_tointeger(L,index);}
        else {
            v.type=Type::Number;v.number=lua_tonumber(L,index);
            if(!std::isfinite(v.number)) luaL_error(L,"task data needs finite numbers");
        }
        break;
    case LUA_TSTRING: v.type=Type::String;v.text=readString(L,index,limits);break;
    case LUA_TUSERDATA: {
        auto id=static_cast<uint32_t*>(luaL_testudata(L,index,"Starfall.Unit"));
        if(!id) luaL_error(L,"unsupported task userdata");
        v.type=Type::Unit;uint32_t value;std::memcpy(&value,id,sizeof(value));v.integer=value;break;
    }
    case LUA_TTABLE: {
        if(lua_getmetatable(L,index)) luaL_error(L,"task data needs a plain table");
        auto identity=lua_topointer(L,index);
        if(!ancestors.insert(identity).second) luaL_error(L,"cyclic task data");
        v.type=Type::Table;
        lua_pushnil(L);
        while(lua_next(L,index)) {
            if(lua_isinteger(L,-2)) v.indices.emplace(lua_tointeger(L,-2),copy(L,-1,limits,ancestors,depth+1));
            else if(lua_type(L,-2)==LUA_TSTRING) {
                auto key=readString(L,-2,limits);
                v.fields.emplace(std::move(key),copy(L,-1,limits,ancestors,depth+1));
            } else luaL_error(L,"task table keys must be integers or strings");
            lua_pop(L,1);
        }
        ancestors.erase(identity);break;
    }
    default: luaL_error(L,"cannot persist Lua %s",luaL_typename(L,index));
    }
    return v;
}
inline Value copy(lua_State* L,int index) {
    Limits limits;std::set<const void*> ancestors;
    return copy(L,index,limits,ancestors,0);
}
inline void push(lua_State* L,const Value& v) {
    luaL_checkstack(L,4,"restoring task data");
    switch(v.type) {
    case Type::Nil: lua_pushnil(L);break;
    case Type::True: case Type::False: lua_pushboolean(L,v.type==Type::True);break;
    case Type::Integer: lua_pushinteger(L,v.integer);break;
    case Type::Number: lua_pushnumber(L,v.number);break;
    case Type::String: lua_pushlstring(L,v.text.data(),v.text.size());break;
    case Type::Unit: {
        uint32_t id=static_cast<uint32_t>(v.integer);
        std::memcpy(lua_newuserdatauv(L,sizeof(id),0),&id,sizeof(id));
        luaL_setmetatable(L,"Starfall.Unit");break;
    }
    case Type::Table:
        lua_newtable(L);
        for(const auto& [k,value]:v.indices) {push(L,value);lua_seti(L,-2,k);}
        for(const auto& [k,value]:v.fields) {lua_pushlstring(L,k.data(),k.size());push(L,value);lua_rawset(L,-3);}
        break;
    }
}
template<class W> void writeInteger(W& w,lua_Integer i) {
    auto bits=std::bit_cast<uint64_t>(i);
    w.word(static_cast<uint32_t>(bits));w.word(static_cast<uint32_t>(bits>>32));
}
template<class R> lua_Integer readInteger(R& r) {
    uint64_t bits=r.word();bits|=uint64_t(r.word())<<32;return std::bit_cast<lua_Integer>(bits);
}
template<class W> void write(W& w,const Value& v) {
    w.word(static_cast<unsigned>(v.type));
    switch(v.type) {
    case Type::Integer: writeInteger(w,v.integer);break;
    case Type::Number: w.real(v.number);break;
    case Type::String: w.str(v.text);break;
    case Type::Unit: w.word(static_cast<unsigned>(v.integer));break;
    case Type::Table:
        w.word(static_cast<unsigned>(v.indices.size()));
        for(const auto& [k,child]:v.indices) {writeInteger(w,k);write(w,child);}
        w.word(static_cast<unsigned>(v.fields.size()));
        for(const auto& [k,child]:v.fields) {w.str(k);write(w,child);}
        break;
    default: break;
    }
}
template<class R> Value read(R& r,Limits& limits,int depth) {
    Value v;
    if(depth>12 || --limits.nodes<0) {r.ok=false;return v;}
    v.type=static_cast<Type>(r.word());
    switch(v.type) {
    case Type::Nil: case Type::False: case Type::True: break;
    case Type::Integer: v.integer=readInteger(r);break;
    case Type::Number: v.number=r.real();break;
    case Type::Unit: v.integer=r.word();if(!v.integer) r.ok=false;break;
    case Type::String: v.text=r.str(limits.bytes);limits.take(v.text.size());break;
    case Type::Table:
        for(auto n=r.word();r.ok && n--;) {
            auto k=readInteger(r);
            if(!v.indices.emplace(k,read(r,limits,depth+1)).second) r.ok=false;
        }
        for(auto n=r.word();r.ok && n--;) {
            auto k=r.str(limits.bytes);limits.take(k.size());
            if(!v.fields.emplace(std::move(k),read(r,limits,depth+1)).second) r.ok=false;
        }
        break;
    default: r.ok=false;
    }
    return v;
}
template<class R> Value read(R& r) {Limits limits;return read(r,limits,0);}
}
