#include "../../../src/Starfall.cpp"
namespace {
void check(bool ok,const char* label) {
    std::ofstream("animations.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
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
        tickStates();
        check(!has("animconditional") && has("animalways") && !has("animevent") && has("animonly"),"initial visibility");
    }
    if(frame==21) {
        unit->Health=unit->Type->Strength/2;tickStates();
        check(!has("animconditional") && has("animevent"),"condition and delay");
    }
    if(frame==36) {
        tickStates();
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
        unit->Health=unit->Type->Strength;tickStates();
        check(!has("animconditional") && has("animalways") && !has("animevent") && has("animonly") && VisualScale::percent(id)==100,"condition exit");
        auto cloak=unit->CloakState;unit->CloakState=CloakState::Cloaked;tickStates();
        check(!has("animalways") && !has("animonly"),"cloak hides animation");
        unit->CloakState=cloak;tickStates();
        check(has("animalways") && has("animonly"),"uncloak restores animation");
        warheadGrants[id].skills.erase("animonly");removedPassives[id].insert("animonly");tickStates();
        check(!has("animonly"),"removed passive clears animation");
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
