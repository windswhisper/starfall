#include "../../../src/Starfall.cpp"
#include <EventClass.h>

namespace {
bool licensedCameoOffered=false;
void check(bool ok,const char* label) {
    std::ofstream("prices.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
#include "ResearchSequence.inc"
}
// Observe normal sidebar additions without inserting a cameo from the probe.
DEFINE_HOOK(0x4457E7,SF_TestResearchLicenseCameo,8) {
    auto item=InfantryTypeClass::Find("CLEG");
    if(item && R->ESI<int>()==item->ArrayIndex) licensedCameoOffered=true;
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestResearchCost,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    if(frame!=20 && frame!=24 && frame!=32 && frame!=46 && frame!=61) return 0;
    Research::ensureReady();
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    auto other=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    const char* ids[]={"MTNK","HTNK","ORCA","E1","GAPOWR","GAPILL"};
    TechnoTypeClass* types[6]{};
    int base[6]{},opponent[6]{};
    for(int i=0;i<6;++i) {
        types[i]=TechnoTypeClass::Find(ids[i]);
        check(types[i]!=nullptr,"valid fixture type");
    }
    int h=house->ArrayIndex;
    auto licensed=InfantryTypeClass::Find("CLEG");
    check(licensed && !Research::available(7),"registered technology ID required");
    if(frame==20 && GetFileAttributesA("reload.flag")==INVALID_FILE_ATTRIBUTES) {
        const auto logText=[] {
            std::ifstream input("Starfall.log");
            return string(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
        };
        auto ini=CCINIClass::INI_Rules;
        check(!Research::sharedRaritySequence,"shared rarity disabled by default");
        check(ini->WriteString("Starfall.Research","SharedRaritySequence","yes"),"shared rarity merged rules fixture");
        Research::readConfiguration(configPath);
        check(Research::sharedRaritySequence,"shared rarity reads merged rules");
        WritePrivateProfileStringA("Starfall.Research","SharedRaritySequence","no",configPath);
        Research::readConfiguration(configPath);
        check(!Research::sharedRaritySequence,"explicit shared rarity switch overrides rules");
        WritePrivateProfileStringA("Starfall.Research","SharedRaritySequence","invalid",configPath);
        Research::readConfiguration(configPath);
        check(!Research::sharedRaritySequence && logText().find("SharedRaritySequence=invalid")!=string::npos,"invalid shared rarity switch diagnosed");
        WritePrivateProfileStringA("Starfall.Research","SharedRaritySequence",nullptr,configPath);
        Research::readConfiguration(configPath);
        check(Research::sharedRaritySequence,"shared rarity returns to merged rules");
        // Runtime edits to INI_Rules are not retained by a fresh load process.
        // Persist the match setting after testing merged/file precedence.
        WritePrivateProfileStringA("Starfall.Research","SharedRaritySequence","yes",configPath);
        check(ini->WriteString("Starfall.ResearchTypes","12","MergedDiagnostic") &&
            ini->WriteString("MergedDiagnostic","Effect","UnlockTech") &&
            ini->WriteString("MergedDiagnostic","Technology","NoSuchRulesUnit"),"merged rules fixture");
        Research::readConfiguration(configPath);
        auto diagnostics=logText();
        for(auto expected:{"Starfall.ini:","[badsyntax] Credits 25","[badlicense] Technology=SF_MissingUnit",
            "[badbundle] summray=wrong key","[badpart] Technology= -","[badbundle] Effects=BadPart,MissingPart",
            "[brokenchain] Prerequisites=NoSuchProject","[loopa] Prerequisites=LoopB","[brokenchain] Rarity=Mythic",
            "合并规则 [mergeddiagnostic] Technology=NoSuchRulesUnit","[logtest] cooldwon=5","[logtest] Filter.TargetKind=car"})
            check(diagnostics.find(expected)!=string::npos,expected);
        for(int i=0;i<3;++i) Research::available(7);
        check(diagnostics==logText(),"option polling does not repeat diagnostics");
        std::ofstream snapshot("prices.txt");
        for(int i=0;i<6;++i) {
            base[i]=types[i]->GetActualCost(house);
            snapshot<<base[i]<<" "<<types[i]->GetActualCost(other)<<"\n";
        }
        snapshot.close();
        check(!Research::available(3) && !Research::available(4) && !Research::available(5),"reject invalid price options");
        auto factory=static_cast<FactoryClass*>(YRMemory::AllocateChecked(sizeof(FactoryClass)));
        reinterpret_cast<void(__thiscall*)(FactoryClass*)>(0x4C98B0)(factory);
        factory->Owner=house;
        check(factory->DemandProduction(types[0],house,false) && factory->Balance==base[0],"original order quote");
        // Feed a partially paid order to the real cancellation/refund routine.
        house->TakeMoney(50);
        factory->Balance-=50;
        Research::award(h,300);
        for(int i=0;i<3;++i) check(Research::purchase(h,i,house->Type->ID),"purchase");
        check(Research::balance(h)==0 && factory->Balance==base[0]-50,"locked order");
        int before=house->Available_Money();
        check(factory->AbandonProduction() && house->Available_Money()==before+50,"refund paid credits");
        check(factory->DemandProduction(types[0],house,false) && factory->Balance==types[0]->GetActualCost(house),"factory matches quoted price");
        check(factory->AbandonProduction(),"cancel discounted order");
        delete factory;
        auto barracksType=BuildingTypeClass::Find("GAPILE");
        auto barracks=static_cast<BuildingClass*>(barracksType->CreateObject(house));
        CellStruct site{50,65};auto cell=MapClass::Instance.TryGetCellAt(site);
        check(barracks && cell && barracks->Unlimbo(cell->GetCoordsWithBridge(),DirType::North),"license factory placed");
        barracks->QueueMission(Mission::Guard,false);
        check(house->CanBuild(licensed,false,false)!=CanBuildResult::Buildable,"no access before research");
        Research::award(h,100);
        check(!Research::canOfferFree(h,house->Type->ID,{"licensedupgrade"},{},-1) &&
            Research::canOfferFree(h,"TestCountry",{"licensedupgrade"},{},-1),"country qualifies before a production license");
        check(Research::purchase(h,6,house->Type->ID),"purchase production license");
        check(!Research::canApply(h,6) && !Research::Detail::hasProductionLicense(other,licensed),"license is local to purchaser");
        check(house->CanBuild(licensed,false,false)==CanBuildResult::Buildable,"licensed unit buildable");
        auto limit=licensed->BuildLimit;
        licensed->BuildLimit=0;
        check(house->CanBuild(licensed,false,false)!=CanBuildResult::Buildable,"license respects build limit");
        licensed->BuildLimit=limit;
        checkResearchRoundSave(h);
        std::ofstream("research-seed.txt")<<Research::raritySequenceSeed.value();
        check(ScenarioClass::SaveGame("Saved Games\\Prices.sav",L"Research prices"),"saved");
    }
    if(frame==24) {
        if(GetFileAttributesA("reload.flag")==INVALID_FILE_ATTRIBUTES)
            check(licensedCameoOffered,"licensed cameo offered by sidebar");
        auto order=EventClass(h,EventType::Produce,static_cast<int>(AbstractType::InfantryType),licensed->ArrayIndex,false);
        check(EventClass::OutList.Add(order),"issued normal production order");
    }
    if(frame==32) {
        unsigned savedSeed=0;std::ifstream("research-seed.txt")>>savedSeed;
        check(Research::sharedRaritySequence && Research::completedRounds.at(h)==4 &&
            Research::raritySequenceSeed==savedSeed,"ordinary research rounds survive game load");
        check(house->GetFactoryProducing(licensed)!=nullptr,"licensed unit in production queue");
        check(Research::Detail::hasProductionLicense(house,licensed) && !Research::Detail::hasProductionLicense(other,licensed) &&
            house->CanBuild(licensed,false,false)==CanBuildResult::Buildable,"loaded production license");
        check(Research::canOfferFree(h,house->Type->ID,{"licensedupgrade"},{},-1) &&
            !Research::canOfferFree(other->ArrayIndex,other->Type->ID,{"licensedupgrade"},{},-1),
            "licensed research bypasses country for license holder only");
        check(Research::canGrantRandom(h,-1,house->Type->ID,Research::Rarity::Epic) &&
            !Research::canGrantRandom(other->ArrayIndex,-1,other->Type->ID,Research::Rarity::Epic),
            "random awards honor license alternative");
        const auto& offers=Research::offers(h,house->Type->ID);
        check(std::any_of(offers.begin(),offers.end(),[](int i){return Research::options[i].id=="licensedupgrade";}),
            "licensed follow-up offered in ordinary research");
        std::ifstream snapshot("prices.txt");
        for(int i=0;i<6;++i) snapshot>>base[i]>>opponent[i];
        int percent[]={85,85,80,125,50,0};
        for(int i=0;i<6;++i) {
            int expected=(base[i]*percent[i]+50)/100-(i<=1?100:0);
            std::ofstream("prices.log",std::ios::app)<<ids[i]<<" "<<types[i]->GetActualCost(house)<<" expected "<<expected<<"\n";
            check(types[i]->GetActualCost(house)==expected,"stacked price and category");
            check(types[i]->GetActualCost(other)==opponent[i],"other player unchanged");
        }
        for(auto owner:{house,other}) for(int i=0;i<2;++i) {
            auto tank=static_cast<TechnoClass*>(types[i]->CreateObject(owner));
            check(tank!=nullptr,"create passive recipient");
            vector<string> grants;
            researchSkills(tank,&grants);
            check(includes(grants,"basemark")==(owner==house) &&
                  includes(grants,"variantmark")==(owner==house && i==1),
                  "base research applies to variant once and variant research stays specific to its owner");
            tank->UnInit();
        }
        check(Research::owns(h,"bulk") && Research::owns(h,"grizzly") && Research::owns(h,"support"),"ownership");
        Research::award(other->ArrayIndex,150);
        Research::award(h,100);
    }
    if(frame==46) {
        check(!Research::allowAIResearch && Research::balance(other->ArrayIndex)==150 &&
            Research::completedProjects(other->ArrayIndex).empty(),"AI research disabled by default");
        check(CCINIClass::INI_Rules->WriteString("Starfall.Research","AllowAIResearch","yes"),"enable AI research in merged rules");
        Research::readConfiguration(configPath);
    }
    if(frame==61) {
        check(Research::allowAIResearch && Research::balance(other->ArrayIndex)==50 &&
            Research::completedProjects(other->ArrayIndex).size()==1,"AI buys one project for 100 points");
        check(Research::balance(h)==100 && Research::completedProjects(h).size()==4,"human retains points and choices");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
