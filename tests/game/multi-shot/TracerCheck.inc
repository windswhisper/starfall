void testTracers(UnitClass* primary,UnitClass* nearTarget,UnitClass* sideTarget) {
    auto human=HouseClass::CurrentPlayer;
    auto shooter=static_cast<InfantryClass*>(InfantryTypeClass::Find("E1")->CreateObject(human));
    check(shooter && shooter->Unlimbo(MapClass::Instance.GetCellAt(CellStruct{45,74})->GetCoordsWithBridge(),DirType::North),"place tracer rifle");
    auto rifle=shooter->GetWeapon(0)->WeaponType;
    auto style=ArtSp::Tracers::select(primary->UniqueID,rifle);
    check(style && !style->gradient && style->length==96 && style->speed==192 && style->width==2 && style->color.G==205,"tracer resolves art section");
    grantToUnit(shooter,PassiveGrant{{"fixedshot"},-1});
    MapClass::Instance.Reveal(human);
    auto tactical=TacticalClass::Instance;
    auto previous=tactical->TacticalPos;
    tactical->TacticalPos=TacticalClass::CoordsToScreen(shooter->Location)-Point2D{200,150};
    auto surface=GameCreate<BSurface>(400,300);
    const unsigned frame=Unsorted::CurrentFrame;
    for(int shot=1;shot<=4;++shot) {
        ArtSp::Tracers::clearTrails();
        shooter->SetTarget(primary);
        shooter->PrimaryFacing.SetCurrent(shooter->GetTargetDirection(primary));
        shooter->RearmTimer.Stop();
        shooter->Fire(primary,0);
        for(auto target:{primary,nearTarget,sideTarget}) {
            auto point=tactical->CoordsToClient(target->GetTargetCoords()).first;
            bool lit=false;
            for(unsigned age=0;age<8;++age) {
                surface->Fill(0);
                ArtSp::Tracers::draw(surface,{0,0,400,300},frame+age);
                lit|=surface->GetPixel(&point)!=0;
            }
            if(lit!=(shot==1 || shot==4))
                std::ofstream("multishot.log",std::ios::app)<<"rifle "<<rifle->ID<<" shot "<<shot<<" target "<<target->UniqueID<<" lit "<<lit<<"\n";
            check(lit==(shot==1 || shot==4),"rifle tracer cadence on primary and secondary targets");
        }
    }
    ArtSp::Tracers::expire(frame+8);
    surface->Fill(0);
    ArtSp::Tracers::draw(surface,{0,0,400,300},frame+8);
    auto point=tactical->CoordsToClient(primary->GetTargetCoords()).first;
    check(!surface->GetPixel(&point),"tracer expires");
    // Sample a known 768-lepton path on the same tactical projection as the rifle.
    auto from=MapClass::Instance.GetCellAt(CellStruct{45,74})->GetCoordsWithBridge();
    ArtSp::Tracers::launch(*style,from,from+CoordStruct{768,0,0},frame);
    auto sample=[&](int distance) {
        auto pixel=tactical->CoordsToClient(from+CoordStruct{distance,0,0}).first;
        return surface->GetPixel(&pixel);
    };
    const auto gold=Drawing::RGB_To_Int(style->color);
    ArtSp::Tracers::draw(surface,{0,0,400,300},frame);
    check(sample(192)==gold && !sample(384) && !sample(32),"tracer speed length and head color");
    check(sample(96)==gold && sample(144)==gold,"default tracer has an opaque solid tail");
    surface->Fill(0);
    ArtSp::Tracers::draw(surface,{0,0,400,300},frame+1);
    check(sample(384)==gold && !sample(192),"tracer advances one frame");
    surface->Fill(0);
    auto projected=tactical->CoordsToClient(from+CoordStruct{384,0,0}).first;
    ArtSp::Tracers::draw(surface,{0,0,projected.X,300},frame+1);
    check(!sample(384),"tracer clips to viewport");
    ArtSp::Tracers::clearTrails();
    auto solid=ArtSp::Tracers::select(shooter->UniqueID,WeaponTypeClass::Find("Vulcan2"));
    check(solid && !solid->gradient,"explicit solid ignores unused tail color");
    auto gradient=ArtSp::Tracers::select(shooter->UniqueID,WeaponTypeClass::Find("Vulcan"));
    if(!gradient || !gradient->gradient || gradient->tailColor.B!=255) {
        char binding[80]{},mode[80]{},tail[80]{};
        CCINIClass::INI_Rules->ReadString("Vulcan","Tracer","<missing>",binding);
        CCINIClass::INI_Art.ReadString(binding,"Gradient","<missing>",mode);
        CCINIClass::INI_Art.ReadString(binding,"TailColor","<missing>",tail);
        std::ofstream("multishot.log",std::ios::app)<<"gradient fixture: "<<binding<<" "<<mode<<" "<<tail<<", resolved "<<(gradient!=nullptr)<<" "<<(gradient && gradient->gradient)<<" "<<(gradient?int(gradient->tailColor.B):-1)<<"\n";
    }
    check(gradient && gradient->gradient && gradient->tailColor.B==255,"gradient style reads tail color");
    const auto background=Drawing::RGB_To_Int(16,40,80);
    surface->Fill(background);
    ArtSp::Tracers::launch(*gradient,from,from+CoordStruct{768,0,0},frame);
    ArtSp::Tracers::draw(surface,{0,0,400,300},frame);
    check(sample(192)==gold && sample(96)==background && sample(144)!=background && sample(144)!=gold,"gradient tracer fades into the background");
    ArtSp::Tracers::clearTrails();
    GameDelete(surface);
    tactical->TacticalPos=previous;
    shooter->UnInit();
}
