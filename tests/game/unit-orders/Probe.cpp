#include "../../../src/Starfall.cpp"
#include <Surface.h>
#include <Drawing.h>
#include <WWMouseClass.h>
#include <CCToolTip.h>
#include "../WindowMouse.h"
EXPORT_FUNC(SF_ResearchDraw);
EXPORT_FUNC(SF_PaintCommandBarOrders);
namespace {
int nativeButtonEdges=0;
bool traceVoices=false;
std::vector<std::pair<TechnoClass*,int>> queuedVoices;
void verify(bool ok,const char* msg) {
    std::ofstream("orders.log",std::ios::app)<<(ok?"PASS ":"FAIL ")<<msg<<"\n";
    if(!ok) TerminateProcess(GetCurrentProcess(),2);
}
TechnoClass* placeHost(TechnoTypeClass* type,HouseClass* house,CellStruct at) {
    auto obj=type?static_cast<TechnoClass*>(type->CreateObject(house)):nullptr;
    verify(obj && obj->Unlimbo(MapClass::Instance.GetCellAt(at)->GetCoordsWithBridge(),DirType::North),"placed boarding fixture");
    return obj;
}
void boardingFixture(HouseClass* house) {
    auto ifv=UnitTypeClass::Find("FV");
    auto eng=InfantryTypeClass::Find("ENGINEER");
    auto nearest=placeHost(ifv,house,{55,72});
    auto farther=placeHost(ifv,house,{59,72});
    auto first=placeHost(eng,house,{55,78});
    auto second=placeHost(eng,house,{56,78});
    auto third=placeHost(eng,house,{55,79});
    auto gi=placeHost(InfantryTypeClass::Find("E1"),house,{63,79});
    auto gi2=placeHost(InfantryTypeClass::Find("E1"),house,{64,79});
    auto tank=placeHost(UnitTypeClass::Find("MTNK"),house,{73,80});
    auto post=placeHost(BuildingTypeClass::Find("NABNKR"),house,{63,73});
    auto bunker=placeHost(BuildingTypeClass::Find("NATBNK"),house,{73,73});
    post->ForceMission(Mission::Guard);bunker->ForceMission(Mission::Guard);
    auto button=std::find_if(CommandBarOrders::entries.begin(),CommandBarOrders::entries.end(),[](const auto& e){return e.id=="probeboard";});
    verify(button!=CommandBarOrders::entries.end(),"registered boarding button");
    for(auto unit:{first,second,third})
        verify(UnitOrders::start(unit,"probeboard",nearest->GetMapCoords(),1),"request single transport");
    UnitOrders::tick();
    verify(UnitOrders::orders.at(first->UniqueID).target==nearest && UnitOrders::orders.at(second->UniqueID).target==nearest &&
        !UnitOrders::controlled(third) && third->CurrentMission==Mission::Guard,"two seats assigned, surplus infantry on guard");
    UnitOrders::cancel(first);
    UnitOrders::cancel(second);
    UnitOrders::tick();
    verify(!UnitOrders::controlled(third) && !static_cast<FootClass*>(third)->Destination,"released seats do not reactivate a finished order");
    auto temporary=placeHost(ifv,house,{55,75});
    verify(UnitOrders::start(first,"probeboard",{57,74},6),"request nearby transport");
    UnitOrders::tick();
    verify(UnitOrders::orders.at(first->UniqueID).target==temporary,"nearest transport assigned");
    // UnInit can defer the native destructor. Exercise the next approach check.
    UnitOrders::orders.at(first->UniqueID).nextScan=Unsorted::CurrentFrame;
    temporary->UnInit();
    UnitOrders::tick();
    verify(!UnitOrders::controlled(first) && !static_cast<FootClass*>(first)->Destination && first->CurrentMission==Mission::Guard,
        "lost transport ends the approach despite other free seats");
    for(auto unit:{first,second,third})
        verify(CommandBarOrders::execute(house->ArrayIndex,static_cast<unsigned>(button-CommandBarOrders::entries.begin()),TargetClass(unit).m_ID,{57,72}),"unarmed infantry can use command bar");
    verify(UnitOrders::start(gi,"probegarrison",post->GetMapCoords(),4) &&
        UnitOrders::start(gi2,"probegarrison",post->GetMapCoords(),4) &&
        UnitOrders::start(tank,"probebunker",bunker->GetMapCoords(),4),"start garrison and bunker");
    UnitOrders::tick();
    const auto& orders=UnitOrders::orders;
    auto a=orders.at(first->UniqueID).target,b=orders.at(second->UniqueID).target;
    verify(a==nearest && b==nearest && orders.at(third->UniqueID).target==farther,"transport seats reserved up to capacity");
    verify(orders.at(gi2->UniqueID).target==post,"shared garrison reserved");
    verify(orders.at(gi->UniqueID).target==post && orders.at(tank->UniqueID).target==bunker,"matched garrison and bunker");
    verify(UnitOrders::cancel(tank) && !bunker->ContainsLink(tank),"cancel releases bunker");
    verify(UnitOrders::start(tank,"probebunker",bunker->GetMapCoords(),4),"restart bunker approach");
    UnitOrders::tick();
}
bool boardingFinished(HouseClass* house) {
    static bool secondWave=false;
    int boarded=0;bool occupied=false,bunkered=false;
    UnitClass* partial=nullptr;
    BuildingClass* post=nullptr;
    for(auto t:UnitClass::Array) if(t->Owner==house && t->Type==UnitTypeClass::Find("FV")) {
        boarded+=t->Passengers.NumPassengers;
        if(t->Passengers.NumPassengers==1) partial=t;
    }
    for(auto b:BuildingClass::Array) if(b->Owner==house) {
        if(b->Type==BuildingTypeClass::Find("NABNKR") && b->Occupants.Count==(secondWave?3:2)) {occupied=true;post=b;}
        if(b->Type==BuildingTypeClass::Find("NATBNK") && b->TankBunkerState==TankBunkerState::Bunkered) bunkered=true;
    }
    for(const auto& [id,o]:UnitOrders::orders)
        if(o.id=="probeboard" || o.id=="probegarrison" || o.id=="probebunker") return false;
    if(!secondWave && boarded==3 && partial && occupied && bunkered) {
        secondWave=true;
        auto engineer=placeHost(InfantryTypeClass::Find("ENGINEER"),house,{56,78});
        auto soldier=placeHost(InfantryTypeClass::Find("E1"),house,{63,79});
        verify(UnitOrders::start(engineer,"probeboard",{57,72},6) &&
               UnitOrders::start(soldier,"probegarrison",post->GetMapCoords(),4),"send reinforcements");
        UnitOrders::tick();
        verify(UnitOrders::orders.at(engineer->UniqueID).target==partial &&
               UnitOrders::orders.at(soldier->UniqueID).target==post,"partially occupied hosts accept reinforcements");
        return false;
    }
    return secondWave && boarded==4 && occupied && bunkered;
}
bool cast(UnitClass* ship,const string& id,CellStruct center) {
    auto& specs=ActiveSkills::specs;
    auto it=std::find_if(specs.begin(),specs.end(),[&](const auto& s){return s.id==id;});
    return it!=specs.end() && ActiveSkills::execute(ship->Owner->ArrayIndex,static_cast<int>(it-specs.begin()),ship->UniqueID,0,center);
}
void click(Point2D p) {
    nativeButtonEdges=0;
    verify(WindowMouse::button(p,1) && WindowMouse::button(p,4) && nativeButtonEdges==0,"HUD consumed both clicks");
}
bool tracingRanges=false;
std::vector<std::pair<float,bool>> drawnRanges;
MouseCursorType cursorAt(Point2D point) {
    WWMouseClass::Instance->XY1=point;
    drawnRanges.clear();tracingRanges=true;
    SF_ResearchDraw(nullptr);
    tracingRanges=false;
    return MouseClass::Instance.MouseCursorIndex;
}
void checkNativeCursorRequests(Point2D aim,Point2D bar,MouseCursorType expected) {
    auto& mouse=MouseClass::Instance;
    auto pointer=WWMouseClass::Instance;
    int frame=pointer->ImageFrameIndex;
    auto stable=[&] {return mouse.MouseCursorIndex==expected && !mouse.MouseCursorIsMini &&
        pointer->ImageFrameIndex==frame;};
    bool held=true;
    for(int i=0;i<3;++i) {
        mouse.SetCursor(MouseCursorType::Move,false);held&=stable();
        mouse.RestoreCursor();held&=stable();
        mouse.UpdateCursor(MouseCursorType::NoMove,true);held&=stable();
        mouse.UpdateCursorMinimapState(true);held&=stable();
    }
    verify(held,"order cursor survives native requests between HUD draws");
    pointer->XY1=bar;
    mouse.UpdateCursor(MouseCursorType::Default,false);
    verify(mouse.MouseCursorIndex==MouseCursorType::Default,"native cursor available outside tactical view");
    pointer->XY1=aim;
    mouse.UpdateCursor(MouseCursorType::Move,false);
    verify(mouse.MouseCursorIndex==expected,"order cursor resumes on map without HUD redraw");
}
bool nukeRange(int radius) {
    if(drawnRanges.size()!=static_cast<size_t>(2*radius+1)) return false;
    for(int i=0;i<=2*radius;++i) if(drawnRanges[i]!=std::pair{0.5f*i,false}) return false;
    return true;
}
bool capturedKey(WPARAM vk) {
    int tail=WWKeyboardClass::Instance->Tail;
    auto wnd=reinterpret_cast<WNDPROC>(0x7775C0);
    wnd(Game::hWnd,WM_KEYDOWN,vk,1);
    wnd(Game::hWnd,WM_KEYUP,vk,0xC0000001);
    return WWKeyboardClass::Instance->Tail==tail;
}
void checkEnterInput(HouseClass* house,Point2D aim,const RectangleStruct& cell,int boardSlot) {
    auto soldier=placeHost(InfantryTypeClass::Find("ENGINEER"),house,{83,59});
    ObjectClass::CurrentObjects.Clear();soldier->Select();
    const Point2D board{cell.X+cell.Width*boardSlot+cell.Width/2,cell.Y+cell.Height/2};
    const Point2D deploy{cell.X+3*cell.Width+cell.Width/2,board.Y};
    auto mouse=WWMouseClass::Instance;
    const auto old=mouse->XY1;
    auto cursor=[&] {return cursorAt(aim);};
    verify(UnitOrders::start(soldier,"probeboard",soldier->GetMapCoords(),6),"existing auto enter area");
    queuedVoices.clear();traceVoices=true;
    click(board);
    verify(cursor()==MouseCursorType::Enter,"auto enter uses native enter cursor");
    checkNativeCursorRequests(aim,board,MouseCursorType::Enter);
    verify(drawnRanges==std::vector{std::pair{6.0f,true}},"auto enter native building range");
    click(deploy);
    verify(capturedKey('S') && cursor()==MouseCursorType::Enter,"native button and stop hotkey suppressed while aiming");
    verify(WindowMouse::button(deploy,16) && WindowMouse::button(deploy,64) &&
        cursor()!=MouseCursorType::Enter,"right click outside map cancels without native dispatch");
    click(board);
    verify(capturedKey(VK_ESCAPE) && cursor()!=MouseCursorType::Enter,"escape cancels auto enter");
    const auto& view=DSurface::ViewBounds;
    click({view.X+40,view.Y+view.Height-40});
    verify(cursor()==MouseCursorType::Enter && WindowMouse::button(aim,16) && WindowMouse::button(aim,64) &&
        cursor()!=MouseCursorType::Enter,"area skill uses the same auto enter targeting");
    click(board);
    int before=EventClass::OutList.Count;
    verify(queuedVoices.empty(),"aim and cancel issue no unit voice");
    click(aim);
    verify(EventClass::OutList.Count==before+1 && cursor()!=MouseCursorType::Enter,"confirm sends one order and restores cursor");
    verify(drawnRanges.empty() && UnitOrders::controlled(soldier),"auto enter range hidden after confirmation");
    verify(WindowMouse::send(aim,WM_LBUTTONDBLCLK) && WindowMouse::button(aim,4) &&
        EventClass::OutList.Count==before+1,"confirm double click cannot issue native order");
    traceVoices=false;
    verify(queuedVoices.size()==1 && queuedVoices.front().first==soldier &&
        soldier->GetTechnoType()->VoiceMove.FindItemIndex(queuedVoices.front().second)>=0,"auto enter queues one native move voice");
    mouse->XY1=old;
    soldier->UnInit();
}
unsigned checkNativeTip(const char* button,const wchar_t* expected) {
    auto manager=CCToolTip::Instance;
    auto cell=*reinterpret_cast<const RectangleStruct* const*>(0xB0FC68);
    Point2D center{cell->X+cell->Width*CommandBarOrders::listedSlots.at(button)+cell->Width/2,
        *reinterpret_cast<const int*>(0xB0CC24)+cell->Height/2};
    auto tip=manager->FindFromPosition(center);
    verify(tip!=nullptr,"native tooltip hit region");
    manager->CurrentToolTip=tip;
    manager->CurrentMousePosition=center;
    verify(manager->Process() && std::wstring_view(manager->CurrentToolTipData.HelpText)==expected,
        "native tooltip resolves button text");
    manager->Draw(false);
    manager->Hide();
    return tip->GadgetID;
}
void checkOpeningBar() {
    const auto cell=*reinterpret_cast<const RectangleStruct* const*>(0xB0FC68);
    auto surface=DSurface::Composite;
    auto bounds=surface->GetRect();
    for(const auto key:{"areaattack","testboard"}) {
        const int x=cell->X+CommandBarOrders::listedSlots.at(key)*cell->Width;
        const int y=*reinterpret_cast<const int*>(0xB0CC24);
        const std::string filename=std::string(key)+".shp";
        SHPReference reference(filename.c_str());
        auto glyph=reference.GetData();
        RectangleStruct ref{x,y-cell->Height,cell->Width,cell->Height};
        const WORD marker=0xF81F;
        surface->FillRect(&ref,marker);
        Point2D at{ref.X+(ref.Width-glyph->Width)/2,ref.Y+(ref.Height-glyph->Height)/2};
        surface->DrawSHP(FileSystem::SIDEBAR_PAL,glyph,0,&at,&bounds,BlitterFlags::None,
            0,0,ZGradient::Ground,1000,0,nullptr,0,0,0);
        auto data=static_cast<const BYTE*>(surface->Lock(x,ref.Y));
        int ink=0,matched=0;
        for(int row=0;row<cell->Height;++row) {
            auto expected=reinterpret_cast<const WORD*>(data+row*surface->GetPitch());
            auto actual=reinterpret_cast<const WORD*>(data+(row+cell->Height)*surface->GetPitch());
            for(int col=0;col<cell->Width;++col) if(expected[col]!=marker) {
                ++ink;matched+=actual[col]==expected[col];
            }
        }
        surface->Unlock();
        verify(ink>5 && matched==ink,"custom button visible before first input");
    }
}
void captureBar(Point2D pointer,int left,int top,int width,int height) {
    auto mouse=WWMouseClass::Instance;
    auto previous=mouse->XY1;
    mouse->XY1=pointer;
    GScreenClass::Instance.Render();
    mouse->XY1=pointer;
    auto surface=DSurface::Composite;
    auto cell=*reinterpret_cast<const RectangleStruct* const*>(0xB0FC68);
    auto bounds=surface->GetRect();
    RectangleStruct ref{cell->X,cell->Y-2*cell->Height,cell->Width,cell->Height};
    const WORD marker=0xF81F;
    surface->FillRect(&ref,marker);
    SHPReference guard("button06.shp");
    auto shape=guard.GetData();
    verify(shape && shape->Frames>0,"stock Guard SHP available");
    Point2D refPos{ref.X+(ref.Width-shape->Width)/2,ref.Y+(ref.Height-shape->Height)/2};
    surface->DrawSHP(FileSystem::SIDEBAR_PAL,shape,0,&refPos,&bounds,BlitterFlags::None,
        0,0,ZGradient::Ground,1000,0,nullptr,0,0,0);
    auto pixels=static_cast<const BYTE*>(surface->Lock(left,top));
    verify(pixels!=nullptr,"command bar surface");
    std::ofstream out("command-bar.ppm",std::ios::binary);
    out<<"P6\n"<<width<<" "<<height<<"\n255\n";
    int pitch=surface->GetPitch();
    auto row=reinterpret_cast<const WORD*>(pixels+(pointer.Y-top)*pitch);
    auto centerX=[&](const char* key) {return cell->X+CommandBarOrders::listedSlots.at(key)*cell->Width+cell->Width/2-left;};
    const int stockLeft=centerX("iconprobe")-cell->Width/2;
    int painted=0,matched=0;
    for(int y=0;y<ref.Height;++y) {
        auto expected=reinterpret_cast<const WORD*>(pixels+(ref.Y-top+y)*pitch)+ref.X-left;
        auto actual=reinterpret_cast<const WORD*>(pixels+(cell->Y-top+y)*pitch)+stockLeft;
        for(int x=0;x<ref.Width;++x) if(expected[x]!=marker) {++painted;matched+=actual[x]==expected[x];}
    }
    verify(painted>5 && painted==matched,"explicit MIX SHP overrides same-name file");
    for(auto name:{"missingprobe","automissingprobe"}) {
        auto x=centerX(name);
        verify(row[x]!=row[x+4],"missing SHP draws built-in reticle");
    }
    std::ifstream log("Starfall.log");
    int warnings=0,implicitWarnings=0;
    for(std::string line;std::getline(log,line);) {
        if(line.find("CommandBar.Image=nonexistentprobe.shp")!=std::string::npos) ++warnings;
        if(line.find("CommandBar.Image=automissingprobe.shp")!=std::string::npos) ++implicitWarnings;
    }
    verify(implicitWarnings==0,"implicit fallback stays quiet");
    verify(warnings==1,"explicit missing image logged once");
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        WORD pixel=reinterpret_cast<const WORD*>(pixels+y*pitch)[x];
        for(auto [shift,mask]:{std::pair{int(Drawing::RedShiftLeft),255>>Drawing::RedShiftRight},
            std::pair{int(Drawing::GreenShiftLeft),255>>Drawing::GreenShiftRight},std::pair{int(Drawing::BlueShiftLeft),255>>Drawing::BlueShiftRight}})
            out.put(static_cast<char>(((pixel>>shift)&mask)*255/mask));
    }
    surface->Unlock();
    for(auto key:{"areaattack","testboard"}) {
        RectangleStruct slot{centerX(key)+left-cell->Width/2,cell->Y,cell->Width,cell->Height};
        surface->FillRect(&slot,marker);
        auto previousSurface=DSurface::Temp;
        DSurface::Temp=surface;
        SF_PaintCommandBarOrders(nullptr);
        DSurface::Temp=previousSurface;
        surface->FillRect(&ref,marker);
        std::string filename=std::string(key)+".shp";
        SHPReference reference(filename.c_str());
        auto glyph=reference.GetData();
        verify(glyph && glyph->Frames==2,"two-frame command glyph available");
        Point2D pos{ref.X+(ref.Width-glyph->Width)/2,ref.Y+(ref.Height-glyph->Height)/2};
        surface->DrawSHP(FileSystem::SIDEBAR_PAL,glyph,slot.X+slot.Width/2==pointer.X?1:0,
            &pos,&bounds,BlitterFlags::None,0,0,ZGradient::Ground,1000,0,nullptr,0,0,0);
        auto data=static_cast<const BYTE*>(surface->Lock(0,0));
        verify(data!=nullptr,"transparent button surface");
        int inkPixels=0;
        bool identical=true;
        for(int y=0;y<slot.Height;++y) {
            auto actual=reinterpret_cast<const WORD*>(data+(slot.Y+y)*pitch)+slot.X;
            auto expected=reinterpret_cast<const WORD*>(data+(ref.Y+y)*pitch)+ref.X;
            for(int x=0;x<slot.Width;++x) {
                inkPixels+=expected[x]!=marker;
                identical&=expected[x]==actual[x];
            }
        }
        surface->Unlock();
        verify(inkPixels>0 && inkPixels<slot.Width*slot.Height/2 && identical,"same-name SHP visible");
    }
    mouse->XY1=previous;
}
DWORD otherID=0,first=0;
std::set<DWORD> launched;
bool sawChange=false;
}
DEFINE_HOOK(0x708D90,SF_TestOrderVoiceQueue,5) {
    if(traceVoices) queuedVoices.emplace_back(R->ECX<TechnoClass*>(),R->Stack<int>(4));
    return 0;
}
// First native dispatch instruction after the HUD's early-consume branch.
DEFINE_HOOK(0x77762F,SF_TestCommandBarNativeInput,5) {
    if(WindowMouse::tracing) {
        auto msg=R->Stack<UINT>(0x18);
        if(msg==WM_LBUTTONDOWN || msg==WM_LBUTTONUP) ++nativeButtonEdges;
    }
    return 0;
}
DEFINE_HOOK(0x456980,SF_TestOrderRadialDraw,6) {
    if(tracingRanges && DSurface::Temp==DSurface::Composite)
        drawnRanges.emplace_back(R->Stack<float>(0x14),R->Stack<BYTE>(0x1C)!=0);
    return 0;
}
DEFINE_HOOK(0x55B4EB,SF_TestAreaAttack,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES) return 0;
    int frame=Unsorted::CurrentFrame;
    if(frame<15) return 0;
    const bool load=GetFileAttributesA("reload.flag")!=INVALID_FILE_ATTRIBUTES;
    auto house=HouseClass::FindByPlayerAt(HouseClass::PlayerAtA);
    UnitClass* ship=nullptr;
    for(auto unit:UnitClass::Array) if(unit->Owner==house && !_stricmp(unit->Type->ID,"DRED")) ship=unit;
    static bool checkedSave=false;
    if(!checkedSave && (!ship || !ship->SpawnManager)) verify(false,"dreadnought present");
    CellStruct center{70,61};
    auto& orders=UnitOrders::orders;
    if(load && checkedSave) {
        if(boardingFinished(house)) {verify(true,"both transports full, three garrisoned and tank bunkered after load");TerminateProcess(GetCurrentProcess(),0);}
        if(frame%100==0) {
            std::ofstream log("orders.log",std::ios::app);
            for(auto& [id,o]:orders) if(o.id.starts_with("probe"))
                log<<frame<<" "<<o.id<<" "<<id<<" mission "<<int(o.unit->CurrentMission)<<" pos "<<o.unit->GetMapCoords().X<<","<<o.unit->GetMapCoords().Y
                    <<" target "<<(o.target?o.target->GetTechnoType()->ID:"none")<<"\n";
            for(auto b:BuildingClass::Array) if(b->Owner==house && (b->Type->Bunker || b->Type->CanBeOccupied))
                log<<b->Type->ID<<" occupants "<<b->Occupants.Count<<" bunker "<<int(b->TankBunkerState)<<"\n";
            for(auto t:UnitClass::Array) if(t->Type==UnitTypeClass::Find("FV")) log<<"FV "<<t->Passengers.NumPassengers<<"\n";
        }
        if(frame>1000) verify(false,"auto enter timed out");
        return 0;
    }
    if(load) {
        checkedSave=true;
        verify(orders.contains(ship->UniqueID) && orders.at(ship->UniqueID).center==center && orders.at(ship->UniqueID).radius==6 &&
               orders.at(ship->UniqueID).target && orders.at(ship->UniqueID).target==ship->Target,"restored area");
        auto p=ship->GetMapCoords();++p.X;
        EventClass ev(house->ArrayIndex,TargetClass(ship),Mission::Move,TargetClass{},TargetClass(p),TargetClass{});
        reinterpret_cast<void(__thiscall*)(EventClass*)>(0x4C6CB0)(&ev);
        verify(!UnitOrders::controlled(ship),"player move cancels");
        verify(cast(ship,"orderprobe",center) && !UnitOrders::controlled(ship),"Lua entry points");
        verify(cast(ship,"dreadnoughtbombard",center),"restart after cancel");
        auto& liveOrder=orders.at(ship->UniqueID);
        auto& definition=UnitOrders::definitions.at(liveOrder.id);
        for(auto ally:UnitClass::Array) if(ally->Owner==house) verify(!UnitOrders::eligible(liveOrder,definition,ally),"ally excluded");
        auto enemy=HouseClass::FindByPlayerAt(HouseClass::PlayerAtB);
        verify(enemy && ship->SetOwningHouse(enemy),"transfer ownership");
        UnitOrders::tick();
        verify(!UnitOrders::controlled(ship),"ownership change cancels");
        const auto cell=*reinterpret_cast<const RectangleStruct* const*>(0xB0FC68);
        for(int slot:{3,5}) {
            Point2D p{cell->X+(2*slot+1)*cell->Width/2,cell->Y+cell->Height/2};
            nativeButtonEdges=0;
            WindowMouse::button(p,1);WindowMouse::button(p,4);
            verify(nativeButtonEdges==2,slot==3?"deploy clicks forwarded to native bar":"guard clicks forwarded to native bar");
        }
        return 0;
    }
    if(frame==15) {
        checkOpeningBar();
        checkNativeTip("testboard",L"进入载具 Move");
        const auto areaTip=checkNativeTip("areaattack",L"区域锁定打击 · 6 格");
        checkNativeTip("iconprobe",StringTable::LoadString("Tip:Guard"));
        UnitClass* other=nullptr;
        for(auto unit:UnitClass::Array) if(unit!=ship) {
            unit->Health=unit->EstimatedHealth=20000;
            if(!other && unit->Owner==house && !_stricmp(unit->Type->ID,"MTNK")) other=unit;
            else unit->Deactivate();
        }
        verify(other && ActiveSkills::held(other).empty(),"ordinary tank has no active skill");
        otherID=other->UniqueID;
        auto cell=*reinterpret_cast<const RectangleStruct* const*>(0xB0FC68);
        auto occupied=reinterpret_cast<const int*>(0xB0CB78);
        auto parse=reinterpret_cast<bool(__thiscall*)(RulesClass*,CCINIClass*,bool)>(0x674650);
        auto nativeID=reinterpret_cast<int(__fastcall*)(const char*)>(0x6CFCC0);
        auto& positions=CommandBarOrders::listedSlots;
        verify(positions.contains("areaattack") && positions.at("areaattack")==4,"solo ButtonList sets exact slot");
        verify(occupied[nativeID("Deploy")]==3 && occupied[nativeID("Guard")]==5,"native buttons flank AreaAttack");
        parse(RulesClass::Instance,&CCINIClass::INI_UIMD,true);
        verify(!positions.contains("areaattack") && occupied[nativeID("Guard")]==4,"omitted multiplayer button stays hidden");
        GScreenClass::Instance.Render();
        ToolTip removed;
        verify(!CCToolTip::Instance->Find(areaTip,removed),"removed button has no native tooltip");
        CCINIClass alternate;
        alternate.WriteString("MultiplayerAdvancedCommandBar","ButtonList","x,Guard,AreaAttack,Deploy");
        parse(RulesClass::Instance,&alternate,true);
        verify(positions.contains("areaattack") && positions.at("areaattack")==2 &&
            occupied[nativeID("Guard")]==1 && occupied[nativeID("Deploy")]==3,"multiplayer list controls its own order");
        parse(RulesClass::Instance,&CCINIClass::INI_UIMD,false);
        CCINIClass unrelated;
        parse(RulesClass::Instance,&unrelated,false);
        verify(positions.contains("areaattack") && positions.at("areaattack")==4,"missing INI section preserves native layout");
        const int next=positions.at("areaattack");
        Point2D button{cell->X+cell->Width*next+cell->Width/2,cell->Y+cell->Height/2};
        CellStruct empty{84,59};
        auto coord=MapClass::Instance.GetCellAt(empty)->GetCoordsWithBridge();
        auto tactical=TacticalClass::Instance;
        auto off=tactical->CoordsToScreen(coord);
        tactical->TacticalPos={off.X-DSurface::ViewBounds.Width/2,off.Y-DSurface::ViewBounds.Height/2};
        auto aim=tactical->CoordsToClient(coord).first;
        checkEnterInput(house,aim,*cell,positions.at("testboard"));
        ObjectClass::CurrentObjects.Clear();ship->Select();other->Select();
        const auto& view=DSurface::ViewBounds;
        click({view.X+40,view.Y+view.Height-40});
        verify(cursorAt(aim)==MouseCursorType::Attack && nukeRange(6) && capturedKey(VK_ESCAPE) &&
            cursorAt(aim)!=MouseCursorType::Attack,"area attack ability cursor and escape");
        click(button);
        verify(cursorAt(aim)==MouseCursorType::Attack,"area attack button uses native attack cursor");
        checkNativeCursorRequests(aim,button,MouseCursorType::Attack);
        verify(nukeRange(6),"area attack native nuke rings span configured radius");
        click({cell->X+3*cell->Width+cell->Width/2,button.Y});
        verify(capturedKey('S') && cursorAt(aim)==MouseCursorType::Attack,"area attack captures deploy and stop");
        verify(WindowMouse::button(aim,16) && WindowMouse::button(aim,64),"right click cancels targeting");
        verify(!UnitOrders::controlled(other),"cancel emits no unit order");
        click(button);
        captureBar(button,0,cell->Y-72,std::min(DSurface::WindowBounds.Width,cell->X+(positions.at("testboard")+1)*cell->Width),cell->Height+72);
        auto count=EventClass::OutList.Count;
        queuedVoices.clear();traceVoices=true;
        click(aim);
        verify(WindowMouse::send(aim,WM_LBUTTONDBLCLK) && WindowMouse::button(aim,4) &&
            EventClass::OutList.Count==count+2 && cursorAt(aim)!=MouseCursorType::Attack,"one area attack per selected unit after double click");
        traceVoices=false;
        verify(queuedVoices.size()==1,"group area attack queues exactly one native voice");
        auto [speaker,sound]=queuedVoices.front();
        auto type=speaker->GetTechnoType();
        verify((speaker==ship || speaker==other) && sound>=0 &&
            (type->VoiceAttack.FindItemIndex(sound)>=0 || type->VoicePrimaryWeaponAttack==sound),
            "area attack queues selected unit attack voice");
    }
    if(frame==35) {
        verify(orders.contains(otherID) && orders.contains(ship->UniqueID) &&
            orders.at(otherID).radius==CommandBarOrders::entries.at(0).radius &&
            orders.at(otherID).center==orders.at(ship->UniqueID).center,"command bar orders ordinary units and groups");
        auto mouse=WWMouseClass::Instance->XY1;
        cursorAt(mouse);
        verify(drawnRanges.empty(),"area attack range stays hidden after dispatch");
        ship->Deselect();cursorAt(mouse);
        verify(ship->Select(),"reselect caster");cursorAt(mouse);
        verify(nukeRange(orders.at(ship->UniqueID).radius),"reselecting unit shows its current order area");
    }
    if(frame==40) {
        verify(UnitOrders::controlled(ship) && !ship->Target && ship->CurrentMission==Mission::Sleep,"empty area");
        verify(cast(ship,"dreadnoughtbombard",center),"replace area");
    }
    if(frame>40 && orders.contains(ship->UniqueID)) {
        auto& order=orders.at(ship->UniqueID);
        if(order.target && !first) first=order.target->UniqueID;
        for(auto node:ship->SpawnManager->SpawnedNodes) if(node && node->Unit &&
            (node->Status==SpawnNodeStatus::TakeOff || node->Status==SpawnNodeStatus::Attacking)) launched.insert(node->Unit->UniqueID);
        if(order.target && order.target->UniqueID!=first && !sawChange) {
            verify(launched.size()>=2,"real missile salvo");sawChange=true;
            verify(true,"acquired another target");
        }
        if(sawChange) {
            boardingFixture(house);
            verify(ScenarioClass::SaveGame("Saved Games\\UnitOrders.sav",L"Unit Orders"),"saved");
            TerminateProcess(GetCurrentProcess(),0);
        }
    }
    if(frame%50==0) {
        std::ofstream log("orders.log",std::ios::app);
        log<<"frame "<<frame<<" at "<<ship->GetMapCoords().X<<","<<ship->GetMapCoords().Y<<" target "<<first<<" launches "<<launched.size()<<" mission "<<int(ship->CurrentMission)<<" nodes ";
        for(auto n:ship->SpawnManager->SpawnedNodes) if(n) log<<int(n->Status)<<" ";
        log<<"\n";
    }
    if(frame>650) verify(false,"two shots and retarget within 650 frames");
    return 0;
}
