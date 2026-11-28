// Continuous orders run exclusively on the synchronized simulation thread.
namespace UnitOrders {
struct Definition {
    string name;
    int interval=15;
    unsigned enterKinds=0;
    vector<string> houses,kinds;
};
struct Order {
    string id;
    TechnoClass* unit=nullptr;
    TechnoClass* target=nullptr;
    int owner=-1;
    CellStruct center{};
    int radius=0;
    unsigned nextScan=0;
    bool fired=false,missile=false;
    CellStruct lastCell{};
    unsigned lastMoved=0;
};
std::map<string,Definition> definitions;
std::map<DWORD,Order> orders;
std::map<UnitTypeClass*,bool> passengerSizeRules;

void readDefinitions() {
    passengerSizeRules.clear();
    definitions.clear();
    CommandBarOrders::entries.clear();
    const string registryName="Starfall.UnitOrders";
    for(const auto& key:sectionKeys(registryName)) {
        Definition def;
        def.name=readRaw(registryName,key.c_str());
        string id=lower(def.name);
        if(id.empty() || id.size()>127 || definitions.contains(id)) continue;
        rulesSkillNames.try_emplace(id,def.name);
        const auto type=read(id,"Type");
        if(type!="areaattack" && type!="autoenter") {invalidField(id,"Type requires AreaAttack or AutoEnter");continue;}
        string interval=read(id,"ScanInterval","15");
        auto parsed=std::from_chars(interval.data(),interval.data()+interval.size(),def.interval);
        if(parsed.ec!=std::errc{} || parsed.ptr!=interval.data()+interval.size() || def.interval<1) {
            invalidField(id,"ScanInterval requires a positive frame count");continue;
        }
        if(type=="autoenter") {
            auto kinds=split(read(id,"AutoEnter.Kinds","Transport,Garrison,TankBunker"));
            for(const auto& k:kinds) {
                if(k=="transport") def.enterKinds|=CommandBarOrders::Transport;
                else if(k=="garrison") def.enterKinds|=CommandBarOrders::Garrison;
                else if(k=="tankbunker") def.enterKinds|=CommandBarOrders::TankBunker;
                else {def.enterKinds=0;break;}
            }
            if(!def.enterKinds) {invalidField(id,"AutoEnter.Kinds requires Transport, Garrison or TankBunker");continue;}
        } else {
            if(read(id,"AreaAttack.Select","Random")!="random") {invalidField(id,"AreaAttack.Select requires Random");continue;}
            def.houses=split(read(id,"AreaAttack.Houses","Enemies"));
            def.kinds=split(read(id,"AreaAttack.Kinds","All"));
            if(def.houses.empty() || std::any_of(def.houses.begin(),def.houses.end(),[](const string& h){return h!="enemies" && h!="neutral";})) {
                invalidField(id,"AreaAttack.Houses requires Enemies or Neutral");continue;
            }
            if(def.kinds.empty() || std::any_of(def.kinds.begin(),def.kinds.end(),[](const string& k){
                return !includes({"all","building","infantry","vehicle","aircraft"},k);
            })) {invalidField(id,"AreaAttack.Kinds");continue;}
        }
        auto button=read(id,"CommandBar.Button");
        if(!button.empty()) {
            string raw=read(id,"Radius","6");
            int radius=0;
            auto parsed=std::from_chars(raw.data(),raw.data()+raw.size(),radius);
            if(parsed.ec!=std::errc{} || parsed.ptr!=raw.data()+raw.size() || radius<1 || radius>128) {
                invalidField(id,"Radius requires 1..128 cells");continue;
            }
            if(button=="x" || button.find_first_of(" ,\t")!=string::npos ||
               std::any_of(CommandBarOrders::entries.begin(),CommandBarOrders::entries.end(),
                   [&](const auto& entry){return entry.button==button;})) {
                invalidField(id,"CommandBar.Button requires a unique button name");continue;
            }
            auto image=read(id,"CommandBar.Image");
            auto rawTip=readRaw(id,"CommandBar.Tooltip");
            std::wstring tooltip=(def.enterKinds?L"自动进入 · ":L"区域锁定打击 · ")+std::to_wstring(radius)+L" 格";
            if(!_strnicmp(rawTip.c_str(),"TIP:",4)) tooltip=StringTable::TryFetchString(rawTip.c_str(),tooltip.c_str());
            else if(!rawTip.empty()) tooltip=ConfigText::displayText(rawTip);
            constexpr size_t tipLimit=std::extent_v<decltype(ToolTipManagerData::HelpText)>-1;
            if(tooltip.size()>tipLimit) {
                tooltip.resize(tipLimit);
                if(tooltip.back()>=0xD800 && tooltip.back()<=0xDBFF) tooltip.pop_back();
            }
            CommandBarOrders::entries.push_back({id,button,image.empty()?button+".shp":image,!image.empty(),radius,def.enterKinds,std::move(tooltip)});
        }
        definitions.emplace(id,std::move(def));
    }
}
bool registered(const string& id) {return definitions.contains(id);}
bool isAutoEnter(const string& id) {
    auto def=definitions.find(id);
    return def!=definitions.end() && def->second.enterKinds;
}
bool controlled(TechnoClass* unit) {
    return unit && orders.contains(unit->UniqueID);
}
bool fixedPosition(TechnoClass* unit) {
    if(!unit || orders.empty()) return false;
    auto it=orders.find(unit->UniqueID);
    if(it==orders.end()) return false;
    auto def=definitions.find(it->second.id);
    return def!=definitions.end() && !def->second.enterKinds;
}
bool enterSource(TechnoClass* unit,unsigned kinds) {
    if(!unit || unit->BunkerLinkedItem || unit->Absorbed || unit->IsImmobilized || unit->GetTechnoType()->Speed<=0) return false;
    if(auto infantry=abstract_cast<InfantryClass*>(unit))
        return (kinds&CommandBarOrders::Transport) || ((kinds&CommandBarOrders::Garrison) && infantry->Type->Occupier);
    return (kinds&CommandBarOrders::TankBunker) && abstract_cast<UnitClass*>(unit) &&
        reinterpret_cast<bool(__thiscall*)(TechnoClass*)>(0x70FB50)(unit); // YR's current tank bunker eligibility
}
void releaseApproach(const Order& o) {
    if(!o.target || !o.unit->ContainsLink(o.target) || o.unit->BunkerLinkedItem || o.unit->Transporter) return;
    auto def=definitions.find(o.id);
    if(def!=definitions.end() && def->second.enterKinds) o.unit->SendCommand(RadioCommand::NotifyUnlink,o.target);
}
void hold(TechnoClass* unit) {
    Taunt::clearOrders(unit);
    unit->SetTarget(nullptr);
    if(unit->SpawnManager) unit->SpawnManager->ResetTarget();
    unit->ForceMission(Mission::Sleep);
}
bool start(TechnoClass* unit,const string& id,CellStruct center,int radius) {
    auto def=definitions.find(id);
    if(def==definitions.end() || !Taunt::present(unit) || !ActiveSkills::readyCaster(unit,unit->Owner) ||
       UnitMount::mounted(unit) || !ActiveSkills::playable(center) || radius<1 || radius>128) return false;
    if(def->second.enterKinds && !enterSource(unit,def->second.enterKinds)) return false;
    if(auto existing=orders.find(unit->UniqueID);existing!=orders.end()) releaseApproach(existing->second);
    hold(unit);
    orders[unit->UniqueID]={id,unit,nullptr,unit->Owner->ArrayIndex,center,radius};
    return true;
}
bool cancel(TechnoClass* unit,const string& id={}) {
    if(!unit) return false;
    auto it=orders.find(unit->UniqueID);
    if(it==orders.end() || (!id.empty() && it->second.id!=id)) return false;
    releaseApproach(it->second);
    orders.erase(it);
    if(Taunt::present(unit) && ActiveSkills::readyCaster(unit,unit->Owner)) {
        hold(unit);
        unit->ForceMission(Mission::Guard);
    }
    return true;
}
void forget(DWORD id) {
    orders.erase(id);
    for(auto& [key,o]:orders) if(o.target && o.target->UniqueID==id) {
        o.target=nullptr;o.fired=false;
        if(!isAutoEnter(o.id)) o.nextScan=0;
    }
}
bool eligible(const Order& o,const Definition& def,TechnoClass* t) {
    if(!Taunt::present(t) || !includes(def.houses,t->Owner->IsNeutral()?"neutral":"enemies") ||
       (!includes(def.kinds,"all") && !includes(def.kinds,kind(t)))) return false;
    auto p=t->GetMapCoords();
    int x=p.X-o.center.X,y=p.Y-o.center.Y;
    return x*x+y*y<=o.radius*o.radius && Taunt::canAttack(o.unit,t,true);
}
bool missileSalvoInProgress(TechnoClass* unit) {
    auto manager=unit->SpawnManager;
    if(!manager || !manager->SpawnType || !manager->SpawnType->MissileSpawn) return false;
    for(auto node:manager->SpawnedNodes)
        if(node && node->Unit && (node->Status==SpawnNodeStatus::Idle || node->Status==SpawnNodeStatus::TakeOff ||
           node->Status==SpawnNodeStatus::Preparing)) return true;
    return false;
}
void shot(TechnoClass* unit,AbstractClass* aim,bool missile=false) {
    if(!unit || orders.empty()) return;
    auto it=orders.find(unit->UniqueID);
    if(it!=orders.end() && aim && it->second.target==aim) {
        it->second.fired=true;it->second.missile=missile;
    }
}
void update(Order& o,const Definition& def,unsigned frame) {
    auto unit=o.unit;
    if(o.target && !eligible(o,def,o.target)) {
        o.target=nullptr;o.fired=false;o.nextScan=0;hold(unit);
    }
    const bool finished=o.fired && !unit->CurrentBurstIndex && (!o.missile || !missileSalvoInProgress(unit));
    if(frame>=o.nextScan && (!o.target || finished)) {
        std::map<DWORD,TechnoClass*> choices;
        visitNearbyCells(o.center,o.radius,!includes(def.kinds,"building") || def.kinds.size()>1,[&](TechnoClass* t){
            if(t!=o.target && !choices.contains(t->UniqueID) && eligible(o,def,t)) choices.emplace(t->UniqueID,t);
            return false;
        });
        if(!choices.empty()) {
            auto pick=choices.begin();
            if(choices.size()>1) std::advance(pick,ScenarioClass::Instance->Random.RandomRanged(0,static_cast<int>(choices.size())-1));
            o.target=pick->second;
        }
        o.nextScan=frame+def.interval;
        o.fired=false;
    }
    if(o.target) {
        if(unit->Target!=o.target) unit->SetTarget(o.target);
        if(unit->CurrentMission!=Mission::Attack) unit->ForceMission(Mission::Attack);
    } else if(unit->Target || unit->CurrentMission!=Mission::Sleep) hold(unit);
}
#include "AutoEnter.inc"
void tick() {
    EntryReservations reserved;
    for(const auto& [id,o]:orders) {
        auto def=definitions.find(o.id);
        if(o.target && def!=definitions.end() && def->second.enterKinds && Taunt::present(o.unit) &&
           !o.unit->Absorbed && !o.unit->BunkerLinkedItem && o.unit->Owner->ArrayIndex==o.owner)
            reserved[o.target->UniqueID]+=entrySpace(o.unit,o.target);
    }
    for(auto it=orders.begin();it!=orders.end();) {
        auto& o=it->second;
        auto unit=o.unit;
        auto def=definitions.find(o.id);
        if(def!=definitions.end() && def->second.enterKinds && alive(unit) &&
           (unit->Transporter || unit->Absorbed || unit->InLimbo || unit->BunkerLinkedItem)) {
            it=orders.erase(it);continue;
        }
        if(!Taunt::present(unit) || unit->Owner->ArrayIndex!=o.owner || def==definitions.end()) {
            it=orders.erase(it);
            if(Taunt::present(unit) && ActiveSkills::readyCaster(unit,unit->Owner)) {
                hold(unit);unit->ForceMission(Mission::Guard);
            }
            continue;
        }
        if(ActiveSkills::readyCaster(unit,unit->Owner)) {
            if(def->second.enterKinds) {
                if(!updateEnter(o,def->second,Unsorted::CurrentFrame,reserved)) {
                    releaseApproach(o);
                    it=orders.erase(it);
                    hold(unit);
                    unit->ForceMission(Mission::Guard);
                    continue;
                }
            }
            else update(o,def->second,Unsorted::CurrentFrame);
        }
        ++it;
    }
}
}
