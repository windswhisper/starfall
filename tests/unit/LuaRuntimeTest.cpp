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
            end,
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
    for(std::string name:{"check","values","cycle","oversized","closure","spin","allocate","check"}) {
        ScriptRuntime::Call call{&module,nullptr,&name,invoke,nullptr};
        bool ok=ScriptRuntime::run(call,error);
        if(ok!=(name=="check" || name=="values") || call.bytes!=0) return 2;
        if(name=="cycle" && error.find("cyclic")==std::string::npos) return 5;
        if(name=="spin" && error.find("instruction limit")==std::string::npos) return 3;
    }
    source="return {\ninvalid ?\n}";
    ScriptRuntime::Module bad;
    bad.file="bad.lua";
    ScriptRuntime::Call invalid{&bad,&source,nullptr,nullptr,nullptr};
    if(ScriptRuntime::run(invalid,error) || error.find("bad.lua:2")==std::string::npos) return 4;
    std::puts("PASS isolated callbacks, data copies and limits, memory and instruction limits, syntax location");
}
