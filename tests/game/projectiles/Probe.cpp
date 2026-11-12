#include "../../../src/Starfall.cpp"

namespace {
int explosionWidth(AnimClass* anim) {
    auto surface=DSurface::Temp;
    Point2D pos{220,200};
    RectangleStruct area{40,20,360,360};
    surface->FillRect(&area,0x39E7);
    anim->DrawIt(&pos,&area);
    const int pitch=surface->GetPitch();
    const BYTE* pixels=static_cast<const BYTE*>(surface->Lock(area.X,area.Y));
    if(!pixels) return 0;
    int left=area.Width,right=-1;
    for(int y=0;y<area.Height;++y) {
        auto row=reinterpret_cast<const WORD*>(pixels+y*pitch);
        for(int x=0;x<area.Width;++x) if(row[x]!=0x39E7) {left=std::min(left,x);right=std::max(right,x);}
    }
    surface->Unlock();
    return std::max(0,right-left+1);
}
}
// Runs after the production frame hook in the isolated test DLL.
DEFINE_HOOK(0x55B4EB, SF_IntegrationTestFrame, 5) {
    static const bool enabled=GetFileAttributesA("Starfall.test")!=INVALID_FILE_ATTRIBUTES;
    if(!enabled) return 0;
    const int f=Unsorted::CurrentFrame;
    if(GetFileAttributesA("reload.flag")==INVALID_FILE_ATTRIBUTES && (f==25 || f==26 || f==27)) {
        UnitClass* shooter=nullptr,* victim=nullptr,* control=nullptr;
        for(auto u:UnitClass::Array) {
            if(!shooter && !_stricmp(u->Type->ID,"MTNK")) shooter=u;
            if(!victim && !_stricmp(u->Type->ID,"TTNK")) victim=u;
            if(!control && !_stricmp(u->Type->ID,"HTNK")) control=u;
        }
        if(!shooter || !victim || !control) return 0;
        std::ofstream log("probe.log",std::ios::app);
        if(f==25) {
            capture("onkill",shooter,victim,shooter,victim,0);
            dispatch();
            log<<"shooter "<<shooter->UniqueID<<" state "<<projectileState[shooter->UniqueID]<<" base "<<projectileBase[shooter->UniqueID]<<" unit model "<<VisualScale::percent(shooter->UniqueID)<<"\n";
        }
        if(f==26) {
            auto wh=WarheadTypeClass::Find("AP");
            float orig=wh->CellSpread;
            wh->CellSpread=1;
            auto at=victim->GetCoords(); at.X+=480;
            auto cell=MapClass::Instance.GetCellAt(at);
            auto bt=WeaponTypeClass::Find("105mm")->Projectile;
            for(auto u:{control,shooter}) {
                auto b=bt->CreateBullet(cell,u,100,wh,10,false);
                b->SetLocation(at);
                int hp=victim->Health;
                if(u==shooter) {projectileBase.erase(shooter->UniqueID);projectileState.erase(shooter->UniqueID);}
                auto it=VisualScale::projectiles.find(b->UniqueID);
                log<<"created "<<(it==VisualScale::projectiles.end()?100:it->second)<<" at distance "<<victim->GetCoords().DistanceFrom(at)<<"\n";
                b->Detonate(at);
                log<<"explosions after impact "<<VisualScale::explosions.size()<<"\n";
                log<<"damage "<<hp-victim->Health<<" restored "<<wh->CellSpread<<" stack "<<spreadScopes.size()<<"\n";
                b->UnInit();
            }
            wh->CellSpread=orig;
            projectileBase[shooter->UniqueID]=200;

        }
        if(f==27) {
            auto wh=WarheadTypeClass::Find("AP");
            bool shp=false,vxl=false;
            for(auto t:BulletTypeClass::Array) {
                if(!t->Voxel && _stricmp(t->ID,"Cannon")) continue;
                if(t->Inviso || (t->Voxel?(vxl || !t->MainVoxel.VXL):(shp || !t->Image))) continue;
                auto b=t->CreateBullet(victim,shooter,1,wh,8,false);
                b->SetWeaponType(WeaponTypeClass::Find("105mm"));
                auto at=shooter->GetCoords();at.Z+=300;
                b->MoveTo(at,{2,1,0});
                Point2D pos{200,150};
                RectangleStruct bounds=DSurface::ViewBounds;
                log<<"draw "<<t->ID<<" voxel "<<t->Voxel<<"\n"<<std::flush;
                b->DrawIt(&pos,&bounds);
                if(t->Voxel) vxl=true;else shp=true;
            }
            log<<"saved "<<ScenarioClass::SaveGame("Saved Games\\ProjectileScale.sav",L"ProjectileScale")<<"\n";
        }
    }
    static bool boot=true;
    if(boot) {std::ofstream("probe.log",std::ios::app)<<"boot "<<f<<" base "<<projectileBase.size()<<" shots "<<VisualScale::projectiles.size()<<"\n";boot=false;}
    if(f==28) {
        std::ofstream log("probe.log",std::ios::app);
        for(auto anim:AnimClass::Array) {
            auto saved=VisualScale::explosions.extract(anim->UniqueID);
            if(saved.empty()) continue;
            int native=explosionWidth(anim);
            VisualScale::explosions.insert(std::move(saved));
            int scaled=explosionWidth(anim);
            log<<"explosion widths "<<native<<" "<<scaled<<"\n";
            if(native>0 && std::abs(native*3-scaled)<=2) {log<<"PASS triple explosion width\n";break;}
        }
        for(auto b:BulletClass::Array) if(VisualScale::projectiles.contains(b->UniqueID))
            log<<"live "<<b->Type->ID<<" "<<VisualScale::projectiles.at(b->UniqueID)<<"\n";
    }
    if(f>=150) {
        std::ofstream("probe.log",std::ios::app)<<"completed "<<f<<"\n";
        TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
