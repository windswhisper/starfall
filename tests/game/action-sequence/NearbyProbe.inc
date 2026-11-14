// Compare candidate selection to the old ordered full scan, in a real match.
void testNearby(HouseClass* owner) {
    auto type=InfantryTypeClass::Find("E1");
    InfantryClass* sample=nullptr;
    int created=0;
    for(short y=22;y<106 && created<500;++y) for(short x=22;x<106 && created<500;++x) {
        CellStruct p{x,y};
        auto& map=MapClass::Instance;
        if(!map.IsWithinUsableArea(p,false)) continue;
        auto cell=map.TryGetCellAt(p);
        if(!cell || cell->FirstObject || cell->AltObject) continue;
        auto u=static_cast<InfantryClass*>(type->CreateObject(owner));
        if(!u->Unlimbo(cell->GetCoords(),DirType::North)) {u->UnInit();continue;}
        sample=u;++created;
    }
    check(created==500,"dense query fixture");
    Objects live;
    for(auto u:TechnoClass::Array) if(alive(u)) live.emplace(u->UniqueID,u);
    auto collect=[&](CoordStruct pos,double radius,bool indexed) {
        vector<DWORD> result;
        auto accept=[&](DWORD id,TechnoClass* u) {
            if(alive(u) && u->Owner && !u->InLimbo && u->IsOnMap &&
                distanceCells(pos,u->GetCenterCoords())<=radius) result.push_back(id);
        };
        if(indexed) LuaNearby::visit(live,pos,radius,accept);
        else for(auto [id,u]:live) accept(id,u);
        return result;
    };
    for(auto [id,unit]:live) {
        if(unit->InLimbo || !unit->IsOnMap) continue;
        auto pos=unit->GetCenterCoords();
        for(double r:{0.,0.25,1.,4.5,128.})
            if(collect(pos,r,true)!=collect(pos,r,false)) check(false,"ordered spatial equivalence");
    }
    check(true,"ordered spatial equivalence");
    auto cell=sample->GetCell();
    auto pos=sample->GetCenterCoords();
    cell->RemoveContent(sample,false);
    auto detached=collect(pos,0,true);
    check(std::find(detached.begin(),detached.end(),sample->UniqueID)!=detached.end() &&
        detached==collect(pos,0,false),"detached unit in same frame");
    auto origin=sample->Location;
    auto shifted=origin;shifted.X+=512;
    sample->Location=shifted; // Leave the detached fixture out of both native cell lists.
    auto newCenter=sample->GetCenterCoords();
    check(collect(pos,0,true)==collect(pos,0,false) && collect(newCenter,0,true)==collect(newCenter,0,false),"detached move in same frame");
    sample->Location=origin;
    cell->AddContent(sample,true);
    LuaNearby::reset();
    check(collect(pos,0,true)==collect(pos,0,false) && !LuaNearby::offGrid.contains(sample->UniqueID),"upper cell chain");
    cell->RemoveContent(sample,true);
    cell->AddContent(sample,false);
    auto air=static_cast<AircraftClass*>(AircraftTypeClass::Find("ORCA")->CreateObject(owner));
    CoordStruct airPos=pos;airPos.X+=512;airPos.Z+=1024;
    check(air && air->Unlimbo(airPos,DirType::North),"aircraft placed");
    air->SetHeight(1024);
    check(air->IsInAir(),"airborne query target");
    airPos=air->GetCenterCoords();
    auto before=collect(airPos,0,true);
    check(std::find(before.begin(),before.end(),air->UniqueID)==before.end(),"original snapshot membership");
    live.emplace(air->UniqueID,air);
    auto after=collect(airPos,0,true);
    check(std::find(after.begin(),after.end(),air->UniqueID)!=after.end() && after==collect(airPos,0,false),"new aircraft in next snapshot");
    auto edge=pos;edge.X=0;edge.Y=0;
    check(collect(edge,4,true)==collect(edge,4,false),"map edge");
    auto bench=[&](bool indexed) {
        size_t hits=0;
        auto start=std::chrono::steady_clock::now();
        for(int i=0;i<2000;++i) {
            auto point=pos;
            point.X+=(i%13-6)*256;point.Y+=(i%11-5)*256;
            hits+=collect(point,4,indexed).size();
        }
        double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::ofstream("sequence.log",std::ios::app)<<"nearby "<<(indexed?"grid":"scan")<<" units "<<live.size()
            <<" queries 2000 hits "<<hits<<" ms "<<ms<<"\n";
        return hits;
    };
    auto linear=bench(false);check(linear==bench(true),"benchmark counts");
}
