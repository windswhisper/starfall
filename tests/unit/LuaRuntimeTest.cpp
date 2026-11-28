#include "../../src/LuaTaskData.h"
#include <cstdio>

int copyValue(lua_State* L) {
    auto value=LuaTaskData::copy(L,1);
    LuaTaskData::push(L,value);
    return 1;
}
int invoke(lua_State* L) {
    lua_pushcfunction(L,copyValue);
    lua_call(L,1,0);
    return 0;
}
struct Nested {
    lua_State* outer=nullptr;
    bool verified=false;
};
int nested(lua_State* outer) {
    auto& parent=ScriptRuntime::call(outer);
    auto& state=*static_cast<Nested*>(parent.context);
    state.outer=outer;
    std::string fn="child",error;
    ScriptRuntime::Call child{parent.module,nullptr,&fn,parent.invoke,&state};
    bool ok=ScriptRuntime::run(child,error);
    state.verified=ok && &ScriptRuntime::call(outer)==&parent;
    lua_pushboolean(outer,state.verified);
    return 1;
}
int invokeNested(lua_State* L) {
    auto& c=ScriptRuntime::call(L);
    if(*c.function=="child") {
        if(static_cast<Nested*>(c.context)->outer==L) return luaL_error(L,"nested VM is busy");
        ScriptRuntime::clearPool(); // active leases must not re-enter a reset pool
        lua_call(L,0,0);
    } else {
        lua_pushcfunction(L,nested);lua_call(L,1,0);
    }
    return 0;
}

int main() {
    ScriptRuntime::Module module;
    module.file="fixture.lua";
    std::string source=R"(
        local calls = 0
        return {
            id = function() end,
            cooldown = function() end,
            targeting = function() end,
            check = function()
                calls = calls + 1
                assert(calls == 1 and os == nil and io == nil and load == nil)
                assert(_G == _ENV and marker == nil and math.floor(1.9) == 1)
                assert(rawget(_G,"string")==string and ("x"):upper()=="X" and string.extra==nil)
                assert(table.concat({"a","b"})=="ab" and utf8.len("abc")==3)
            end,
            dirty = function()
                _G.marker=5; math.floor=nil; table.concat=nil; utf8.len=nil
                string.extra=123; string.upper=function() return "changed" end
                assert(("x"):upper()=="changed")
                _ENV={}; lost=true
            end,
            outer = function(enter) marker=11; assert(enter()); assert(marker==11) end,
            child = function() assert(marker==nil); marker=22 end,
            large = function() local s=string.rep("b",1024*1024); assert(#s==1024*1024) end,
            values = function(copy)
                local v = { n = 1.5, [-3] = "a\0b", [9] = false, p = {1, 2} }
                local saved = copy(v)
                v.p[1] = -1
                assert(saved.n == 1.5 and saved[-3] == "a\0b" and saved[9] == false and saved.p[1] == 1)
                assert(copy(math.maxinteger) == math.maxinteger and copy(math.mininteger) == math.mininteger)
            end,
            cycle = function(copy) local t = {}; t.self = t; copy(t) end,
            oversized = function(copy) copy(string.rep("x", 8193)) end,
            closure = function(copy) copy({f = function() end}) end,
            spin = function() while true do end end,
            allocate = function() return string.rep("x", 12 * 1024 * 1024) end,
        }
    )";
    std::string error;
    ScriptRuntime::Call compile{&module,&source,nullptr,nullptr,nullptr};
    if(!ScriptRuntime::run(compile,error)) return 1;
    for(std::string name:{"check","dirty","check","large","check","values","cycle","oversized","closure","spin","allocate","check"}) {
        ScriptRuntime::Call call{&module,nullptr,&name,invoke,nullptr};
        bool ok=ScriptRuntime::run(call,error);
        if(ok!=(name=="check" || name=="values" || name=="dirty" || name=="large")) return 2;
        if(ScriptRuntime::pool().idle.size()!=(ok?1u:0u)) return 7;
        if(ok && ScriptRuntime::pool().idle.front()->bytes>512*1024) return 8;
        if(name=="cycle" && error.find("cyclic")==std::string::npos) return 5;
        if(name=="spin" && error.find("instruction limit")==std::string::npos) return 3;
    }
    source="return {\ninvalid ?\n}";
    ScriptRuntime::Module bad;
    bad.file="bad.lua";
    ScriptRuntime::Call invalid{&bad,&source,nullptr,nullptr,nullptr};
    if(ScriptRuntime::run(invalid,error) || error.find("bad.lua:2")==std::string::npos) return 4;
    Nested state;
    std::string outer="outer";
    ScriptRuntime::Call reentry{&module,nullptr,&outer,invokeNested,&state};
    if(!ScriptRuntime::run(reentry,error) || !state.verified || !ScriptRuntime::pool().idle.empty()) return 9;
    // Identical path, newly compiled body; cycle through more modules than idle slots.
    std::vector<ScriptRuntime::Module> versions(12);
    std::string fn="check";
    for(size_t i=0;i<versions.size();++i) {
        auto& version=versions[i];version.file=module.file;
        fn="check"+std::to_string(i);
        source="return {"+fn+"=function(expect) assert(expect("+std::to_string(i)+")=="+std::to_string(i)+") end}";
        ScriptRuntime::Call build{&version,&source,nullptr,nullptr,nullptr};
        ScriptRuntime::Call run{&version,nullptr,&fn,invoke,nullptr};
        if(!ScriptRuntime::run(build,error) || !ScriptRuntime::run(run,error)) return 10;
    }
    if(ScriptRuntime::pool().idle.size()!=ScriptRuntime::Pool::idleLimit) return 11;
    source="return {newfn=function() assert(_G.marker==nil) end}";
    if(!ScriptRuntime::run(compile,error)) return 12;
    fn="check";
    ScriptRuntime::Call old{&module,nullptr,&fn,invoke,nullptr};
    if(ScriptRuntime::run(old,error) || error.find("missing callback check")==std::string::npos) return 13;
    fn="newfn";
    if(!ScriptRuntime::run(old,error)) return 14;
    ScriptRuntime::clearPool();
    std::puts("PASS pooled callbacks, isolated globals and libraries, nested calls, reload, eviction and resource budgets");
}
