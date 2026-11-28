void testSplitBeams(UnitClass* firer,UnitClass* primary,UnitClass* first,UnitClass* second) {
    namespace beams=ArtSp::ContinuousBeam;
    auto tactical=TacticalClass::Instance;
    const auto camera=tactical->TacticalPos;
    tactical->TacticalPos=TacticalClass::CoordsToScreen(firer->Location)-Point2D{200,150};
    auto surface=GameCreate<BSurface>(400,300);
    auto redraw=[&] {
        beams::tick();
        surface->Fill(0);
        beams::draw(surface,{0,0,400,300},Unsorted::CurrentFrame+3);
    };
    auto lit=[&](TechnoClass* end,int height=0) {
        auto center=tactical->CoordsToClient(end->GetTargetCoords()+CoordStruct{0,0,height}).first;
        for(int dx=-2;dx<=2;++dx) for(int dy=-2;dy<=2;++dy) {
            auto at=center+Point2D{dx,dy};
            if(surface->GetPixel(&at)) return true;
        }
        return false;
    };
    auto mount=firer->GetWeapon(0);
    auto original=mount->WeaponType;
    for(auto id:{"PrismShot","CoilBolt","RadBeamWeapon"}) {
        mount->WeaponType=WeaponTypeClass::Find(id);
        firer->SetTarget(primary);
        firer->RearmTimer.Stop();
        auto lasers=laserEnds.size();
        auto bolts=EBolt::Array.Count,rads=RadBeam::Array.Count;
        firer->Fire(primary,0);
        if(FireOrigin::enabled(mount->WeaponType)) {
            std::set<DWORD> branches;
            for(auto bullet:BulletClass::Array) if(bullet->WeaponType==mount->WeaponType && bullet->Owner==firer) {
                for(auto end:{primary,first,second})
                    if(bullet->Target==end && bullet->SourceCoords==end->GetTargetCoords()+CoordStruct{0,0,512})
                        branches.insert(end->UniqueID);
            }
            check(branches.size()==3,"each split round launches above its own target");
        }
        auto height=first->GetHeight();
        first->SetHeight(height+128);
        redraw();
        check(lit(primary) && lit(first) && lit(second),"three persistent beam endpoints");
        if(FireOrigin::enabled(mount->WeaponType))
            check(lit(primary,512) && lit(first,512) && lit(second,512),"split beams follow their individual overhead origins");
        check(lasers==laserEnds.size() && bolts==EBolt::Array.Count && rads==RadBeam::Array.Count,"split beams have no native duplicates");
        first->CloakState=CloakState::Cloaked;
        redraw();
        check(lit(primary) && !lit(first) && lit(second),"losing one beam leaves the others connected");
        first->CloakState=CloakState::Uncloaked;
        redraw();
        check(!lit(first),"a lost beam waits for a shot");
        first->SetHeight(height);
        firer->RearmTimer.Stop();
        firer->Fire(primary,0);
        first->SetHeight(height+128);
        redraw();
        check(lit(first),"next volley restores the split beam");
        first->SetHeight(height);
        beams::stop(firer);
    }
    mount->WeaponType=original;
    dispatch();
    GameDelete(surface);
    tactical->TacticalPos=camera;
}
void testContinuousBeams(UnitClass* firer,UnitClass* target) {
    namespace beams=ArtSp::ContinuousBeam;
    MapClass::Instance.Reveal(firer->Owner);
    auto tactical=TacticalClass::Instance;
    const auto camera=tactical->TacticalPos;
    tactical->TacticalPos=TacticalClass::CoordsToScreen(firer->Location)-Point2D{200,150};
    auto image=GameCreate<BSurface>(400,300);
    auto render=[&](unsigned frame) {
        image->Fill(0);
        beams::draw(image,{0,0,400,300},frame);
        auto ptr=static_cast<const BYTE*>(image->Lock(0,0));
        unsigned hash=0;
        for(int y=0;y<300;++y) {
            auto row=reinterpret_cast<const WORD*>(ptr+y*image->GetPitch());
            for(int x=0;x<400;++x) hash=hash*31+row[x];
        }
        image->Unlock();
        return hash;
    };
    auto mount=firer->GetWeapon(0);
    const auto original=mount->WeaponType;
    const unsigned frame=Unsorted::CurrentFrame;
    for(auto id:{"PrismShot","CoilBolt","RadBeamWeapon"}) {
        auto weapon=WeaponTypeClass::Find(id);
        check(weapon!=nullptr,"beam weapon fixture");
        mount->WeaponType=weapon;
        firer->SetTarget(target);
        firer->RearmTimer.Stop();
        firer->PrimaryFacing.SetCurrent(firer->GetTargetDirection(target));
        firer->SecondaryFacing.SetCurrent(firer->GetTargetDirection(target));
        check(!render(frame),"aim alone creates no continuous beam");
        auto lasers=laserEnds.size();
        int bolts=EBolt::Array.Count,rays=RadBeam::Array.Count;
        firer->Fire(target,0);
        check(firer->RearmTimer.GetTimeLeft()>0,"beam weapon commits its native shot");
        check(lasers==laserEnds.size() && bolts==EBolt::Array.Count && rays==RadBeam::Array.Count,"no duplicate native beam");
        beams::tick();
        auto first=render(frame+2),later=render(frame+5);
        check(first && later && ((first==later)==weapon->IsLaser),"beam persists and animates during reload");
        ProjectileShield::active.push_back({target,{1,0,0,true}});
        beams::tick();
        auto shielded=render(frame+5);
        check(shielded && shielded!=later,"continuous beam shortens when the target gains a shield");
        ProjectileShield::forget(target);
        beams::tick();
        check(render(frame+5)==later,"continuous beam reaches the target again when its shield ends");
        int height=target->GetHeight();
        target->SetHeight(height+128);
        beams::tick();
        check(render(frame+5)!=later,"beam endpoint follows moving target");
        target->SetHeight(height);
        firer->RearmTimer.Stop();
        firer->Fire(target,0);
        beams::tick();
        check(render(frame+5)==later,"repeat shot keeps one beam");
        const auto range=weapon->Range;
        weapon->Range=1;
        beams::tick();
        weapon->Range=range;
        check(!render(frame+5),"leaving range breaks beam until another shot");
        firer->RearmTimer.Stop();
        firer->Fire(target,0);
        beams::tick();
        check(render(frame+5)!=0,"next actual shot restores beam");
        firer->SetTarget(nullptr);
        beams::tick();
        check(!render(frame+5),"lost target removes continuous beam");
    }
    mount->WeaponType=original;
    dispatch();
    tactical->TacticalPos=camera;
    GameDelete(image);
}
