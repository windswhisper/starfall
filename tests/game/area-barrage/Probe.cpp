#include "../../../src/Starfall.cpp"
namespace {
DWORD v3Id=0,tankId=0;
std::map<DWORD,int> hits;
DWORD impactOwner=0;
std::map<DWORD,int> visible;
CellStruct impactCell{};
void check(bool ok,const char* label) {
    std::ofstream("barrage.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
}
DEFINE_HOOK(0x4690D4,SF_TestBarrageImpact,6) {
    auto b=R->ESI<BulletClass*>();
    if(applyingDamage && b->Owner && b->WH && (b->Owner->UniqueID==tankId || b->Owner->UniqueID==v3Id)) {
        ++hits[b->Owner->UniqueID];
        impactOwner=b->Owner->UniqueID;
        impactCell=CellClass::Coord2Cell(*R->Base<CoordStruct*>(8));
    }
    return 0;
}
DEFINE_HOOK(0x4226F6,SF_TestBarrageAnimCreated,6) {
    auto anim=R->ESI<AnimClass*>();
    if(applyingDamage && impactOwner && anim->Type && !anim->Invisible &&
       WarheadTypeClass::Find("HE")->AnimList.FindItemIndex(anim->Type)>=0) {
        auto cell=CellClass::Coord2Cell(anim->Location);
        if(cell.X==impactCell.X && cell.Y==impactCell.Y) {++visible[impactOwner];impactOwner=0;}
    }
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestBarrageOrders,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    auto frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    auto human=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    UnitClass *v3=nullptr,*tank=nullptr,*dummy=nullptr;
    for(auto unit:UnitClass::Array) {
        if(unit->Owner==human && !_stricmp(unit->Type->ID,"V3") && (!v3 || unit->UniqueID==v3Id)) v3=unit;
        // The preplaced Grizzly starts in the central group.
        if(unit->Owner==human && !_stricmp(unit->Type->ID,"MTNK") &&
           (unit->UniqueID==tankId || (!tankId && unit->GetMapCoords().X==59))) tank=unit;
        if(!dummy && unit->Owner!=human && !_stricmp(unit->Type->ID,"LTNK")) dummy=unit;
    }
    if(loading) {
        if(frame==220) {
            auto type=UnitTypeClass::Find("MTNK");
            auto fresh=static_cast<UnitClass*>(type->CreateObject(human));
            CellStruct at{63,66};auto& map=MapClass::Instance;
            check(fresh && fresh->Unlimbo(map.GetCellAt(at)->GetCoordsWithBridge(),DirType::North) &&
                  ownsPassive(fresh,"sf_areabarrage"),"saved map binding on new tank");
            tankId=fresh->UniqueID;at.X+=3;
            fresh->SetTarget(map.GetCellAt(at));fresh->QueueMission(Mission::Attack,true);
        }
        if(frame==410) {
            check(hits[tankId]==20 && visible[tankId]>0,"real ground attack after load");
            TerminateProcess(GetCurrentProcess(),0);
        }
        return 0;
    }
    if(frame==15) {
        check(v3 && tank && dummy,"map units");
        v3Id=v3->UniqueID;tankId=tank->UniqueID;
        check(ownsPassive(v3,"sf_areabarrage") && ownsPassive(tank,"sf_areabarrage"),"bindings");
        dummy->Health=dummy->EstimatedHealth=40000;dummy->Deactivate();dummy->DiscoveredBy(human);
        for(auto unit:{v3,tank}) {unit->IronCurtain(700,human,false);unit->SetTarget(dummy);unit->QueueMission(Mission::Attack,true);}
    }
    if(frame==210) {
        std::ofstream("barrage.log",std::ios::app)<<"impacts "<<hits[v3Id]<<"/"<<hits[tankId]<<" animations "<<visible[v3Id]<<"/"<<visible[tankId]<<"\n";
        check(hits[v3Id]==20,"V3 barrage");check(hits[tankId]==20,"Grizzly barrage");
        check(visible[v3Id]==20 && visible[tankId]==20,"visible impacts");
        check(ScenarioClass::SaveGame("Saved Games\\Barrage.sav",L"Barrage"),"saved");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
