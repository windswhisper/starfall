#pragma once
// Runs inside the game, where engine globals used by serialization are valid.
void checkSaveIntegrity() {
    reset();
    Research::points[0]=42;
    passiveStacks[100]["saved"]=3;
    passiveBulletOwners[200]=0;
    ActionSequence::lastCast=1;
    ActionSequence::casts[1]={"saved",0,{2,3}};
    Event signal{};
    signal.trigger="onsignal";signal.signal="saved";signal.senderKind="infantry";
    signal.senderRelation="self";signal.senderHp=10;signal.senderMaxHp=20;signal.signalDepth=1;
    signal.signalChain=std::make_shared<SignalChain>(SignalChain{9,100});
    pending.push_back(signal);
    auto snapshot=[]() {
        IStream* stream=nullptr;
        check(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)),"integrity snapshot stream");
        check(saveState(stream),"integrity snapshot write");
        STATSTG stat{};stream->Stat(&stat,STATFLAG_NONAME);
        vector<BYTE> bytes(stat.cbSize.LowPart);
        stream->Seek({},STREAM_SEEK_SET,nullptr);
        ULONG read=0;
        check(SUCCEEDED(stream->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&read)) && read==bytes.size(),"integrity snapshot read");
        stream->Release();return bytes;
    };
    auto load=[](const vector<BYTE>& bytes) {
        IStream* stream=nullptr;
        check(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)),"integrity input stream");
        stream->Write(bytes.data(),static_cast<ULONG>(bytes.size()),nullptr);
        stream->Seek({},STREAM_SEEK_SET,nullptr);
        bool ok=loadState(stream);stream->Release();return ok;
    };
    const auto saved=snapshot();
    Research::points[0]=99;
    ActionSequence::lastCast=2;
    ActionSequence::casts[2]={"live",0,{4,5}};
    nextSignalChain=77;
    const auto before=snapshot();
    auto reject=[&](const vector<BYTE>& bytes) {
        check(!load(bytes) && snapshot()==before && nextSignalChain==77,"invalid save leaves all state unchanged");
    };
    for(size_t end=0;end<saved.size();++end) reject(vector<BYTE>(saved.begin(),saved.begin()+end));
    auto corrupt=saved;
    DWORD invalid=UINT32_MAX;
    // The tail is one bullet-owner entry followed by an empty cloakable set.
    const DWORD tail[]={1,200,0,0};
    check(saved.size()>=sizeof(tail) && !memcmp(saved.data()+saved.size()-sizeof(tail),tail,sizeof(tail)),"integrity fixture tail matches schema");
    memcpy(corrupt.data()+corrupt.size()-8,&invalid,4); // Invalid owner after the valid sequence block.
    reject(corrupt);
    corrupt=saved;memcpy(corrupt.data()+corrupt.size()-4,&invalid,4);reject(corrupt); // Invalid cloakable count.
    DWORD current=0;memcpy(&current,saved.data(),sizeof(current));
    corrupt=saved;DWORD future=current+0x01000000;
    memcpy(corrupt.data(),&future,4);reject(corrupt);
    corrupt=saved;memcpy(corrupt.data()+4,&invalid,4);reject(corrupt);
    corrupt=saved;corrupt.push_back(0);DWORD size=static_cast<DWORD>(corrupt.size()-8);
    memcpy(corrupt.data()+4,&size,4);reject(corrupt);
    check(load(saved) && snapshot()==saved && nextSignalChain==10 && ActionSequence::casts.size()==1 &&
        Research::points.at(0)==42 && passiveBulletOwners.at(200)==0,"valid save commits all staged state");
}
