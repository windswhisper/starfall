#include "../../../src/Starfall.cpp"
#include <MessageListClass.h>
#include <Drawing.h>
#include "ResearchAudio.h"
#include "../WindowMouse.h"
EXPORT_FUNC(SF_ResearchCommand);
EXPORT_FUNC(SF_ResearchDraw);
namespace {
DWORD actualId=0;
void check(bool ok,const char* label) {
    std::ofstream("building-reward.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
void click(Point2D point) {
    check(WindowMouse::button(point,1) && WindowMouse::button(point,4),"research button captures mouse");
}
Point2D tabCenter() {
    const auto& view=DSurface::ViewBounds;
    return {view.X+view.Width-32,view.Y+48};
}
void checkBadge() {
    auto at=tabCenter();
    Point2D edge{at.X+22,at.Y-24};
    SF_ResearchDraw(nullptr);
    check(DSurface::Composite->GetPixel(&edge)==Drawing::RGB_To_Int(100,216,255),"free count badge on research tab");
}
bool freeFocusVisible() {
    auto center=tabCenter();
    const auto& view=DSurface::ViewBounds;
    RectangleStruct area{center.X-view.Width/2,center.Y+view.Height/20,view.Width/4,view.Height/3};
    auto surface=DSurface::Composite;
    surface->FillRect(&area,0);
    SF_ResearchDraw(nullptr);
    const int pitch=surface->GetPitch();
    auto base=static_cast<const BYTE*>(surface->Lock(area.X,area.Y));
    check(base!=nullptr,"free notification sample readable");
    bool lit=false;
    for(int y=0;y<area.Height && !lit;++y) {
        auto row=reinterpret_cast<const WORD*>(base+y*pitch);
        lit=std::any_of(row,row+area.Width,[](WORD pixel){return pixel!=0;});
    }
    surface->Unlock();
    return lit;
}
void checkSwitching(int house) {
    const auto rewards=Research::pendingBuildingRewards(house);
    Research::award(house,Research::price);
    click(tabCenter());
    const auto& view=DSurface::ViewBounds;
    for(auto id:rewards) {
        check(Research::expanded && Research::rewardPanel==id,"free tab cycles through independent building pools");
        int count=static_cast<int>(Research::buildingOptions(id,house).size());
        int width=std::min({244,(view.Width-24-(count-1)*(view.Width>=720?16:8))/count,(view.Height-94)*2/3});
        int panelHeight=38+width*3/2+34;
        click({view.X+view.Width/2,view.Y+(view.Height-panelHeight)/2+panelHeight-14});
    }
    check(Research::expanded && !Research::rewardPanel && Research::balance(house)==Research::price,
        "ordinary research accessible while free rewards are pending");
    click(tabCenter());
    Research::points[house]=0;
}
BuildingClass* place(const char* id,HouseClass* house,CellStruct site) {
    auto type=BuildingTypeClass::Find(id);
    auto b=type?static_cast<BuildingClass*>(type->CreateObject(house)):nullptr;
    bool placed=false;
    for(short y=site.Y;b && !placed && y<site.Y+8;++y) for(short x=site.X;!placed && x<site.X+8;++x) {
        CellStruct c{x,y};auto cell=MapClass::Instance.TryGetCellAt(c);
        if(cell && MapClass::Instance.IsWithinUsableArea(c,false)) placed=b->Unlimbo(cell->GetCoordsWithBridge(),DirType::North);
    }
    check(placed,"placed building");b->QueueMission(Mission::Guard,false);return b;
}
InfantryClass* infantry(const char* id,HouseClass* house) {
    auto type=InfantryTypeClass::Find(id);
    auto u=type?static_cast<InfantryClass*>(type->CreateObject(house)):nullptr;
    check(u!=nullptr,"infantry");return u;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestBuildingReward,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    int frame=Unsorted::CurrentFrame;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    if(!loading && frame==20) {
        Research::ensureReady();check(house && foe,"houses");
        auto closed=place("GAPILE",foe,{48,65});
        auto engBuilding=place("NAPOWR",foe,{57,65});
        auto spyBuilding=place("NAHAND",foe,{65,65});
        auto spyTransfer=place("NAHAND",foe,{73,65});
        auto claimed=place("NAPOWR",foe,{81,65});
        auto real=place("GAPOWR",foe,{89,65});
        check(!BuildingResearch::rewardRule(closed->Type).engineer && !BuildingResearch::rewardRule(closed->Type).spy &&
              BuildingResearch::rewardRule(engBuilding->Type).engineer && !BuildingResearch::rewardRule(engBuilding->Type).spy &&
              !BuildingResearch::rewardRule(spyBuilding->Type).engineer && BuildingResearch::rewardRule(spyBuilding->Type).spy &&
              BuildingResearch::rewardRule(real->Type).engineer && BuildingResearch::rewardRule(real->Type).spy,"configuration");
        auto engineer=infantry("ENGINEER",house);auto spy=infantry("E1",house);
        BuildingResearch::tickBuildings();
        check(!Research::buildingRewards.contains(closed->UniqueID) && !Research::buildingRewards.contains(real->UniqueID),
              "ordinary placement does not allocate free rewards");
        closed->CurrentMission=Mission::Guard;
        closed->GoOnline();
        BuildingResearch::tickBuildings();
        auto producerId=closed->UniqueID;
        check(Research::producers.contains(producerId),"newly placed producer indexed");
        auto& timer=Research::producers.at(producerId);
        timer.elapsed=49999;
        const int balance=Research::balance(foe->ArrayIndex);
        closed->EMPLockRemaining=2;
        BuildingResearch::tickBuildings();
        check(timer.elapsed==49999 && Research::balance(foe->ArrayIndex)==balance,"EMP pauses producer");
        closed->EMPLockRemaining=0;
        closed->GoOffline();
        BuildingResearch::tickBuildings();
        check(timer.elapsed==49999 && Research::balance(foe->ArrayIndex)==balance,"offline producer keeps elapsed frames");
        closed->GoOnline();
        BuildingResearch::tickBuildings();
        check(timer.elapsed==0 && Research::balance(foe->ArrayIndex)==balance+7,"online producer resumes and pays once");
        timer.elapsed=250;
        check(closed->SetOwningHouse(house,false),"transfer closed");BuildingResearch::tickBuildings();
        check(!Research::producers.contains(producerId) || Research::producers.at(producerId).elapsed<=1,"capture resets producer progress");
        closed->CurrentMission=Mission::Guard;
        closed->GoOnline();
        BuildingResearch::tickBuildings();
        Research::producers.at(producerId).elapsed=900;
        std::ofstream("producer.id")<<producerId;
        BuildingResearch::authorizeBuildingReward(engineer,closed,Research::BuildingRewardSource::Engineer);
        BuildingResearch::authorizeBuildingReward(spy,closed,Research::BuildingRewardSource::Spy);
        check(Research::buildingOptions(closed->UniqueID,house->ArrayIndex).empty(),"defaults closed");
        BuildingResearch::authorizeBuildingReward(spy,engBuilding,Research::BuildingRewardSource::Spy);
        check(Research::buildingOptions(engBuilding->UniqueID,house->ArrayIndex).empty(),"engineer only excludes spy");
        BuildingResearch::authorizeBuildingReward(spy,spyBuilding,Research::BuildingRewardSource::Spy);
        check(!Research::buildingOptions(spyBuilding->UniqueID,house->ArrayIndex).empty(),"spy enabled");
        auto board=Research::buildingOptions(spyBuilding->UniqueID,house->ArrayIndex);
        check(board.size()==3 && std::set<int>(board.begin(),board.end()).size()==3 &&
            std::all_of(board.begin(),board.end(),[](int i){return Research::options[i].rarity==Research::Rarity::Rare;}),
            "large pool offers three distinct positive-weight cards");
        int outside=3;while(std::find(board.begin(),board.end(),outside)!=board.end()) ++outside;
        check(!Research::claimBuilding(spyBuilding->UniqueID,house->ArrayIndex,outside),"unoffered configured project rejected");
        checkBadge();
        check(ResearchAudio::sampleCache().contains("builtin:available"),"free authorization plays the available cue at zero points");
        Sleep(160);
        check(freeFocusVisible(),"new free entitlement animates the research button");
        Sleep(620);
        BuildingResearch::authorizeBuildingReward(spy,spyBuilding,Research::BuildingRewardSource::Spy);
        SF_ResearchDraw(nullptr);Sleep(160);
        check(!freeFocusVisible(),"repeated authorization does not replay the animation");
        click(tabCenter());
        check(Research::balance(house->ArrayIndex)==0 && Research::expanded && Research::rewardPanel==spyBuilding->UniqueID,
            "zero-point tab opens free research");
        click(tabCenter());click(tabCenter());
        check(Research::buildingOptions(Research::rewardPanel,house->ArrayIndex)==board,"reopening preserves weighted board");
        click(tabCenter());
        check(spyTransfer->SetOwningHouse(house,false),"spy building transfer");BuildingResearch::tickBuildings();
        BuildingResearch::authorizeBuildingReward(engineer,spyTransfer,Research::BuildingRewardSource::Engineer);
        check(Research::buildingOptions(spyTransfer->UniqueID,house->ArrayIndex).empty(),"independent flags");
        check(spyTransfer->SetOwningHouse(foe,false),"transfer back");BuildingResearch::tickBuildings();
        BuildingResearch::authorizeBuildingReward(spy,spyTransfer,Research::BuildingRewardSource::Spy);
        check(!Research::buildingOptions(spyTransfer->UniqueID,house->ArrayIndex).empty(),"spy permission");
        check(spyTransfer->SetOwningHouse(house,false),"spy owner change");BuildingResearch::tickBuildings();
        check(Research::buildingOptions(spyTransfer->UniqueID,house->ArrayIndex).empty(),"spy privilege revoked on transfer");
        check(engBuilding->SetOwningHouse(house,false),"engineer building transfer");BuildingResearch::tickBuildings();
        BuildingResearch::authorizeBuildingReward(engineer,engBuilding,Research::BuildingRewardSource::Engineer);
        check(!Research::buildingOptions(engBuilding->UniqueID,house->ArrayIndex).empty(),"engineer privilege");
        check(claimed->SetOwningHouse(house,false),"claim building transfer");BuildingResearch::tickBuildings();
        BuildingResearch::authorizeBuildingReward(engineer,claimed,Research::BuildingRewardSource::Engineer);
        auto choices=Research::buildingOptions(claimed->UniqueID,house->ArrayIndex);
        check(!choices.empty(),"free research offers");
        check(choices==vector<int>{0,1,2} && Research::pendingBuildingRewards(house->ArrayIndex).size()==3,
            "three building rewards counted and fixed lists retain order");
        checkSwitching(house->ArrayIndex);
        const int option=choices.front();
        const auto id=claimed->UniqueID;
        EventClass selection(house->ArrayIndex,EventType::SpecialPlace,-0x100000-option,
            CellStruct{static_cast<short>(id),static_cast<short>(id>>16)});
        REGISTERS regs{};regs.ESI(&selection);
        SF_ResearchCommand(&regs);SF_ResearchCommand(&regs);
        check(Research::owns(house->ArrayIndex,Research::options[option].id),"claim reward");
        check(Research::pendingBuildingRewards(house->ArrayIndex).size()==2 && Research::balance(house->ArrayIndex)==0,
            "one claim consumes one free reward at zero points");
        check(Research::completedRounds.empty(),"free research does not advance ordinary rounds");
        const auto notice=std::wstring(house->UIName)+L" 已研究「"+Research::options[option].name+L"」。";
        int messages=0;
        for(const auto& text:MessageListClass::Instance.MessageBuffers) if(notice==text) ++messages;
        check(messages==1,"free research announced once");
        check(claimed->SetOwningHouse(foe,false),"claimed transfer");BuildingResearch::tickBuildings();
        BuildingResearch::authorizeBuildingReward(spy,claimed,Research::BuildingRewardSource::Spy);
        check(Research::buildingOptions(claimed->UniqueID,house->ArrayIndex).empty() && Research::buildingRewards.at(claimed->UniqueID).claimed,"claimed once");
        real->Type->Capturable=true;
        auto realEngineer=infantry("ENGINEER",house);
        auto at=real->GetMapCoords();bool placed=false;
        for(short dy=-5;dy<=5 && !placed;++dy) for(short dx=-5;dx<=5 && !placed;++dx) {
            if(std::abs(dx)+std::abs(dy)<3) continue;
            CellStruct c{static_cast<short>(at.X+dx),static_cast<short>(at.Y+dy)};auto cell=MapClass::Instance.TryGetCellAt(c);
            if(cell && MapClass::Instance.IsWithinUsableArea(c,false)) placed=realEngineer->Unlimbo(cell->GetCoordsWithBridge(),DirType::North);
        }
        check(placed,"placed real engineer");
        realEngineer->SetDestination(real,true);realEngineer->SetTarget(real);realEngineer->QueueMission(Mission::Capture,true);
        actualId=real->UniqueID;
        std::ofstream("building-reward.ids")<<engBuilding->UniqueID<<" "<<spyBuilding->UniqueID<<" "<<claimed->UniqueID<<" "<<actualId;
    } else if(!loading && frame>20 && actualId) {
        BuildingClass* real=nullptr;for(auto b:BuildingClass::Array) if(b && b->UniqueID==actualId) real=b;
        if(real && real->Owner==house && Research::buildingRewards.at(actualId).source==Research::BuildingRewardSource::Engineer) {
            check(!Research::buildingOptions(actualId,house->ArrayIndex).empty(),"native engineer capture");
            std::ifstream ids("building-reward.ids");DWORD eng,spy;ids>>eng>>spy;
            std::ofstream expected("building-reward.board");
            for(int index:Research::buildingOptions(spy,house->ArrayIndex)) expected<<Research::options[index].id<<'\n';
            expected.close();
            DWORD producer=0;std::ifstream("producer.id")>>producer;
            std::ofstream("producer.elapsed")<<Research::producers.at(producer).elapsed;
            check(ScenarioClass::SaveGame("Saved Games\\BuildingReward.sav",L"Building Reward"),"saved");
            TerminateProcess(GetCurrentProcess(),0);
        }
        if(frame>=620){check(false,"native engineer capture");}
    } else if(loading && frame>=35) {
        // The first resumed simulation tick can precede the local HUD/viewport setup.
        static const int resumed=frame;
        if(frame<resumed+5) return 0;
        static int sent=-1;
        static std::string picked;
        if(sent>=0) {
            if(frame<sent+15) return 0;
            check(Research::owns(house->ArrayIndex,picked) && Research::pendingBuildingRewards(house->ArrayIndex).size()==2 &&
                Research::balance(house->ArrayIndex)==0,"card click grants one free research through the command queue");
            check(ResearchAudio::sampleCache().contains("builtin:select"),"free selection plays the confirmation cue");
            Sleep(720);SF_ResearchDraw(nullptr);
            check(!Research::expanded,"free selection animation completes");
            TerminateProcess(GetCurrentProcess(),0);return 0;
        }
        DWORD producer=0;int elapsed=0;
        std::ifstream("producer.id")>>producer;std::ifstream("producer.elapsed")>>elapsed;
        check(elapsed>=900 && Research::producers.contains(producer) && Research::producers.at(producer).elapsed>elapsed,
              "restored producer resumes saved progress");
        DWORD eng=0,spy=0,claimed=0,real=0;std::ifstream("building-reward.ids")>>eng>>spy>>claimed>>real;
        check(Research::buildingRewards.contains(eng) && Research::buildingRewards.at(eng).source==Research::BuildingRewardSource::Engineer &&
              Research::buildingRewards.contains(spy) && Research::buildingRewards.at(spy).source==Research::BuildingRewardSource::Spy &&
              !Research::buildingOptions(eng,house->ArrayIndex).empty() && !Research::buildingOptions(spy,house->ArrayIndex).empty(),"saved permissions");
        check(Research::buildingRewards.contains(claimed) && Research::buildingRewards.at(claimed).claimed,"saved claimed");
        check(Research::completedRounds.empty(),"free research round zero survives load");
        check(Research::buildingOptions(claimed,house->ArrayIndex).empty(),"loaded no duplicate");
        vector<string> restored,expected;string option;
        for(int i:Research::buildingOptions(spy,house->ArrayIndex)) restored.push_back(Research::options[i].id);
        std::ifstream saved("building-reward.board");while(saved>>option) expected.push_back(option);
        check(restored.size()==3 && restored==expected && Research::pendingBuildingRewards(house->ArrayIndex).size()==3,
            "weighted board and free count survive load");
        check(HouseClass::CurrentPlayer==house && !house->Defeated && !house->IsObserver() &&
            !ScenarioClass::Instance->UserInputLocked,"restored player HUD is interactive");
        checkBadge();
        check(!ResearchAudio::sampleCache().contains("builtin:available"),"restored free rewards do not replay the cue");
        click(tabCenter());
        auto cards=Research::buildingOptions(Research::rewardPanel,house->ArrayIndex);
        check(!cards.empty(),"free panel accessible after load");
        const int slot=static_cast<int>(cards.size())/2;
        picked=Research::options[cards[slot]].id;
        const auto& view=DSurface::ViewBounds;
        int count=static_cast<int>(cards.size()),gap=view.Width>=720?16:8;
        int width=std::min({244,(view.Width-24-(count-1)*gap)/count,(view.Height-94)*2/3});
        int left=view.X+(view.Width-count*width-(count-1)*gap)/2;
        click({left+slot*(width+gap)+width/2,view.Y+view.Height/2});
        sent=frame;
    }
    return 0;
}
