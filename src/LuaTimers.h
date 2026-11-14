#pragma once
#include "LuaTaskData.h"
#include <tuple>

namespace LuaTimers {
using std::string;
using LuaTaskData::Value;
struct Task {
    uint32_t owner,target;
    string skill,file,callback;
    Value data;
};
using Order=std::pair<uint32_t,uint32_t>; // absolute scenario frame, then allocation order
using StateKey=std::tuple<uint32_t,string,string>;
constexpr unsigned maxTasks=1024,maxStateKeys=2048;
struct Store {
    uint32_t lastId=0;
    bool seeded=false;
    uint64_t rng=0;
    std::map<Order,Task> queue;
    std::map<uint32_t,Order> byId;
    std::map<StateKey,Value> state;
    void insert(Order order,Task task) {
        queue.emplace(order,std::move(task));
        byId.emplace(order.second,order);
    }
    bool cancel(uint32_t id,uint32_t owner,const string& skill) {
        auto it=byId.find(id);
        if(it==byId.end()) return false;
        auto task=queue.find(it->second);
        if(task->second.owner!=owner || task->second.skill!=skill) return false;
        queue.erase(task);byId.erase(it);return true;
    }
    void forget(uint32_t owner) {
        for(auto it=state.lower_bound({owner,{},{}});it!=state.end() && std::get<0>(it->first)==owner;)
            it=state.erase(it);
        for(auto it=queue.begin();it!=queue.end();) {
            if(it->second.owner==owner) {byId.erase(it->first.second);it=queue.erase(it);}
            else ++it;
        }
    }
    // PCG32 uses fixed-width integer arithmetic. Persisting this separate stream keeps Lua
    // draws reproducible on reload, without changing the RNG sequence of native weapons.
    void ensureSeed(uint32_t seed) {
        if(!seeded) {rng=0x853c49e6748fea9bULL+uint64_t(seed)*6364136223846793005ULL;seeded=true;}
    }
    uint32_t draw(uint32_t seed) {
        ensureSeed(seed);
        auto old=rng;
        rng=old*6364136223846793005ULL+1442695040888963407ULL;
        return std::rotr(static_cast<uint32_t>(((old>>18)^old)>>27),static_cast<int>(old>>59));
    }
    int range(int low,int high,uint32_t seed) {
        const auto width=static_cast<uint32_t>(int64_t(high)-low+1);
        auto value=draw(seed);
        if(width) {
            const auto reject=(uint32_t(0)-width)%width;
            while(value<reject) value=draw(seed);
            value%=width;
        }
        return static_cast<int>(int64_t(low)+value);
    }
    template<class W> void write(W& w) const {
        w.word(lastId);w.word(seeded);w.word(static_cast<uint32_t>(rng));w.word(static_cast<uint32_t>(rng>>32));
        w.word(static_cast<uint32_t>(queue.size()));
        for(const auto& [order,t]:queue) {
            w.word(order.first);w.word(order.second);w.word(t.owner);w.word(t.target);
            w.str(t.skill);w.str(t.file);w.str(t.callback);LuaTaskData::write(w,t.data);
        }
        w.word(static_cast<uint32_t>(state.size()));
        for(const auto& [key,value]:state) {
            w.word(std::get<0>(key));w.str(std::get<1>(key));w.str(std::get<2>(key));LuaTaskData::write(w,value);
        }
    }
    template<class R> void read(R& r) {
        lastId=r.word();auto initialized=r.word();if(initialized>1) r.ok=false;
        seeded=initialized!=0;rng=r.word();rng|=uint64_t(r.word())<<32;
        auto count=r.word();if(count>maxTasks) r.ok=false;
        for(uint32_t i=0;r.ok && i<count;++i) {
            auto due=r.word(),id=r.word();
            Task t{r.word(),r.word(),r.str(),r.str(),r.str(),LuaTaskData::read(r)};
            if(!id || id>lastId || byId.contains(id) || !t.owner || t.skill.empty() || t.file.empty() || t.callback.empty()) r.ok=false;
            if(r.ok) insert({due,id},std::move(t));
        }
        count=r.word();if(count>maxStateKeys) r.ok=false;
        for(uint32_t i=0;r.ok && i<count;++i) {
            StateKey key{r.word(),r.str(),r.str()};
            auto value=LuaTaskData::read(r);
            if(!std::get<0>(key) || std::get<1>(key).empty() || std::get<2>(key).empty() ||
               value.type==LuaTaskData::Type::Nil || !state.emplace(std::move(key),std::move(value)).second) r.ok=false;
        }
    }
};
}
