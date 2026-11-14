#include "../../../src/Starfall.cpp"
#include <TacticalClass.h>
namespace {
void check(bool ok,const char* label) {
    std::ofstream("animations.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
// Count rendered pixels that change toward the requested channel within the same frame.
vector<WORD> renderBody(TechnoClass* unit) {
    auto surface=DSurface::Temp;
    Point2D pos{200,180};RectangleStruct bounds=DSurface::ViewBounds,area{120,100,160,160};
    // The backing depth surface is scrolled independently of screen coordinates.
    // Clearing only the sample rectangle can leave the first building draw's
    // depth values in place and suppress every pixel of the second draw.
    ZBuffer::Instance->Fill(0xFFFF);
    surface->FillRect(&area,0x39E7);
    if(unit->WhatAmI()==AbstractType::Building) {
        auto tactical=TacticalClass::Instance;
        auto previous=tactical->TacticalPos;
        tactical->TacticalPos=tactical->CoordsToScreen(unit->GetRenderCoords())-pos;
        auto building=static_cast<BuildingClass*>(unit);
        building->DrawBehind(&pos,&bounds);
        building->Draw(pos,bounds);
        building->DrawIt(&pos,&bounds);
        for(auto anim:building->Anims) if(anim) anim->DrawIt(&pos,&bounds);
        tactical->TacticalPos=previous;
    } else unit->DrawIt(&pos,&bounds);
    const int pitch=surface->GetPitch();
    const auto base=static_cast<const BYTE*>(surface->Lock(area.X,area.Y));
    check(base!=nullptr,"read rendered body");
    vector<WORD> pixels;
    for(int y=0;y<area.Height;++y) {
        auto row=reinterpret_cast<const WORD*>(base+y*pitch);
        pixels.insert(pixels.end(),row,row+area.Width);
    }
    surface->Unlock();return pixels;
}
void verifyTintPixels(TechnoClass* unit,const char* label) {
    auto base=renderBody(unit);
    grantToUnit(unit,PassiveGrant{{"tintonly"},-1});tickStates();
    auto blue=renderBody(unit);
    check(std::any_of(base.begin(),base.end(),[](WORD pixel){return pixel!=0x39E7;}) &&
        std::any_of(blue.begin(),blue.end(),[](WORD pixel){return pixel!=0x39E7;}),"both body samples contain rendered pixels");
    int brighter=0;
    int mask=Drawing::RGB_To_Int(0,0,255);
    for(size_t i=0;i<base.size();++i)
        if(base[i]!=0x39E7 && blue[i]!=0x39E7 && (blue[i]&mask)>(base[i]&mask)) ++brighter;
    std::ofstream("animations.log",std::ios::app)<<label<<" base "<<(base.size()-std::count(base.begin(),base.end(),0x39E7))<<" pixels "<<brighter<<"\n";
    check(brighter>=4,label);
}
int animWidth(AnimClass* anim) {
    auto surface=DSurface::Temp;
    Point2D pos{220,200};RectangleStruct area{40,20,360,360};
    surface->FillRect(&area,0x39E7);anim->DrawIt(&pos,&area);
    const int pitch=surface->GetPitch();
    auto pixels=static_cast<const BYTE*>(surface->Lock(area.X,area.Y));
    if(!pixels) return 0;
    int left=area.Width,right=-1;
    for(int y=0;y<area.Height;++y) {
        auto row=reinterpret_cast<const WORD*>(pixels+y*pitch);
        for(int x=0;x<area.Width;++x) if(row[x]!=0x39E7) {left=std::min(left,x);right=std::max(right,x);}
    }
    surface->Unlock();return std::max(0,right-left+1);
}
}
DEFINE_HOOK(0x55B4EB,SF_TestPassiveAnimations,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    bool loading=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    if(loading && frame<=37) return 0;
    if(frame!=20 && frame!=21 && frame!=36 && frame!=37 && frame!=40) return 0;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    UnitClass* unit=nullptr;DWORD saved=0;
    if(frame!=20) {std::ifstream("animation.id")>>saved;}
    for(auto u:UnitClass::Array) if(u->Owner==house && !_stricmp(u->Type->ID,"MTNK") && (!saved || u->UniqueID==saved)) {unit=u;break;}
    check(unit!=nullptr,"unit");auto id=unit->UniqueID;
    auto has=[&](const char* skill){return attachedAnims.contains({id,skill});};
    if(frame==20) {
        std::ofstream("animation.id")<<id;
        check(skills.contains("animconditional") && skills.contains("animalways") && skills.contains("animevent") &&
            skills.contains("animonly") && !skills.contains("animinvalid"),"configuration");
        grantToUnit(unit,PassiveGrant{{"animconditional","animalways","animevent","animonly"},-1});
        check(skills.contains("tintonly") && skills.contains("woundedtint") && !skills.contains("badtint"),"tint configuration");
        verifyTintPixels(unit,"voxel body tint");
        auto gi=static_cast<InfantryClass*>(InfantryTypeClass::Find("E1")->CreateObject(house));
        CellStruct cell{48,70};
        check(gi && gi->Unlimbo(MapClass::Instance.GetCellAt(cell)->GetCoordsWithBridge(),DirType::North),"infantry placed");
        MapClass::Instance.Reveal(house);
        verifyTintPixels(gi,"infantry body tint");
        auto aircraft=static_cast<AircraftClass*>(AircraftTypeClass::Find("ORCA")->CreateObject(house));
        auto airAt=unit->GetCoords();airAt.Z+=512;
        check(aircraft && aircraft->Unlimbo(airAt,DirType::North),"aircraft placed");
        verifyTintPixels(aircraft,"aircraft body tint");
        auto building=static_cast<BuildingClass*>(BuildingTypeClass::Find("GAPOWR")->CreateObject(house));
        CellStruct buildingAt{50,65};
        check(building && building->Unlimbo(MapClass::Instance.GetCellAt(buildingAt)->GetCoordsWithBridge(),DirType::North),"building placed");
        building->DiscoveredBy(house);
        building->QueueMission(Mission::Guard,true);building->NextMission();
        building->BeginMode(BStateType::Idle);
        MapClass::Instance.Reveal(house);
        verifyTintPixels(building,"building body tint");
        grantToUnit(unit,PassiveGrant{{"woundedtint"},-1});tickStates();
        check(passiveTints.at(id)==ColorStruct{0,0,200},"tint before condition");
        check(!has("animconditional") && has("animalways") && !has("animevent") && has("animonly"),"initial visibility");
    }
    if(frame==21) {
        unit->Health=unit->Type->Strength/2;tickStates();
        check(!has("animconditional") && has("animevent"),"condition and delay");
    }
    if(frame==36) {
        tickStates();
        check(passiveTints.at(id)==ColorStruct{255,0,200} && !unit->IsIronCurtained(),"combined tint without invulnerability");
        check(has("animconditional") && VisualScale::percent(id)==200,"delayed activation and scale");
        auto& record=attachedAnims.at({id,"animconditional"});AnimFrame resolver;
        auto anim=resolver.resolve(record);check(anim!=nullptr,"animation object");
        auto entry=VisualScale::attachedAnims.extract(anim->UniqueID);
        int normal=animWidth(anim);VisualScale::attachedAnims.insert(std::move(entry));
        int scaled=animWidth(anim);
        check(normal>0 && std::abs(normal*2-scaled)<=2,"animation raster scales");
        VisualScale::base[id]=25;
        int reduced=animWidth(anim);
        check(VisualScale::percent(id)==50 && reduced>0 && reduced<normal,"animation follows live scale");
        VisualScale::base.erase(id);
    }
    if(frame==37 && !loading) check(ScenarioClass::SaveGame("Saved Games\\PassiveAnimations.sav",L"PassiveAnimations"),"saved");
    if(frame==40) {
        tickStates();
        check(has("animconditional") && has("animalways") && has("animevent") && has("animonly") &&
            VisualScale::attachedAnims.at(attachedAnims.at({id,"animconditional"}).id)==id && VisualScale::percent(id)==200,"retained after save");
        check(passiveTints.at(id)==ColorStruct{255,0,200},"tint after load");
        unit->Health=unit->Type->Strength;tickStates();
        check(passiveTints.at(id)==ColorStruct{0,0,200},"condition exits restore tint");
        check(!has("animconditional") && has("animalways") && !has("animevent") && has("animonly") && VisualScale::percent(id)==100,"condition exit");
        auto cloak=unit->CloakState;unit->CloakState=CloakState::Cloaked;tickStates();
        check(!has("animalways") && !has("animonly"),"cloak hides animation");
        check(drawTint(unit,0)==0,"cloak hides tint");
        unit->CloakState=cloak;tickStates();
        check(has("animalways") && has("animonly"),"uncloak restores animation");
        warheadGrants[id].skills.erase("animonly");removedPassives[id].insert("animonly");tickStates();
        check(!has("animonly"),"removed passive clears animation");
        unit->IronCurtain(75,house,false);
        check(unit->IsIronCurtained() && !drawTint(unit,0) && drawTint(unit,37)==37,"iron curtain takes precedence");
        removeHeldPassive(unit,"tintonly");tickStates();
        check(!passiveTints.contains(id),"lost passive clears tint");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
