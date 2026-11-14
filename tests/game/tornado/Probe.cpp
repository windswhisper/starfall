#include "../../../src/Starfall.cpp"
#include <EventClass.h>
namespace {
void check(bool ok,const char* label) {
    std::ofstream("tornado.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<label<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
FootClass* place(TechnoTypeClass* type,HouseClass* owner,CellStruct p) {
    auto u=static_cast<FootClass*>(type->CreateObject(owner));
    check(u && u->Unlimbo(MapClass::Instance.GetCellAt(p)->GetCoordsWithBridge(),DirType::North),"place");
    return u;
}
BuildingClass* placeBuilding(HouseClass* owner,CellStruct p) {
    auto type=BuildingTypeClass::Find("GAPILL");
    auto u=static_cast<BuildingClass*>(type->CreateObject(owner));
    check(u && u->Unlimbo(MapClass::Instance.GetCellAt(p)->GetCoordsWithBridge(),DirType::North),"place building");
    u->Health=u->EstimatedHealth=2000;
    return u;
}
int height(DWORD id) {
    auto it=ForceMove::motions.find(id);
    return it==ForceMove::motions.end()?0:it->second.lift();
}
int visualPixels(AnimClass* anim) {
    auto surface=DSurface::Temp;RectangleStruct area{0,0,surface->GetWidth(),surface->GetHeight()};
    surface->FillRect(&area,0x39E7);Point2D point{surface->GetWidth()/2,surface->GetHeight()/2};anim->DrawIt(&point,&area);
    auto base=static_cast<BYTE*>(surface->Lock(0,0));if(!base)return 0;int count=0;
    for(int y=0;y<surface->GetHeight();++y){auto row=reinterpret_cast<WORD*>(base+y*surface->GetPitch());for(int x=0;x<surface->GetWidth();++x)if(row[x]!=0x39E7)++count;}
    surface->Unlock();return count;
}
}
DEFINE_HOOK(0x55B4EB,SF_TestTornado,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int f=Unsorted::CurrentFrame;
    if(f<20 || f>220) return 0;
    bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    // Other armies in the skirmish can shell the fixtures during flight. Keep
    // native weapon fire out of the damage measurement in both game sessions.
    for(auto weapon:WeaponTypeClass::Array) weapon->Damage=0;
    BuildingTypeClass::Find("GAPILL")->Strength=2000;
    CellStruct start{44,73},aim{52,73};
    auto human=HouseClass::CurrentPlayer;
    if(f==20 && !load) {
        Research::ensureReady();
        auto foe=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
        auto tank=UnitTypeClass::Find("MTNK");
        auto caster=place(tank,foe,{39,73});
        auto a=place(tank,human,{46,73}),b=place(InfantryTypeClass::Find("E1"),human,{44,76});
        auto friendUnit=place(tank,foe,{41,73}),outside=place(tank,human,{40,79});
        auto neutral=place(tank,HouseClass::FindNeutral(),{42,71});
        auto heavyType=UnitTypeClass::Find("HTNK");
        heavyType->Weight=999;
        auto heavy=place(heavyType,human,{44,73});
        auto protectedUnit=place(tank,foe,{44,72});
        protectedUnit->IronCurtain(220,foe,false);
        auto insideBuilding=placeBuilding(foe,{48,74}),outsideBuilding=placeBuilding(human,{47,81});
        auto along=place(tank,human,{54,76});
        along->Deactivate();along->Health=along->EstimatedHealth=2000;
        for(auto u:{a,b,friendUnit,outside,caster,neutral}) {u->Health=u->EstimatedHealth=2000;}
        std::ofstream("tornado.ids")<<a->UniqueID<<" "<<b->UniqueID<<" "<<friendUnit->UniqueID<<" "<<outside->UniqueID<<" "<<caster->UniqueID<<" "<<neutral->UniqueID<<" "<<insideBuilding->UniqueID<<" "<<outsideBuilding->UniqueID<<" "<<along->UniqueID;
        const auto& specs=ActiveSkills::specs;
        auto skill=skills.find("sf_tornado");
        auto sample=ResearchAudio::load("tornado_attack.wav");
        check(skill!=skills.end() && skill->second.scriptParams.contains("sound") &&
            skill->second.scriptParams.at("sound")=="wav:tornado_attack.wav" &&
            sample.format.nChannels==2 && sample.format.nSamplesPerSec==48000 &&
            sample.format.wBitsPerSample==16 && !sample.data.empty(),"tornado WAV configured and decoded");
        auto spec=std::find_if(specs.begin(),specs.end(),[](const auto& s){return s.id=="sf_tornado";});
        check(spec!=specs.end() && spec->kind==ActiveSkills::Kind::Direction &&
            ActiveSkills::execute(foe->ArrayIndex,spec-specs.begin(),caster->UniqueID,0,aim) &&
            ForceMove::locked(a) && ForceMove::locked(b) && !ForceMove::locked(caster) && ForceMove::locked(neutral) &&
            ForceMove::locked(friendUnit) && !ForceMove::locked(along),"directional cast");
        auto fx=std::find_if(LuaAnim::entries.begin(),LuaAnim::entries.end(),[&](const auto& v){return v.second.owner==caster->UniqueID;});
        check(fx!=LuaAnim::entries.end(),"tornado visual created");auto visual=LuaAnim::find(fx->first);
        check(visual && visual->Type && !_stricmp(visual->Type->ID,"PIFF") && visual->Type->GetImage() && fx->second.zOffset==768,"tornado native animation configured");
        check(visualPixels(visual)>0,"tornado native animation drawn");
        std::ofstream("tornado-anim.id")<<fx->first;
        check(!ForceMove::locked(heavy) && protectedUnit->IsIronCurtained() && !ForceMove::locked(protectedUnit),"tornado skips heavy and protected targets");
        check(ForceMove::motions.at(a->UniqueID).tilt && ForceMove::visualTilt(static_cast<UnitClass*>(a)).side!=0,"vortex tilts vehicle");
    }
    if(f==37 && !load) {
        auto late=placeBuilding(human,{55,70});
        std::ofstream("building.id")<<late->UniqueID;
    }
    DWORD aid,bid,fid,oid,cid,nid,insideId,outsideId,alongId;
    std::ifstream("tornado.ids")>>aid>>bid>>fid>>oid>>cid>>nid>>insideId>>outsideId>>alongId;
    Objects units;for(auto u:TechnoClass::Array) if(alive(u)) units.emplace(u->UniqueID,u);
    auto a=units.at(aid),b=units.at(bid);
    if(f>=22 && f<=116 && f%3==0) {
        auto center=a->GetCenterCoords();
        vector<DWORD> expected,actual;
        auto matches=[&](TechnoClass* u){return alive(u) && u->Owner && u->IsOnMap && !u->InLimbo &&
            distanceCells(center,u->GetCenterCoords())<=1.75;};
        for(auto [id,u]:units) if(matches(u)) expected.push_back(id);
        LuaNearby::visit(units,center,1.75,[&](DWORD id,TechnoClass* u){if(matches(u)) actual.push_back(id);});
        if(expected!=actual) check(false,"moving nearby query");
        if(f==114) check(true,"moving nearby query");
    }
    constexpr int peakFrame=116, releasedFrame=129;
    static int previous=2000,hits=0,previousLift=0;
    double progress=std::clamp((f-20)/96.0,0.0,1.0);
    double eyeX=start.X+13*progress;
    DWORD visualId=0;std::ifstream("tornado-anim.id")>>visualId;
    if(f>=21 && f<=116 && (f==21 || f==40 || f==96 || f==116)) {
        auto it=LuaAnim::entries.find(visualId);auto visual=LuaAnim::find(visualId);
        bool follows=it!=LuaAnim::entries.end() && visual && std::abs(visual->Location.X-static_cast<int>(std::lround((eyeX+0.5)*256)))<=2 && visual->Location.Y==73*256+128;
        if(!follows)check(false,"tornado visual position");
        if(f==96)check(true,"tornado visual follows eye");
        if(load && f==40){check(true,"tornado visual restored");check(visualPixels(visual)>0,"restored tornado native animation drawn");}
    }
    if(f==129)check(!LuaAnim::entries.contains(visualId) && !LuaAnim::find(visualId),"tornado visual removed");
    bool pulse=f<peakFrame && (f-20)%6==0;
    auto inWind=[&](int x,int y){return (x-eyeX)*(x-eyeX)+(y-start.Y)*(y-start.Y)<=16;};
    static int priorInside=2000,previousLate=2000;
    if(f<=releasedFrame) {
        auto inside=units.at(insideId);
        if(pulse && inside->Health!=priorInside-(inWind(48,74)?12:0))
            check(false,"building pulse follows eye");
        priorInside=inside->Health;
        check(!ForceMove::locked(inside) && inside->GetMapCoords()==CellStruct{48,74} &&
            units.at(outsideId)->Health==2000,"buildings stay put");
    }
    if(f>=37 && f<=releasedFrame) {
        DWORD lateId;std::ifstream("building.id")>>lateId;
        auto late=units.at(lateId);
        if(pulse && late->Health!=previousLate-(inWind(55,70)?12:0)) check(false,"late building pulse timing");
        previousLate=late->Health;
        if(f==releasedFrame) check(late->Health<2000 && !ForceMove::locked(late),"building hit along route");
    }
    if(f==96) check(ForceMove::locked(units.at(alongId)) && units.at(alongId)->Health<2000 &&
        height(alongId)>0 && height(alongId)<height(aid),"picked up along route");
    static double swept=0,previousAngle=0;
    double angle=std::atan2(a->Location.Y-(start.Y*256+128.0),a->Location.X-(eyeX*256+128.0));
    if(!load) swept+=std::remainder(angle-previousAngle,2*Math::Pi);
    previousAngle=angle;
    if(load && f==39) std::ifstream("tornado.count")>>hits>>previous;
    if(a->Health<previous) ++hits;
    previous=a->Health;
    if(!load && f>=22 && f<=peakFrame && (f-20)%2==0) {
        int h=height(aid);
        if(h<=previousLift) check(false,"spiral rises at every segment");
        previousLift=h;
    }
    if(f==39 && !load) {
        std::ofstream("tornado.count")<<hits<<" "<<previous;
        check(ScenarioClass::SaveGame("Saved Games\\Tornado.sav",L"Tornado"),"saved in vortex");
    }
    if(f>=40 && f<=releasedFrame) {
        std::ofstream trace(load?"load.flight":"fresh.flight",std::ios::app);
        for(auto u:{a,b}) trace<<f<<" "<<u->Location.X<<" "<<u->Location.Y<<" "<<height(u->UniqueID)<<" "<<u->Health<<"\n";
    }
    if(f==peakFrame) {
        check(std::abs(height(aid)-768)<=2 && std::abs(height(bid)-768)<=2,"reached peak");
        if(!load) check(std::abs(swept/Math::TwoPi-5)<0.25,"five turn rising spiral");
    }
    if(f==releasedFrame) {
        check(!ForceMove::locked(a) && !ForceMove::locked(b) && scriptTimers.queue.empty(),"landed and released");
        std::ofstream("tornado.log",std::ios::app)<<"hits "<<hits<<" bystanders "<<units.at(fid)->Health<<" "<<units.at(oid)->Health<<"\n";
        check(units.at(fid)->Health<2000 && units.at(cid)->Health==2000 && units.at(nid)->Health<2000 &&
            units.at(oid)->Health==2000,"all houses hit along route");
        check(hits==16 && b->Health<2000,"sixteen damage pulses");
        if(load) {
            auto contents=[](const char* path){std::ifstream in(path);return string(std::istreambuf_iterator<char>(in),{});};
            check(contents("fresh.flight")==contents("load.flight"),"identical flight after load");
        }
        auto destination=MapClass::Instance.GetCellAt(CellStruct{61,76});
        check(a->IsControllable() && EventClass::OutList.Add(EventClass(human->ArrayIndex,TargetClass(a),Mission::Move,
            TargetClass(),TargetClass(destination),TargetClass())),"new order after tornado");
    }
    if(f>releasedFrame && a->GetMapCoords()==CellStruct{61,76}) {
        check(true,"player drives out after tornado");
        TerminateProcess(GetCurrentProcess(),0);
    }
    if(f==220) {
        std::ofstream("tornado.log",std::ios::app)<<"final player cell "<<a->GetMapCoords().X<<","<<a->GetMapCoords().Y<<" mission "<<int(a->CurrentMission)<<"\n";
        check(false,"player drives out after tornado");
    }
    return 0;
}
