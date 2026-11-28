constexpr wchar_t orderStreamName[]=L"Starfall.UnitOrders";
bool saveUnitOrders(IStorage* storage) {
    Writer w;
    w.word(2);w.word(static_cast<DWORD>(UnitOrders::orders.size()));
    for(const auto& [id,o]:UnitOrders::orders) {
        w.word(id);w.str(o.id);w.word(o.owner);w.word(o.center.X);w.word(o.center.Y);w.word(o.radius);
        w.word(o.target?o.target->UniqueID:0);w.word(o.nextScan);w.word(o.fired);w.word(o.missile);
        w.word(o.lastCell.X);w.word(o.lastCell.Y);w.word(o.lastMoved);
    }
    IStream* stream=nullptr;
    if(FAILED(storage->CreateStream(orderStreamName,STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,0,&stream))) return false;
    ULONG written=0;
    auto size=static_cast<ULONG>(w.bytes.size());
    bool ok=SUCCEEDED(stream->Write(w.bytes.data(),size,&written)) && written==size && SUCCEEDED(stream->Commit(STGC_DEFAULT));
    stream->Release();return ok;
}
bool loadUnitOrders(IStorage* storage) {
    IStream* stream=nullptr;
    HRESULT hr=storage->OpenStream(orderStreamName,nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&stream);
    if(hr==STG_E_FILENOTFOUND) {UnitOrders::orders.clear();return true;}
    if(FAILED(hr)) return false;
    STATSTG stat{};ULONG got=0;
    bool ok=SUCCEEDED(stream->Stat(&stat,STATFLAG_NONAME)) && stat.cbSize.QuadPart<=16*1024*1024;
    vector<BYTE> bytes(ok?stat.cbSize.LowPart:0);
    ok=ok && SUCCEEDED(stream->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&got)) && got==bytes.size();
    stream->Release();
    Reader r{bytes};
    const auto version=r.word();
    if(!ok || (version!=1 && version!=2)) return false;
    auto count=r.word();
    Objects objects;
    if(count) for(auto unit:TechnoClass::Array) if(alive(unit)) objects.emplace(unit->UniqueID,unit);
    decltype(UnitOrders::orders) restored;
    for(;count && r.ok;--count) {
        DWORD id=r.word();
        UnitOrders::Order o;
        o.id=r.str();o.owner=static_cast<int>(r.word());
        auto x=r.word(),y=r.word();o.radius=static_cast<int>(r.word());
        auto target=r.word();o.nextScan=r.word();auto fired=r.word(),missile=r.word();
        if(version>=2) {
            auto lastX=r.word(),lastY=r.word();o.lastMoved=r.word();
            if(lastX>=512 || lastY>=512) return false;
            o.lastCell={static_cast<short>(lastX),static_cast<short>(lastY)};
        }
        if(!r.ok || !id || o.id.empty() || x>=512 || y>=512 || o.radius<1 || o.radius>128 || fired>1 || missile>1) return false;
        o.center={static_cast<short>(x),static_cast<short>(y)};
        o.unit=findLive(objects,id);o.target=findLive(objects,target);o.fired=fired && o.target;o.missile=missile!=0;
        if(o.unit && !restored.emplace(id,std::move(o)).second) return false;
    }
    if(!r.ok || r.pos!=bytes.size()) return false;
    UnitOrders::orders.swap(restored);return true;
}
