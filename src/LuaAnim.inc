// Native objects controlled by Lua. IDs, never pointers, are persisted.
namespace LuaAnim {
struct Entry {
    DWORD owner=0,cast=0; string skill; unsigned remaining=0;
    bool loop=false; int zOffset=0; CoordStruct start{},finish{}; unsigned duration=0,elapsed=0,lastFrame=0;
};
std::map<DWORD,Entry> entries;
constexpr unsigned maxEntries=2048,maxDuration=54000;
AnimClass* find(DWORD id) { for(auto a:AnimClass::Array) if(a && a->UniqueID==id) return a; return nullptr; }
bool allowed(const Entry& e,DWORD owner,DWORD cast,const string& skill) {return e.owner==owner && e.cast==cast && e.skill==skill;}
void remove(DWORD id) { auto it=entries.find(id);if(it==entries.end())return;entries.erase(it);if(auto a=find(id)) a->UnInit(); }
void forget(DWORD owner) {vector<DWORD> ids;for(const auto& [id,e]:entries)if(e.owner==owner && !e.cast)ids.push_back(id);for(auto id:ids)remove(id);}
void tick() {
    if(entries.empty())return;
    std::unordered_map<DWORD,AnimClass*> animations;for(auto a:AnimClass::Array)if(a)animations.emplace(a->UniqueID,a);
    std::unordered_set<DWORD> owners;for(auto u:TechnoClass::Array)if(u && alive(u))owners.insert(u->UniqueID);
    vector<DWORD> discard;vector<AnimClass*> retire;
    for(auto& [id,e]:entries) {
        auto found=animations.find(id);if(found==animations.end()){discard.push_back(id);continue;}
        auto a=found->second;if((!e.cast && !owners.contains(e.owner)) || !e.remaining){discard.push_back(id);retire.push_back(a);continue;}
        unsigned frame=static_cast<unsigned>(Unsorted::CurrentFrame);if(e.lastFrame==frame)continue;e.lastFrame=frame;
        if(!--e.remaining){discard.push_back(id);retire.push_back(a);continue;}
        if(e.loop)a->RemainingIterations=255;
        if(e.elapsed<e.duration){++e.elapsed;auto mix=[&](int x,int y){return static_cast<int>(static_cast<int64_t>(x)+(static_cast<int64_t>(y)-x)*e.elapsed/e.duration);};
            CoordStruct next{mix(e.start.X,e.finish.X),mix(e.start.Y,e.finish.Y),mix(e.start.Z,e.finish.Z)};
            if(next!=a->Location){a->Mark(MarkType::Up);a->SetLocation(next);a->Mark(MarkType::Down);}
        }
    }
    for(auto id:discard)entries.erase(id);for(auto a:retire)a->UnInit();
}
}
