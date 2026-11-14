#include "../../../src/Starfall.cpp"
namespace {
DWORD ownerId=0,secondId=0,animId=0,shortId=0,secondAnim=0;
void check(bool ok,const char* label){std::ofstream("lua-anim.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";if(!ok)TerminateProcess(GetCurrentProcess(),2);}
UnitClass* place(HouseClass* house,CellStruct cell){auto type=UnitTypeClass::Find("MTNK");auto u=type?static_cast<UnitClass*>(type->CreateObject(house)):nullptr;check(u&&u->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge(),DirType::North),"placed");u->Deactivate();u->IronCurtain(300,house,false);return u;}
LuaTaskData::Value* state(DWORD id,const char* key){auto it=scriptTimers.state.find({id,"sf_luaanimtest",key});return it==scriptTimers.state.end()?nullptr:&it->second;}
bool legacyRoundtrip(DWORD owner){
    if(!LuaAnim::entries.empty() || !passiveBulletOwners.empty() || !cloakableUnits.empty())return false;IStream* stream=nullptr;
    if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)))return false;
    bool ok=saveState(stream);LARGE_INTEGER zero{};ULARGE_INTEGER pos{};DWORD header[2]{};ULONG count=0;
    // Remove the three empty sections added after unhitSaveTag:
    // Lua animations, bullet owners, and temporary cloakable unit IDs.
    if(ok)ok=SUCCEEDED(stream->Seek(zero,STREAM_SEEK_SET,&pos)) && SUCCEEDED(stream->Read(header,sizeof(header),&count)) && count==sizeof(header) && header[0]==cloakableSaveTag && header[1]>=12;
    if(ok){header[0]=unhitSaveTag;header[1]-=12;ok=SUCCEEDED(stream->Seek(zero,STREAM_SEEK_SET,&pos)) && SUCCEEDED(stream->Write(header,sizeof(header),&count)) && count==sizeof(header);}
    if(ok){ULARGE_INTEGER size{};size.QuadPart=sizeof(header)+header[1];ok=SUCCEEDED(stream->SetSize(size)) && SUCCEEDED(stream->Seek(zero,STREAM_SEEK_SET,&pos));}
    if(ok){ok=loadState(stream);auto value=state(owner,"removed");ok=ok && value && value->type==LuaTaskData::Type::True && LuaAnim::entries.empty();}
    stream->Release();return ok;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestLuaAnim,5){
 if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES)return 0;const bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;int frame=Unsorted::CurrentFrame;
 if(load&&!ownerId)std::ifstream("lua-anim.ids")>>ownerId>>secondId>>animId>>shortId>>secondAnim;
 if(!load && frame==20){Research::ensureReady();auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);check(house&&foe&&skills.contains("sf_luaanimtest"),"fixture");
  auto missing=AnimTypeClass::FindOrAllocate("SF_AnimMissing");check(missing && !missing->GetImage(),"missing native image fixture");
  auto a=place(house,{45,65}),b=place(house,{52,65}),enemy=place(foe,{58,65});ownerId=a->UniqueID;secondId=b->UniqueID;
  for(auto u:{a,b}){grantToUnit(u,PassiveGrant{{"sf_luaanimtest"},-1});capture("ondealdamage",u,enemy,u,enemy,1);}dispatch();
  auto first=state(ownerId,"anim"),shortState=state(ownerId,"short"),second=state(secondId,"anim"),started=state(ownerId,"started"),invalid=state(ownerId,"invalid");
  check(first&&shortState&&second&&started&&invalid&&started->type==LuaTaskData::Type::True&&invalid->type==LuaTaskData::Type::False,"started");animId=static_cast<DWORD>(first->integer);shortId=static_cast<DWORD>(shortState->integer);secondAnim=static_cast<DWORD>(second->integer);std::ofstream("lua-anim.ids")<<ownerId<<" "<<secondId<<" "<<animId<<" "<<shortId<<" "<<secondAnim;
  check(state(ownerId,"nilmove")&&state(ownerId,"nilmove")->type==LuaTaskData::Type::False && state(ownerId,"nilremove")&&state(ownerId,"nilremove")->type==LuaTaskData::Type::False && state(ownerId,"unknown")&&state(ownerId,"unknown")->type==LuaTaskData::Type::True && state(ownerId,"missing")&&state(ownerId,"missing")->type==LuaTaskData::Type::True,"invalid handles safe");
  check(LuaAnim::entries.contains(animId)&&LuaAnim::entries.contains(secondAnim),"separate animations");b->RegisterDestruction(nullptr);b->UnInit();
 }
 if(!load && frame==21){check(!LuaAnim::entries.contains(secondAnim),"owner cleanup");check(LuaAnim::entries.contains(shortId),"short survives one frame");}
 if(!load && frame==22)check(!LuaAnim::entries.contains(shortId),"short expires on schedule");
 if(!load && frame==24){auto it=LuaAnim::entries.find(animId);auto a=LuaAnim::find(animId);check(it!=LuaAnim::entries.end()&&a&&it->second.zOffset==512&&it->second.elapsed==4&&a->Location.X==45*256+128+512&&a->Location.Y==65*256+128+256,"midpoint");check(!LuaAnim::entries.contains(shortId),"short expiry");check(ScenarioClass::SaveGame("Saved Games\\LuaAnim.sav",L"Lua Anim"),"saved");}
 if(load && frame==25){auto it=LuaAnim::entries.find(animId);auto a=LuaAnim::find(animId);std::ofstream("lua-anim.log",std::ios::app)<<"INFO id="<<animId<<" size="<<LuaAnim::entries.size()<<" found="<<(it!=LuaAnim::entries.end())<<" anim="<<(a!=nullptr)<<" elapsed="<<(it!=LuaAnim::entries.end()?it->second.elapsed:9999)<<" x="<<(a?a->Location.X:-1)<<" y="<<(a?a->Location.Y:-1)<<" timers="<<scriptTimers.queue.size()<<"\n";check(it!=LuaAnim::entries.end()&&a&&it->second.zOffset==512&&it->second.elapsed==5&&a->Location.X==45*256+128+640&&a->Location.Y==65*256+128+320,"restored midpoint");}
 if(frame==28){auto it=LuaAnim::entries.find(animId);auto a=LuaAnim::find(animId);check(it!=LuaAnim::entries.end()&&a&&it->second.elapsed==8&&a->Location.X==49*256+128&&a->Location.Y==67*256+128,"endpoint");}
 if(frame==33){auto removed=state(ownerId,"removed"),again=state(ownerId,"removedAgain");check(!LuaAnim::entries.contains(animId)&&removed&&again&&removed->type==LuaTaskData::Type::True&&again->type==LuaTaskData::Type::False,"removed once");check(legacyRoundtrip(ownerId),"legacy save compatible");TerminateProcess(GetCurrentProcess(),0);}return 0;
}
