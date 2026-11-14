#include "../../src/Starfall.cpp"
#include <cstdio>

int parseArguments(lua_State* L) {
    ScriptFields fields{{"action","givecredits"},{"action.target","self"}};
    std::optional<DWORD> target;std::optional<CellStruct> point;
    bool fromSection=lua_toboolean(L,2)!=0;
    ScriptActions::arguments(L,1,fields,target,point,fromSection);
    lua_pushstring(L,fields.contains("action.amount")?fields.at("action.amount").c_str():fields.at("action.target").c_str());
    return 1;
}
int main() {
    Skill step{};step.id="review";step.trigger="onsuperweapon";
    ScriptFields valid{{"action","givecredits"},{"action.amount","$(100+50)"},{"action.target","self"}};
    auto typo=valid;typo["action.amunt"]="999";
    if(!readAction(step,"review",&valid) || readAction(step,"review",&typo)) return 5;
    int checked=0;
    for(int state=0;state<128;++state) {
        auto L=luaL_newstate();if(!L)return 1;
        for(auto expression:{"{Amount=1,amount=2}","{Amount=1,['Action.Amount']=2}",
            "{Offset='1,2',['Action.ForceMove.Offset']='3,4'}","{Duration=1,['Action.ForceMove.Duration']=2}",
            "{Target='Self',target='Victim'}"}) {
            lua_pushcfunction(L,parseArguments);
            string code="return "+string(expression);
            if(luaL_loadstring(L,code.c_str()) || lua_pcall(L,0,1,0)) return 2;
            lua_pushboolean(L,0);
            if(lua_pcall(L,2,1,0)==LUA_OK || !strstr(lua_tostring(L,-1),"duplicate action field")) return 3;
            lua_pop(L,1);++checked;
        }
        for(bool fromSection:{false,true}) {
            lua_pushcfunction(L,parseArguments);
            lua_newtable(L);
            lua_pushstring(L,fromSection?"Victim":"7");lua_setfield(L,-2,fromSection?"Target":"Amount");
            lua_pushboolean(L,fromSection);
            if(lua_pcall(L,2,1,0)!=LUA_OK || string(lua_tostring(L,-1))!=(fromSection?"Victim":"7")) return 4;
            lua_pop(L,1);++checked;
        }
        lua_close(L);
    }
    std::printf("PASS %d action argument checks across 128 Lua states\n",checked);
    return 0;
}
