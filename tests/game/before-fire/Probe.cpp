#include "../../../src/Starfall.cpp"
namespace {
DWORD testShooter=0,testPeer=0,testRange=0;
std::vector<std::pair<std::string,int>> shotsShooter,shotsPeer,shotsRange;
bool replacementRemoved=false;
bool savedDuringVolley=false;
bool checkedLoadedVolley=false;
std::vector<std::pair<std::string,int>> shotsAfterRemoval;
void check(bool ok,const char* label) {
    std::ofstream("before-fire.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
}
DEFINE_HOOK(0x6FF66C,SF_TestBeforeFireShot,6) {
    auto unit=R->ESI<TechnoClass*>();auto weapon=R->EBX<WeaponTypeClass*>();
    if(unit && weapon) {
        if(unit->UniqueID==testShooter) {
            shotsShooter.emplace_back(weapon->ID,unit->CurrentBurstIndex);
            if(replacementRemoved) shotsAfterRemoval.emplace_back(weapon->ID,unit->CurrentBurstIndex);
        }
        if(unit->UniqueID==testPeer) shotsPeer.emplace_back(weapon->ID,unit->CurrentBurstIndex);
        if(unit->UniqueID==testRange) shotsRange.emplace_back(weapon->ID,unit->CurrentBurstIndex);
    }
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestBeforeFireFrame,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    if(frame>=412) {TerminateProcess(GetCurrentProcess(),0);return 0;}
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(frame>22 && frame<140) {
        if(loading) {
            if(!checkedLoadedVolley) {
                UnitClass* holder=nullptr;
                for(auto unit:UnitClass::Array) if(volleyWeapons.contains(unit->UniqueID) && volleyWeapons[unit->UniqueID].replacement) {holder=unit;break;}
                check(holder && holder->CurrentBurstIndex>0 && volleyWeapons[holder->UniqueID].replacement==WeaponTypeClass::Find("120mm"),"volley restored");
                testShooter=holder->UniqueID;checkedLoadedVolley=true;
            }
            if(!shotsShooter.empty()) {
                check(!_stricmp(shotsShooter.front().first.c_str(),"120mm"),"volley shot after load");
                TerminateProcess(GetCurrentProcess(),0);
            }
            return 0;
        }
        if(!savedDuringVolley && !shotsShooter.empty()) {
            for(auto unit:UnitClass::Array) if(unit->UniqueID==testShooter && unit->CurrentBurstIndex>0 && volleyWeapons.contains(testShooter)) {
                check(ScenarioClass::SaveGame("Saved Games\\BeforeFire.sav",L"BeforeFire"),"saved mid-volley");
                savedDuringVolley=true;
                warheadGrants[testShooter].skills.erase("beforereplace");
                removedPassives[testShooter].insert("beforereplace");replacementRemoved=true;
                break;
            }
        }
        return 0;
    }
    if(frame!=22 && frame!=400) return 0;
    if(loading && frame==22) return 0;
    UnitClass *tank=nullptr,*peer=nullptr;
    auto player=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    for(auto unit:UnitClass::Array) if(unit->Owner==player && !_stricmp(unit->Type->ID,"MTNK")) {
        if(loading && volleyWeapons.contains(unit->UniqueID)) tank=unit;
        else if(!loading && !tank) tank=unit;
        else if(!peer) peer=unit;
    }
    check(tank && peer,"units");
    auto replacement=WeaponTypeClass::Find("120mm");
    check(replacement && skills.contains("beforereplace") && skills.contains("beforefallback") &&
        skills.contains("beforerange") && !skills.contains("badbefore"),"config");
    if(frame==22 && !loading) {
        check(replacement->Burst==3,"burst fixture");
        grantToUnit(tank,PassiveGrant{{"beforereplace","firereward"},-1});
        grantToUnit(peer,PassiveGrant{{"beforefallback"},-1});
        auto shortWeapon=WeaponTypeClass::Find("20mmRapid");check(shortWeapon!=nullptr,"short weapon");shortWeapon->Range=256;
        auto nearby=tank->GetMapCoords();nearby.X-=2;
        auto shortTank=static_cast<UnitClass*>(tank->Type->CreateObject(player));
        check(shortTank && shortTank->Unlimbo(MapClass::Instance.GetCellAt(nearby)->GetCoordsWithBridge(),DirType::North),"short-range unit");
        grantToUnit(shortTank,PassiveGrant{{"beforerange"},-1});
        auto cell=tank->GetMapCoords();cell.Y+=3;cell.X+=1;
        auto target=static_cast<UnitClass*>(tank->Type->CreateObject(HouseClass::FindByPlayerAt(HouseClass::PlayerAtB)));
        check(target && target->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge(),DirType::North),"target");
        target->Health=30000;target->Deactivate();target->DiscoveredBy(player);
        tank->IronCurtain(600,player,false);peer->IronCurtain(600,player,false);
        testShooter=tank->UniqueID;testPeer=peer->UniqueID;testRange=shortTank->UniqueID;
        for(auto unit:{tank,peer,shortTank}) {unit->SetTarget(target);unit->QueueMission(Mission::Attack,true);}
    }
    if(frame==400 && !loading) {
        {std::ofstream log("before-fire.log",std::ios::app);
        for(auto [weapon,index]:shotsShooter) log<<"tank "<<weapon<<" burst "<<index<<"\n";
        for(auto [weapon,index]:shotsPeer) log<<"peer "<<weapon<<" burst "<<index<<"\n";}
        {std::ofstream log("before-fire.log",std::ios::app);for(auto [weapon,index]:shotsAfterRemoval) log<<"after "<<weapon<<" burst "<<index<<"\n";}
        check(shotsShooter.size()>=3 && std::any_of(shotsShooter.begin(),shotsShooter.end(),[](const auto& shot){return !_stricmp(shot.first.c_str(),"120mm");}),"replacement fired");
        check(std::any_of(shotsShooter.begin(),shotsShooter.end(),[](const auto& shot){return !_stricmp(shot.first.c_str(),"120mm") && shot.second>0;}),"burst maintained");
        check(shotsAfterRemoval.size()>=2 && !_stricmp(shotsAfterRemoval[0].first.c_str(),"120mm") && !_stricmp(shotsAfterRemoval[1].first.c_str(),"120mm"),"remaining burst retained");
        auto fallback=std::find_if(shotsAfterRemoval.begin(),shotsAfterRemoval.end(),[](const auto& shot){return !_stricmp(shot.first.c_str(),"105mm");});
        check(fallback!=shotsAfterRemoval.end() && std::all_of(fallback,shotsAfterRemoval.end(),[](const auto& shot){return !_stricmp(shot.first.c_str(),"105mm");}),"next volley restores original");
        check(!shotsPeer.empty() && std::all_of(shotsPeer.begin(),shotsPeer.end(),[](const auto& shot){return !_stricmp(shot.first.c_str(),"105mm");}),"invalid target fallback");
        check(!shotsRange.empty() && std::all_of(shotsRange.begin(),shotsRange.end(),[](const auto& shot){return !_stricmp(shot.first.c_str(),"105mm");}),"range fallback");
        check(Research::balance(player->ArrayIndex)==static_cast<int>(shotsShooter.size()),"onfire still triggers");
        check(savedDuringVolley,"saved");
    }
    return 0;
}
