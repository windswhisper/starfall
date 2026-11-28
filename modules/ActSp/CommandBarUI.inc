// The native Advanced Command Bar lays out physical cells independently of command IDs.
// Use its live skin geometry, visibility and occupied positions, including after a resize.
namespace Bar {
struct Slot {int order;RectangleStruct bounds;};
std::vector<Slot> slots() {
    std::vector<Slot> result;
    if(CommandBarOrders::listedSlots.empty() || CommandBarOrders::entries.empty() || !*reinterpret_cast<const bool*>(0x87F7E8+0x5544)) return result;
    const auto cell=*reinterpret_cast<const RectangleStruct* const*>(0xB0FC68);
    const int count=*reinterpret_cast<const int*>(0xB0F9E4);
    const auto occupied=reinterpret_cast<const int*>(0xB0CB78);
    if(!cell || cell->Width<16 || cell->Height<16 || count<1 || count>256) return result;
    for(int i=0;i<static_cast<int>(CommandBarOrders::entries.size());++i) {
        auto slot=CommandBarOrders::listedSlots.find(CommandBarOrders::entries[i].button);
        if(slot==CommandBarOrders::listedSlots.end()) continue;
        int pos=slot->second;
        if(pos<0 || pos>=count || std::find(occupied,occupied+25,pos)!=occupied+25) continue;
        RectangleStruct r{cell->X+pos*cell->Width,*reinterpret_cast<const int*>(0xB0CC24),cell->Width,cell->Height};
        if(r.X>=0 && r.Y>=0 && r.X+r.Width<=DSurface::WindowBounds.Width && r.Y+r.Height<=DSurface::WindowBounds.Height)
            result.push_back({i,r});
    }
    return result;
}
int hit(Point2D p) {
    const auto buttons=slots();
    for(const auto& slot:buttons) if(inside(slot.bounds,p)) return slot.order;
    return -1;
}
TechnoClass* eligibleSelection(ObjectClass* object,int index) {
    if(!object || (object->AbstractFlags&AbstractFlags::Techno)==AbstractFlags::None) return nullptr;
    auto unit=static_cast<TechnoClass*>(object);
    return CommandBarOrders::eligible(unit,HouseClass::CurrentPlayer,
        CommandBarOrders::entries[index].enterKinds)?unit:nullptr;
}
bool hasSelection(int index) {
    for(auto object:ObjectClass::CurrentObjects) if(eligibleSelection(object,index)) return true;
    return false;
}
std::vector<TechnoClass*> selection(int index) {
    std::vector<TechnoClass*> units;
    if(index<0 || index>=static_cast<int>(CommandBarOrders::entries.size())) return units;
    for(auto object:ObjectClass::CurrentObjects) {
        if(auto unit=eligibleSelection(object,index)) units.push_back(unit);
    }
    std::sort(units.begin(),units.end(),[](auto a,auto b){return a->UniqueID<b->UniqueID;});
    return units;
}
void cancel() {ui.barAiming=-1;ui.barPressed=nothing;}
bool input(Point2D p,int flags) {
    if(CommandBarOrders::entries.empty()) return false;
    int index=hit(p);
    if((flags&16) && ui.barAiming>=0) {cancel();return true;}
    if(index<0 && ui.barAiming>=0 && inside(DSurface::ViewBounds,p)) index=terrain;
    if(index<0 && index!=terrain && ui.barPressed==nothing) return false;
    if(flags&1) ui.barPressed=index;
    if(flags&4) {
        if(index==ui.barPressed && index!=nothing) {
            auto units=selection(index>=0?index:ui.barAiming);
            if(index>=0) {
                ui.aiming=-1;
                ui.barAiming=units.empty() || ui.barAiming==index?-1:index;
                if(ui.barAiming>=0) playClick();
            } else if(ui.barAiming>=0) {
                const auto destination=pick(p);
                if(ActiveSkills::playable(destination.cell) && !units.empty()) {
                    for(auto unit:units) ui.unsent.emplace_back(ui.house->ArrayIndex,EventType::Place,
                        static_cast<AbstractType>(CommandBarOrders::eventTag|ui.barAiming),TargetClass(unit).m_ID,0,destination.cell);
                    ui.showSelectedOrderAreas=false;
                    acknowledgeOrder(units.front(),orderCursor(),destination.unit);
                    synchronize();ui.barAiming=-1;
                }
            }
        }
        ui.barPressed=nothing;
    }
    return true;
}
SHPFile* icon(const CommandBarOrders::Entry& entry) {
    auto [it,first]=ui.barImages.try_emplace(entry.image);
    if(first && entry.image.ends_with(".shp")) {
        CCFileClass file(entry.image.c_str());
        if(file.Exists()) it->second=std::make_unique<SHPReference>(entry.image.c_str());
    }
    auto shape=it->second?it->second->GetData():nullptr;
    if(shape && shape->Type==0 && shape->Frames>0 && shape->Width>0 && shape->Height>0) return shape;
    if(entry.explicitImage && ui.reportedBarImages.insert(entry.id).second)
        ConfigText::configLog("["+entry.id+"] CommandBar.Image="+entry.image+" - 无法读取按钮 SHP，使用内置准星");
    return nullptr;
}
void drawReticle(DSurface* surface,const RectangleStruct& r,int x,int y,int ink) {
    const int arm=std::min(10,(std::min(r.Width,r.Height)-2)/2),ring=std::max(2,arm-3);
    auto bar=[&](int dx,int dy,int w,int h) {
        RectangleStruct mark{x+dx,y+dy,w,h};surface->FillRect(&mark,ink);
    };
    // Pixel bars stay crisp on the 16-bit bottom strip, outside the tactical line clip.
    for(int dy=-ring;dy<=ring;++dy) {
        int dx=static_cast<int>(std::lround(std::sqrt(double(ring*ring-dy*dy))));
        if(dx) {bar(-dx,dy,2,1);bar(dx-1,dy,2,1);}
        else bar(-2,dy,5,1);
    }
    bar(-arm,0,arm-ring+2,1);bar(ring-1,0,arm-ring+2,1);
    bar(0,-arm,1,arm-ring+2);bar(0,ring-1,1,arm-ring+2);bar(-1,-1,3,3);
}
constexpr unsigned tooltipBase=0x53430000u;
void syncTooltips(const std::vector<BarAppearance>& buttons) {
    static std::vector<int> registered;
    auto manager=CCToolTip::Instance;
    if(!manager) {registered.clear();return;}
    auto remove=[&](unsigned id) {
        if(manager->CurrentToolTip && manager->CurrentToolTip->GadgetID==id) manager->Hide();
        manager->Remove(id);
    };
    std::erase_if(registered,[&](int order) {
        if(std::any_of(buttons.begin(),buttons.end(),[&](const auto& b){return b.order==order;})) return false;
        remove(tooltipBase+order);
        return true;
    });
    for(const auto& b:buttons) {
        const unsigned id=tooltipBase+b.order;
        ToolTip tip;
        if(manager->Find(id,tip)) {
            const auto& r=tip.Bounds;
            if(r.X==b.x && r.Y==b.y && r.Width==b.width && r.Height==b.height) continue;
            remove(id);
        }
        tip.GadgetID=id;
        tip.Bounds={b.x,b.y,b.width,b.height};
        tip.Text=nullptr; // Resolve the order's Unicode text when the native manager requests it.
        tip.field_18=false;
        if(manager->Add(tip) && std::find(registered.begin(),registered.end(),b.order)==registered.end())
            registered.push_back(b.order);
    }
}
// Participate in the stock strip's dirty-surface cache. The engine repaints
// all its backing surfaces when a button first appears or changes state.
bool prepareNativeBar() {
    if(!ScenarioClass::Instance || !HouseClass::CurrentPlayer || !DSurface::Temp || !FileSystem::SIDEBAR_PAL) {
        bool hadButtons=!ui.barAppearance.empty();
        ui.barAppearance.clear();
        syncTooltips(ui.barAppearance);
        return hadButtons;
    }
    if(!CommandBarOrders::listedSlots.empty() && Unsorted::ScenarioStarted) Research::ensureReady();
    syncLocalState();
    decltype(ui.barAppearance) appearance;
    if(!HouseClass::CurrentPlayer->IsObserver() && !HouseClass::CurrentPlayer->Defeated) {
        for(const auto& [i,r]:slots()) {
            int frame=!hasSelection(i)?2:(ui.barAiming==i || ui.barPressed==i)?1:0;
            appearance.push_back({i,r.X,r.Y,r.Width,r.Height,frame});
        }
    }
    syncTooltips(appearance);
    if(appearance==ui.barAppearance) return false;
    ui.barAppearance=std::move(appearance);
    return true;
}
void paintNativeBar() {
    auto surface=DSurface::Temp;
    for(auto [i,x,y,w,h,frame]:ui.barAppearance) {
        const RectangleStruct slot{x,y,w,h};
        auto shape=icon(CommandBarOrders::entries[i]);
        if(shape && FileSystem::SIDEBAR_PAL) {
            if(frame>=shape->Frames) frame=0;
            Point2D offset{(w-shape->Width)/2,(h-shape->Height)/2};
            if(shape->Width>w || shape->Height>h) {
                surface->DrawSHP(FileSystem::SIDEBAR_PAL,shape,frame,&offset,&slot,BlitterFlags::None,
                    0,0,ZGradient::Ground,1000,0,nullptr,0,0,0);
                continue;
            }
            auto& gadget=ui.barGadgets[i];
            if(!gadget) gadget.reset(GameCreate<ShapeButtonClass>());
            gadget->SetShape(shape,w,h);
            gadget->SetPosition(x,y);
            gadget->DrawPosition=offset;
            gadget->Drawer=FileSystem::SIDEBAR_PAL;
            gadget->IsPressed=frame==1;
            gadget->Disabled=frame==2;
            gadget->Draw(true);
        } else {
            const bool armed=ui.barAiming==i;
            const bool over=WWMouseClass::Instance && inside(slot,WWMouseClass::Instance->XY1);
            int ink=frame==2?color(94,105,114):armed || over?color(255,210,101):color(108,196,255);
            RectangleStruct inset{x+3,y+3,w-6,h-6};
            surface->FillRect(&inset,armed?color(12,30,42):color(5,10,17));
            surface->DrawRect(&inset,armed?ink:color(49,68,81));
            drawReticle(surface,inset,x+w/2,y+h/2,ink);
        }
    }
}
void draw(Point2D mouse) {
    auto buttons=slots();
    if(buttons.empty()) {cancel();return;}
    if(ui.barAiming>=0 && (std::none_of(buttons.begin(),buttons.end(),[](const Slot& slot){return slot.order==ui.barAiming;}) || !hasSelection(ui.barAiming))) cancel();
    if(ui.barAiming>=0 && inside(DSurface::ViewBounds,mouse)) {
        auto center=pick(mouse).cell;
        if(ActiveSkills::playable(center)) drawOrderRange(MapClass::Instance.GetCellAt(center)->GetCoordsWithBridge(),
            CommandBarOrders::entries[ui.barAiming].radius,orderCursor());
    }
}
}
