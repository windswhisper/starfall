#include "../../../src/Starfall.cpp"
#include <EventClass.h>
namespace {
constexpr const char* ids[]={"NALASR","NASAM","TESLA","ATESLA","GTGCAN","GAPILL","NAFLAK","YAGGUN","YAPSYT"};
constexpr auto projectId="sf_hx_superdefense";
int projectIndex() {for(int i=0;i<static_cast<int>(Research::options.size());++i) if(Research::options[i].id==projectId) return i;return -1;}
std::array<DWORD,9> objects{};
DWORD oldId=0;
constexpr int voxelTowers[]={0,1,4,6,7};
std::array<BuildingClass*,9> originals{};
std::array<CanBuildResult,9> before{},otherBefore{};
std::set<int> cameos;
bool measuring=false;int voxelDraws=0;
bool trackingDirty=false;
std::optional<RectangleStruct> requestedDirty;
void check(bool ok,const char* label) {
    std::ofstream("hex-defense.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<'\n';
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
BuildingTypeClass* giant(int i) {return BuildingTypeClass::Find((string("SF_HX_Giant")+ids[i]).c_str());}
BuildingClass* find(DWORD id) {for(auto b:BuildingClass::Array) if(b->UniqueID==id) return b;return nullptr;}
BuildingClass* place(BuildingTypeClass* type,HouseClass* owner) {
    for(short y=40;y<90;y+=5) for(short x=40;x<90;x+=5) {
        CellStruct cell{x,y};
        if(BuildingSpawn::create(type,owner,MapClass::Instance.GetCellAt(cell)->GetCoords(),0)) {
            auto b=MapClass::Instance.GetCellAt(cell)->GetBuilding();
            b->QueueMission(Mission::Sleep,false);b->NextMission();return b;
        }
    }
    check(false,"available fixture location");return nullptr;
}
void snapshotIds(bool write) {
    if(write) {std::ofstream out("hex-defense.ids");out<<oldId;for(auto id:objects) out<<' '<<id;}
    else {std::ifstream in("hex-defense.ids");in>>oldId;for(auto& id:objects) in>>id;}
}
void stats() {
    tickStates();
    for(int i=0;i<9;++i) {
        auto b=find(objects[i]);auto base=BuildingTypeClass::Find(ids[i]);
        if(b) std::ofstream("hex-defense.log",std::ios::app)<<ids[i]<<" hp "<<b->Health<<'/'<<UnitHealth::read(b).maximum<<" fp "<<b->FirepowerMultiplier<<" size "<<VisualScale::percent(b->UniqueID)<<" range "<<b->GetWeaponRange(0)<<'/'<<base->GetWeapon(0,false).WeaponType->Range*3/2<<'\n';
        check(b && b->Type==giant(i) && UnitHealth::read(b).maximum==base->Strength*2 && VisualScale::percent(b->UniqueID)==200 &&
            std::abs(b->FirepowerMultiplier-2.0)<1e-5 && b->GetWeaponRange(0)==base->GetWeapon(0,false).WeaponType->Range*3/2,
            "giant native health damage size and range");
        auto bullet=b->GetWeapon(0)->WeaponType->Projectile->CreateBullet(b,b,10,WarheadTypeClass::Find("AP"),10,false);
        check(bullet && VisualScale::projectiles.contains(bullet->UniqueID) && VisualScale::projectiles.at(bullet->UniqueID)==200,
            "giant projectile launch snapshot");bullet->UnInit();
    }
    auto old=find(oldId);
    check(old && old->Type==BuildingTypeClass::Find("GAPILL") && old->Health==old->Type->Strength &&
        VisualScale::percent(old->UniqueID)==100 && old->FirepowerMultiplier==1.0,"existing ordinary tower unchanged");
}
void permissions(HouseClass* owner,HouseClass* other,bool compare) {
    for(int i=0;i<9;++i) {
        auto base=BuildingTypeClass::Find(ids[i]);
        check(owner->CanBuild(base,false,false)==CanBuildResult::Unbuildable,"original production removed");
        if(compare) check(owner->CanBuild(giant(i),false,false)==before[i] && other->CanBuild(base,false,false)==otherBefore[i],
            "replacement preserves original eligibility and other player");
        check(other->CanBuild(giant(i),false,false)!=CanBuildResult::Buildable,"other player cannot build giant");
    }
}
// Compare a full draw to an update of only the building's registered dirty area.
// Using DrawIfVisible here also exercises the native building culling path.
bool completeRedraw(BuildingClass* building) {
    auto view=DSurface::ViewBounds;
    auto surface=DSurface::Temp;
    auto tactical=TacticalClass::Instance;
    auto camera=tactical->TacticalPos;
    tactical->TacticalPos=TacticalClass::CoordsToScreen(building->GetRenderCoords())-Point2D{view.Width/2,view.Height/2};
    RectangleStruct dirty{};
    building->GetRenderDimensions(&dirty);
    const auto zoomed=dirty;
    dirty.X+=view.X;dirty.Y+=view.Y;
    auto render=[&](RectangleStruct region) {
        surface->FillRect(&view,0x39E7);ZBuffer::Instance->Fill(0xFFFF);ABuffer::Instance->Fill(0x7F);
        building->DrawIfVisible(&region,true,0);
        auto turretRegion=region;
        building->DrawIfVisible(&turretRegion,true,1);
        vector<WORD> result(view.Width*view.Height);
        auto bits=static_cast<const BYTE*>(surface->Lock(view.X,view.Y));
        for(int y=0;y<view.Height;++y)
            memcpy(result.data()+y*view.Width,bits+y*surface->GetPitch(),view.Width*sizeof(WORD));
        surface->Unlock();return result;
    };
    auto full=render(view),partial=render(dirty);
    int ink=0,missing=0;
    for(size_t i=0;i<full.size();++i) if(full[i]!=0x39E7) {
        ++ink;if(partial[i]==0x39E7) ++missing;
    }
    requestedDirty.reset();trackingDirty=true;
    VisualScale::setStateFactor(building,1.0);
    trackingDirty=false;
    RectangleStruct ordinary{};
    building->GetRenderDimensions(&ordinary);
    const bool clearsOld=requestedDirty && !memcmp(&*requestedDirty,&zoomed,sizeof(zoomed));
    const bool restoresBounds=!memcmp(&ordinary,&building->unknown_rect_63C,sizeof(ordinary)) &&
        ordinary.Width<zoomed.Width && ordinary.Height<zoomed.Height;
    VisualScale::setStateFactor(building,2.0);
    tactical->TacticalPos=camera;
    std::ofstream("hex-defense.log",std::ios::app)<<building->Type->ID<<" redraw ink "<<ink<<" missing "<<missing
        <<" bounds "<<dirty.X<<','<<dirty.Y<<','<<dirty.Width<<','<<dirty.Height<<'\n';
    return ink>0 && !missing && clearsOld && restoresBounds;
}
enum class DrawLayer { Whole, Body, Turret };
RectangleStruct drawBounds(ObjectClass* object,const string& file={},DrawLayer layer=DrawLayer::Whole) {
    auto surface=DSurface::Temp;RectangleStruct rect{0,0,600,440};Point2D p{300,270};
    surface->FillRect(&rect,0x39E7);
    ZBuffer::Instance->Fill(0xFFFF);ABuffer::Instance->Fill(0x7F);
    auto camera=TacticalClass::Instance->TacticalPos;
    TacticalClass::Instance->TacticalPos=TacticalClass::CoordsToScreen(object->GetRenderCoords())-p;
    measuring=true;voxelDraws=0;
    if(layer!=DrawLayer::Turret) object->DrawIt(&p,&rect);
    if(layer!=DrawLayer::Body && object->WhatAmI()==AbstractType::Building)
        reinterpret_cast<void(__thiscall*)(ObjectClass*,Point2D*,RectangleStruct*)>(0x43DA80)(object,&p,&rect);
    measuring=false;
    TacticalClass::Instance->TacticalPos=camera;
    if(layer==DrawLayer::Turret) check(voxelDraws>0,"native voxel turret draw path exercised");
    auto pixels=static_cast<const BYTE*>(surface->Lock(0,0));check(pixels!=nullptr,"draw surface lock");
    std::ofstream out;
    if(!file.empty()) {out.open(file,std::ios::binary);out<<"P6\n600 440\n255\n";}
    int left=600,right=-1,top=440,bottom=-1;int shifts[]={Drawing::RedShiftLeft,Drawing::GreenShiftLeft,Drawing::BlueShiftLeft};
    int masks[]={255>>Drawing::RedShiftRight,255>>Drawing::GreenShiftRight,255>>Drawing::BlueShiftRight};
    for(int y=0;y<440;++y) for(int x=0;x<600;++x) {
        WORD value=reinterpret_cast<const WORD*>(pixels+y*surface->GetPitch())[x];
        if(value!=0x39E7) {
            left=std::min(left,x);right=std::max(right,x);
            top=std::min(top,y);bottom=std::max(bottom,y);
        }
        if(!file.empty()) for(int k=0;k<3;++k) out.put(static_cast<char>(((value>>shifts[k])&masks[k])*255/masks[k]));
    }
    surface->Unlock();
    return {left,top,std::max(0,right-left+1),std::max(0,bottom-top+1)};
}
void checkTurretMounts() {
    for(int i:voxelTowers) {
        auto original=originals[i];
        auto large=find(objects[i]);
        for(auto dir:{DirType::North,DirType::East,DirType::South,DirType::West}) {
            auto draw=[dir](BuildingClass* b) {
                b->PrimaryFacing.SetCurrent(DirStruct(dir));b->SecondaryFacing.SetCurrent(DirStruct(dir));
                return std::pair{drawBounds(b,{},DrawLayer::Body),drawBounds(b,{},DrawLayer::Turret)};
            };
            const auto [body,turret]=draw(original);
            const auto [bigBody,bigTurret]=draw(large);
            const int dx=bigTurret.X-bigBody.X-2*(turret.X-body.X);
            const int dy=bigTurret.Y-bigBody.Y-2*(turret.Y-body.Y);
            std::ofstream("hex-defense.log",std::ios::app)<<ids[i]<<" facing "<<int(dir)<<" mount error "<<dx<<','<<dy
                <<" turret "<<turret.Width<<'x'<<turret.Height<<" -> "<<bigTurret.Width<<'x'<<bigTurret.Height<<'\n';
            check(turret.Width>0 && body.Width>0 && std::abs(dx)<=1 && std::abs(dy)<=1 &&
                std::abs(bigTurret.Width-2*turret.Width)<=1 && std::abs(bigTurret.Height-2*turret.Height)<=1,
                "scaled voxel turret and barrel stay aligned with the building body");
        }
    }
}
}
DEFINE_HOOK(0x6D2790,SF_TestGiantDirtyRegion,9) {
    if(trackingDirty) requestedDirty=R->Stack<RectangleStruct>(4);
    return 0;
}
DEFINE_HOOK(0x43DF5A,SF_TestVoxelPath,6) {
    if(measuring) ++voxelDraws;
    return 0;
}
DEFINE_HOOK(0x445761,SF_TestGiantCameo,8) {
    for(int i=0;i<9;++i) if(auto t=giant(i);t && R->ESI<int>()==t->ArrayIndex) cameos.insert(i);
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestHexDefense,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    const int f=Unsorted::CurrentFrame;
    if(GetFileAttributesA("art-export.flag")!=INVALID_FILE_ATTRIBUTES) {
        if(f!=30) return 0;
        CCFileClass input("HexBrawl.art.ini"),output("artmd.ini");
        CCINIClass::INI_Art.ReadCCFile(&input);CCINIClass::INI_Art.WriteCCFile(&output);
        std::ofstream("hex-defense.log")<<"PASS prepared complete art rules\n";
        TerminateProcess(GetCurrentProcess(),0);return 0;
    }
    auto owner=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA),other=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
    if(GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES) {
        if(f<90) return 0;
        snapshotIds(false);stats();permissions(owner,other,false);
        check(Research::owns(owner->ArrayIndex,projectId),"research and replacements survive load");
        check(owner->GetFactoryProducing(giant(5))!=nullptr,"giant order survives load");
        TerminateProcess(GetCurrentProcess(),0);return 0;
    }
    if(f==30) {
        Research::ensureReady();int project=projectIndex();
        check(project>=0 && Research::available(project) && Research::options[project].rarity==Research::Rarity::Epic &&
            Research::options[project].effects.size()==9,"epic project and nine replacements parsed");
        for(int i=0;i<9;++i) {
            auto b=BuildingTypeClass::Find(ids[i]);auto g=giant(i);
            check(g && g->GetFoundationWidth()==2*b->GetFoundationWidth() && g->GetFoundationHeight(false)==2*b->GetFoundationHeight(false) &&
                g->Cost==2*b->Cost && g->Strength==2*b->Strength && g->GetImage() && g->GetImage()->GetData() && g->LoadBuildup(),
                "independent doubled foundation price health and reused art");
            check(owner->CanBuild(g,false,false)!=CanBuildResult::Buildable,"giant hidden before research");
            check(VisualScale::placementPercent(g)==200 && VisualScale::placementPercent(b)==100,"independent placement preview scale");
        }
        for(auto id:{"GACNST","GAPOWR","GAPILE","GAAIRC","NACNST","NAPOWR","NAHAND","NARADR","YACNST","YAPOWR","YABRCK","NAPSIS"})
            place(BuildingTypeClass::Find(id),owner);
        oldId=place(BuildingTypeClass::Find("GAPILL"),owner)->UniqueID;
    }
    if(f==40) {
        for(int i=0;i<9;++i) {auto t=BuildingTypeClass::Find(ids[i]);before[i]=owner->CanBuild(t,false,false);otherBefore[i]=other->CanBuild(t,false,false);}
        check(before[5]==CanBuildResult::Buildable,"ordinary tower buildable fixture");
        auto factory=static_cast<FactoryClass*>(YRMemory::AllocateChecked(sizeof(FactoryClass)));
        reinterpret_cast<void(__thiscall*)(FactoryClass*)>(0x4C98B0)(factory);factory->Owner=owner;
        check(factory->DemandProduction(BuildingTypeClass::Find("GAPILL"),owner,false),"ordinary tower order");
        owner->TakeMoney(50);factory->Balance-=50;int money=owner->Available_Money();
        Research::savedOffers[owner->ArrayIndex]={projectId};Research::clearOfferCache();Research::award(owner->ArrayIndex,100);
        check(Research::purchase(owner->ArrayIndex,projectIndex(),owner->Type->ID),"purchased giant tower research");
        check(!factory->Object && owner->Available_Money()==money+50,"ordinary order cancelled with exact refund");
        delete factory;permissions(owner,other,true);
        auto tech=owner->TechLevel;owner->TechLevel=0;
        check(owner->CanBuild(giant(5),false,false)!=CanBuildResult::Buildable,"source technology restriction retained");owner->TechLevel=tech;
        auto limit=giant(5)->BuildLimit;giant(5)->BuildLimit=0;
        check(owner->CanBuild(giant(5),false,false)!=CanBuildResult::Buildable,"replacement build limit retained");giant(5)->BuildLimit=limit;
        auto country=owner->Type;owner->Type=HouseTypeClass::Find("French");
        check(owner->CanBuild(giant(4),false,false)==CanBuildResult::Buildable,"French grand cannon available");owner->Type=country;
        check(owner->CanBuild(giant(4),false,false)!=CanBuildResult::Buildable,"French grand cannon country restriction retained");
        for(int i=0;i<9;++i) objects[i]=place(giant(i),owner)->UniqueID;
        for(int i:voxelTowers) originals[i]=place(BuildingTypeClass::Find(ids[i]),owner);
        // The pack targets only the ordinary cannon in this research.
        auto& projects=Research::unlocked[owner->ArrayIndex];
        projects.insert("sf_hx_gatlingdownpour");
        for(auto cannon:{originals[7],find(objects[7])}) {
            vector<string> passives;
            researchSkills(cannon,&passives);
            check(includes(passives,"sf_hx_gatlingdownpour_p"),"Gatling Downpour reaches ordinary and giant cannons");
        }
        projects.erase("sf_hx_gatlingdownpour");
        for(auto id:objects) {
            auto b=find(id);auto cell=b->GetMapCoords();int occupied=0;
            for(int dy=-5;dy<=5;++dy) for(int dx=-5;dx<=5;++dx) {
                auto c=MapClass::Instance.TryGetCellAt(CellStruct{short(cell.X+dx),short(cell.Y+dy)});
                if(c && c->GetBuilding()==b) ++occupied;
            }
            check(occupied==b->Type->GetFoundationWidth()*b->Type->GetFoundationHeight(false),"entire enlarged foundation occupies map cells");
        }
        stats();snapshotIds(true);
    }
    if(f==60) {
        bool complete=true;
        for(auto id:objects) complete=completeRedraw(find(id)) && complete;
        check(complete,"all giant towers redraw complete silhouette and clear previous bounds on shrink");
        checkTurretMounts();
        check(cameos.contains(5),"giant cameo offered by native sidebar");
        auto forbidden=EventClass(owner->ArrayIndex,EventType::Produce,static_cast<int>(AbstractType::BuildingType),BuildingTypeClass::Find("GAPILL")->ArrayIndex,false);
        check(EventClass::OutList.Add(forbidden),"attempted retired production command");
        auto event=EventClass(owner->ArrayIndex,EventType::Produce,static_cast<int>(AbstractType::BuildingType),giant(5)->ArrayIndex,false);
        check(EventClass::OutList.Add(event),"normal giant production command");
        for(int i=0;i<9;++i) {
            auto b=find(objects[i]);VisualScale::state.erase(b->UniqueID);
            int normal=drawBounds(b,string("tower-")+ids[i]+"-100.ppm").Width;VisualScale::state[b->UniqueID]=2.0;
            int large=drawBounds(b,string("tower-")+ids[i]+"-200.ppm").Width;
            std::ofstream("hex-defense.log",std::ios::app)<<ids[i]<<" raster "<<normal<<" -> "<<large<<'\n';
            check(normal>0 && large>normal*1.5,"native building sprite and turret enlarged");
            if(b->Type->TurretAnimIsVoxel) {
                VisualScale::state.erase(b->UniqueID);int narrow=drawBounds(b,string("turret-")+ids[i]+"-100.ppm",DrawLayer::Turret).Width;
                VisualScale::state[b->UniqueID]=2.0;int big=drawBounds(b,string("turret-")+ids[i]+"-200.ppm",DrawLayer::Turret).Width;
                check(narrow>0 && big>narrow*1.5,"voxel turret and barrel independently enlarged");
            }
        }
        for(int i:{2,3,8}) {
            auto b=find(objects[i]);b->PlayNthAnim(BuildingAnimSlot::Active,false,false);
        }
        int animations=0;
        for(auto id:objects) for(auto anim:find(id)->Anims) if(anim && anim->IsBuildingAnim) {
            VisualScale::state.erase(id);int normal=drawBounds(anim,"anim-100.ppm").Width;
            VisualScale::state[id]=2.0;int large=drawBounds(anim,"anim-200.ppm").Width;
            if(normal) {check(large>normal*1.5,"native building animation enlarged");++animations;}
        }
        check(animations>0,"building active animations exercised");
    }
    if(f==80) {
        check(owner->GetFactoryProducing(BuildingTypeClass::Find("GAPILL"))==nullptr,"retired production command rejected");
        check(owner->GetFactoryProducing(giant(5))!=nullptr,"native factory accepts giant order");
        check(ScenarioClass::SaveGame("Saved Games\\HexDefense.sav",L"Giant defense"),"saved");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
