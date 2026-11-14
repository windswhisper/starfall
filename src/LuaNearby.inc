// Native ground and bridge chains track most objects. Keep IDs (never pointers)
// for units not certified to be in the cell containing their actual center.
namespace LuaNearby {
bool initialized=false;
std::unordered_set<DWORD> dirty,offGrid;
unsigned lastQuery=0;
void reset() {initialized=false;dirty.clear();offGrid.clear();}
void forget(DWORD id) {dirty.erase(id);offGrid.erase(id);}
void moved(TechnoClass* unit) {if(initialized) dirty.insert(unit->UniqueID);}
void expire() {
    if(initialized && static_cast<unsigned>(Unsorted::CurrentFrame)-lastQuery>60) reset();
}
bool inCenterCell(TechnoClass* unit) {
    if(!unit->IsOnMap || unit->InLimbo) return false;
    auto pos=unit->GetCenterCoords();
    if(pos.X<0 || pos.Y<0 || pos.X>=512*256 || pos.Y>=512*256) return false;
    auto cell=MapClass::Instance.TryGetCellAt(pos);
    if(cell) for(auto head:{cell->FirstObject,cell->AltObject}) {
        // Long chains are cheaper to treat as unverified until the next move.
        unsigned remaining=64;
        for(auto obj=head;obj && remaining--;obj=obj->NextObject) if(obj==unit) return true;
    }
    return false;
}
struct Bounds {int left,top,right,bottom;};
Bounds bounds(CoordStruct pos,double radius) {
    auto cell=[](double v){return static_cast<int>(std::floor(v/256.0));};
    double reach=radius*256.0;
    return {std::max(0,cell(pos.X-reach)),std::max(0,cell(pos.Y-reach)),
            std::min(511,cell(pos.X+reach)),std::min(511,cell(pos.Y+reach))};
}
template<class Visit> void visit(const Objects& objects,CoordStruct pos,double radius,Visit&& accept) {
    auto box=bounds(pos,radius);
    auto area=std::max(0,box.right-box.left+1)*std::max(0,box.bottom-box.top+1);
    auto scan=[&] {for(auto [id,u]:objects) accept(id,u);};
    if(static_cast<size_t>(area)>=objects.size()) {
        scan();return;
    }
    lastQuery=Unsorted::CurrentFrame;
    if(!initialized) {
        for(auto u:TechnoClass::Array) if(u && !inCenterCell(u)) offGrid.insert(u->UniqueID);
        initialized=true;
    }
    for(auto id:dirty) {
        auto it=objects.find(id);
        if(it!=objects.end() && inCenterCell(it->second)) offGrid.erase(id);
        else offGrid.insert(id);
    }
    dirty.clear();
    if(area+offGrid.size()>=objects.size()) {scan();return;}
    vector<DWORD> ids(offGrid.begin(),offGrid.end());
    size_t remaining=objects.size();
    for(int y=box.top;y<=box.bottom;++y) for(int x=box.left;x<=box.right;++x) {
        auto cell=MapClass::Instance.TryGetCellAt(CellStruct{static_cast<short>(x),static_cast<short>(y)});
        if(!cell) continue;
        for(auto head:{cell->FirstObject,cell->AltObject}) for(auto obj=head;obj;obj=obj->NextObject) {
            if(!remaining) {scan();return;}
            --remaining;
            if((obj->AbstractFlags & AbstractFlags::Techno)!=AbstractFlags::None) ids.push_back(obj->UniqueID);
        }
    }
    std::sort(ids.begin(),ids.end());
    ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    for(auto id:ids) {
        auto it=objects.find(id);
        if(it!=objects.end()) accept(id,it->second);
    }
}
}
