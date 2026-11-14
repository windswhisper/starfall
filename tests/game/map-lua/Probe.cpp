#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* name) {std::ofstream("map-lua.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<name<<"\n";if(!ok) TerminateProcess(GetCurrentProcess(),2);}
}
DEFINE_HOOK(0x55B4EB,SF_TestMapLua,5) {
 if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
 bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
 auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);int f=Unsorted::CurrentFrame;
 if(!loading && f==20) {
  Research::ensureReady();int credits=house->Available_Money();
  check(Research::balance(house->ArrayIndex)>=97,"native map trigger and map configuration");
  TActionClass action; action.ActionKind=static_cast<TriggerAction>(9602);action.Param3=1;action.Param4=0;
  CellStruct missing{-1,-1}; action.Execute(house,nullptr,nullptr,missing);
  check(ActionSequence::timers.queue.size()==2,"map script dispatch");
  TechnoClass* object=nullptr;
  for(auto unit:TechnoClass::Array) if(alive(unit) && unit->IsOnMap && !unit->InLimbo) {object=unit;break;}
  check(object!=nullptr,"object fixture");
  action.Param3=3;auto point=object->GetMapCoords();action.Execute(house,object,nullptr,point);
  check(ActionSequence::timers.queue.size()==3,"object and position dispatch");
  std::ofstream("map-lua.id")<<credits;
  check(ScenarioClass::SaveGame("Saved Games\\MapLua.sav",L"Map Lua"),"saved pending");
 }
 if(f>=55) {
  int credits=0;std::ifstream("map-lua.id")>>credits;
  check(house->Available_Money()==credits+123 && Research::balance(house->ArrayIndex)>=108 && ActionSequence::timers.queue.empty(),"scheduled execution");
  TerminateProcess(GetCurrentProcess(),0);
 }
 return 0;
}
