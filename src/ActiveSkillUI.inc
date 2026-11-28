// Tactical HUD. Local selection and cursor previews never execute Lua.
namespace ActiveHUD {
using ActiveSkills::Kind;
constexpr unsigned commandTag=0x53460000u;
constexpr int nothing=-1,terrain=-2,nextPage=-3;
constexpr int buttonStride=64;
struct Group {
    int index;
    std::vector<TechnoClass*> ready;
    int cooldownFrames=0;
};
void trackCooldown(Group& group,int frames) {
    if(frames>0 && (!group.cooldownFrames || frames<group.cooldownFrames)) group.cooldownFrames=frames;
}
int cooldownSeconds(const Group& group) {
    if(!group.ready.empty() || group.cooldownFrames<=0) return 0;
    // Game seconds use 15 simulation frames; avoid overflow at INT_MAX.
    return group.cooldownFrames/15+(group.cooldownFrames%15!=0);
}
struct BarAppearance {
    int order,x,y,width,height,frame;
    bool operator==(const BarAppearance&) const=default;
};
struct State {
    unsigned generation=0;
    HouseClass* house=nullptr;
    int aiming=-1,pressed=nothing,page=0;
    int barAiming=-1,barPressed=nothing;
    bool showSelectedOrderAreas=true;
    std::map<std::string,std::unique_ptr<SHPReference>> barImages;
    std::map<int,std::unique_ptr<ShapeButtonClass,GameDeleter>> barGadgets;
    std::vector<BarAppearance> barAppearance;
    std::set<std::string> reportedBarImages;
    std::vector<DWORD> selection;
    std::set<std::pair<int,DWORD>> awaiting;
    std::deque<EventClass> unsent;
} ui;

MouseCursorType orderCursor() {
    if(ui.barAiming>=0 && ui.barAiming<static_cast<int>(CommandBarOrders::entries.size()))
        return CommandBarOrders::entries[ui.barAiming].enterKinds?MouseCursorType::Enter:MouseCursorType::Attack;
    if(ui.aiming>=0 && ui.aiming<static_cast<int>(ActiveSkills::specs.size()))
        return ActiveSkills::specs[ui.aiming].orderCursor;
    return MouseCursorType::Default;
}
bool aimingOrder() {return orderCursor()!=MouseCursorType::Default;}
void updateOrderCursor(MouseCursorType cursor=MouseCursorType::Default) {
    static MouseCursorType previous=MouseCursorType::Default;
    auto& mouse=MouseClass::Instance;
    if(cursor!=MouseCursorType::Default) {
        if(mouse.MouseCursorIndex!=cursor || mouse.MouseCursorIsMini)
            mouse.UpdateCursor(cursor,false);
    } else if(previous!=MouseCursorType::Default && mouse.MouseCursorIndex==previous) {
        mouse.UpdateCursor(MouseCursorType::Default,false);
    }
    previous=cursor;
}

bool available() {
    auto h=HouseClass::CurrentPlayer;
    return (!ActiveSkills::specs.empty() || !CommandBarOrders::entries.empty()) && h && !h->Defeated && !h->IsObserver() && ScenarioClass::Instance &&
        !ScenarioClass::Instance->UserInputLocked && TacticalClass::Instance && DSurface::Composite;
}
MouseCursorType mapOrderCursor() {
    const auto cursor=orderCursor();
    if(cursor==MouseCursorType::Default || ui.generation!=ActiveSkills::generation ||
       ui.house!=HouseClass::CurrentPlayer || !Game::IsActive || !Unsorted::TacticalActive ||
       Unsorted::UserInputLocked || Research::expanded || !available() || !WWMouseClass::Instance ||
       !inside(DSurface::ViewBounds,WWMouseClass::Instance->XY1)) return MouseCursorType::Default;
    return cursor;
}
void syncLocalState() {
    if(ui.generation!=ActiveSkills::generation || ui.house!=HouseClass::CurrentPlayer) {
        ui=State{};ui.generation=ActiveSkills::generation;ui.house=HouseClass::CurrentPlayer;
    }
}
void synchronize() {
    syncLocalState();
    while(!ui.unsent.empty() && EventClass::OutList.Add(ui.unsent.front())) ui.unsent.pop_front();
}
std::vector<Group> groups() {
    synchronize();
    std::map<int,Group> grouped;std::vector<DWORD> ids;
    for(auto object:ObjectClass::CurrentObjects) {
        if(!object || (object->AbstractFlags&AbstractFlags::Techno)==AbstractFlags::None) continue;
        auto unit=static_cast<TechnoClass*>(object);
        if(unit->Owner!=ui.house || !unit->IsAlive || unit->InLimbo) continue;
        ids.push_back(unit->UniqueID);
        for(auto index:ActiveSkills::held(unit)) {
            auto& g=grouped[index];g.index=index;
            int left=ActiveSkills::remaining(unit,ActiveSkills::specs[index]);
            trackCooldown(g,left);
            if(!ActiveSkills::readyCaster(unit,ui.house) || ui.awaiting.contains({index,unit->UniqueID})) continue;
            if(!left) g.ready.push_back(unit);
        }
    }
    std::sort(ids.begin(),ids.end());
    if(ids!=ui.selection) {ui.barAiming=-1;ui.barPressed=nothing;ui.aiming=-1;ui.pressed=nothing;ui.page=0;ui.showSelectedOrderAreas=true;ui.selection=std::move(ids);}
    if(!grouped.contains(ui.aiming) || grouped[ui.aiming].ready.empty()) ui.aiming=-1;
    std::vector<Group> result;
    for(auto& [index,g]:grouped) result.push_back(std::move(g));
    return result;
}
int pageSize() {return std::max(1,std::min(6,(DSurface::ViewBounds.Width-64)/buttonStride));}
RectangleStruct button(int slot) {
    const auto& v=DSurface::ViewBounds;
    return {v.X+8+slot*buttonStride,v.Y+v.Height-72,64,64};
}
struct Pick { CellStruct cell{-1,-1};TechnoClass* unit=nullptr; };
Pick pick(Point2D point) {
    Pick result;
    if(!inside(DSurface::ViewBounds,point)) return result;
    auto coords=TacticalClass::Instance->ClientToCoords(point);
    Point2D relative{point.X-DSurface::ViewBounds.X,point.Y-DSurface::ViewBounds.Y};
    ObjectClass* object=nullptr;BYTE a=0,b=0;
    DisplayClass::Instance.ProcessClickCoords(&relative,&result.cell,&coords,&object,&a,&b);
    if(object && (object->AbstractFlags&AbstractFlags::Techno)!=AbstractFlags::None) {
        auto unit=static_cast<TechnoClass*>(object);
        auto cell=unit->GetCell();
        if(cell && !cell->IsFogged() && !cell->IsShrouded()) result.unit=unit;
    }
    return result;
}
struct Choice { TechnoClass* caster;CellStruct end;int squared; };
std::vector<Choice> choose(const Group& group,const Pick& pick) {
    const auto& spec=ActiveSkills::specs[group.index];
    std::vector<Choice> choices;
    for(auto caster:group.ready) {
        CellStruct end{};
        if(!ActiveSkills::aim(spec,caster,pick.unit,pick.cell,end)) continue;
        if(spec.eject && !ForceMove::canEject(caster,end,*spec.eject)) continue;
        auto origin=caster->GetMapCoords();
        auto goal=spec.kind==Kind::Direction?pick.cell:end;
        int dx=origin.X-goal.X,dy=origin.Y-goal.Y;
        choices.push_back({caster,end,dx*dx+dy*dy});
    }
    std::sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){
        return a.squared!=b.squared?a.squared<b.squared:a.caster->UniqueID<b.caster->UniqueID;
    });
    if(spec.jump) {
        std::vector<CellStruct> claimed;
        std::vector<Choice> assigned;
        for(auto choice:choices) {
            auto slot=ForceMove::jumpSlot(choice.caster,pick.cell,*spec.jump,claimed);
            if(!slot) continue;
            choice.end=*slot;claimed.push_back(*slot);assigned.push_back(choice);
            if(!spec.all) break;
        }
        return assigned;
    }
    if(!spec.all && choices.size()>1) choices.resize(1);
    return choices;
}
void acknowledgeOrder(TechnoClass* speaker,MouseCursorType order,ObjectClass* target) {
    if(!Unsorted::MoveFeedback || !VocClass::VoicesEnabled) return;
    if(order==MouseCursorType::Enter) speaker->VoiceMove();
    else if(order==MouseCursorType::Attack) speaker->VoiceAttack(target);
}
bool confirm(const Group& group,const Pick& pick) {
    const auto& spec=ActiveSkills::specs[group.index];
    auto choices=choose(group,pick);
    for(const auto& c:choices) {
        DWORD target=spec.kind==Kind::Unit?pick.unit->UniqueID:0;
        EventClass command(ui.house->ArrayIndex,EventType::Place,static_cast<AbstractType>(commandTag|group.index),
            c.caster->UniqueID,target,pick.cell);
        ui.awaiting.emplace(group.index,c.caster->UniqueID);
        ui.unsent.emplace_back(command);
    }
    if(choices.empty()) return false;
    if(spec.orderCursor!=MouseCursorType::Default) {
        ui.showSelectedOrderAreas=false;
        acknowledgeOrder(choices.front().caster,spec.orderCursor,pick.unit);
    } else playClick();
    synchronize();ui.aiming=-1;return true;
}
void circle(CoordStruct pos,int radius,int rgb);
void drawOrderRange(CoordStruct pos,int radius,MouseCursorType style,bool valid=true) {
    auto player=HouseClass::CurrentPlayer;
    if(radius<=0 || !player || !DSurface::Composite) return;
    ColorStruct base=valid?player->Color:ColorStruct{237,91,83};
    // The native range renderer draws to Temp. Here the tactical view has already been composited.
    auto previous=DSurface::Temp;
    DSurface::Temp=DSurface::Composite;
    if(style==MouseCursorType::Enter) {
        Game::DrawRadialIndicator(false,true,pos,base,static_cast<float>(radius),false,true);
    } else {
        // Stock NukeSpecial has LineMultiplier=2. Its half-cell rings pulse in house color.
        int phase=Unsorted::CurrentFrame%20;
        for(int i=0;i<=radius*2;++i) {
            int shade=10*std::abs(10-(phase+i)%20);
            ColorStruct tint{static_cast<BYTE>(base.R-base.R*shade/256),
                static_cast<BYTE>(base.G-base.G*shade/256),static_cast<BYTE>(base.B-base.B*shade/256)};
            Game::DrawRadialIndicator(false,true,pos,tint,0.5f*i,false,false);
        }
    }
    DSurface::Temp=previous;
}
#include "../modules/ActSp/CommandBarUI.inc"
bool input(Point2D p,int flags) {
    if(!available() || Research::expanded) {Bar::cancel();ui.aiming=-1;ui.pressed=nothing;return false;}
    auto items=groups();
    if(Bar::input(p,flags)) return true;
    if((flags&0x10) && ui.aiming>=0) {ui.aiming=-1;ui.pressed=nothing;return true;}
    if(items.empty()) return false;
    int per=pageSize(),pages=(static_cast<int>(items.size())+per-1)/per;ui.page%=pages;
    int hit=nothing;
    for(int i=ui.page*per;i<std::min<int>((ui.page+1)*per,items.size());++i)
        if(inside(button(i%per),p)) hit=items[i].index;
    auto next=button(per);next.Width=36;
    if(pages>1 && inside(next,p)) hit=nextPage;
    if(hit==nothing && ui.aiming>=0 && inside(DSurface::ViewBounds,p)) hit=terrain;
    if(hit==nothing && ui.pressed==nothing) return false;
    if(flags&1) ui.pressed=hit;
    if(flags&4) {
        if(hit!=nothing && ui.pressed==hit) {
            if(hit==nextPage) {ui.page=(ui.page+1)%pages;ui.aiming=-1;}
            else {
                int selected=hit==terrain?ui.aiming:hit;
                auto it=std::find_if(items.begin(),items.end(),[&](const auto& g){return g.index==selected;});
                if(it!=items.end() && !it->ready.empty()) {
                    if(hit==terrain) confirm(*it,pick(p));
                    else if(ActiveSkills::specs[selected].kind==Kind::None) confirm(*it,{});
                    else ui.aiming=ui.aiming==selected?-1:selected;
                }
            }
        }
        ui.pressed=nothing;
    }
    return true;
}
void line(Point2D a,Point2D b,int rgb) {
    auto clip=DSurface::ViewBounds;
    DSurface::Composite->DrawLineEx(&clip,&a,&b,rgb);
}
Point2D project(CoordStruct p) {
    auto point=TacticalClass::Instance->CoordsToClient(p).first;
    return {point.X+DSurface::ViewBounds.X,point.Y+DSurface::ViewBounds.Y};
}
void circle(CoordStruct pos,int radius,int rgb) {
    Point2D first{},last{};
    for(int i=0;i<=40;++i) {
        double t=i*(6.283185307179586/40);
        CoordStruct offset{pos.X+static_cast<int>(radius*std::cos(t)),pos.Y+static_cast<int>(radius*std::sin(t)),pos.Z};
        auto point=project(offset);
        if(i) line(last,i==40?first:point,rgb);else first=point;
        last=point;
    }
}
void arrow(Point2D from,Point2D to,int rgb) {
    line(from,to,rgb);
    double dx=to.X-from.X,dy=to.Y-from.Y,length=std::hypot(dx,dy);
    if(length<1) return;
    dx/=length;dy/=length;
    for(int side:{-1,1})
        line(to,{to.X-static_cast<int>(dx*13+dy*6*side),to.Y-static_cast<int>(dy*13-dx*6*side)},rgb);
}
#include "ActiveIndicator.inc"
void preview(const Group& g,Point2D mouse) {
    auto picked=pick(mouse);
    auto choices=choose(g,picked);
    const auto& s=ActiveSkills::specs[g.index];
    int rgb=choices.empty()?color(237,91,83):color(Indicator::validRed,Indicator::validGreen,Indicator::validBlue);
    const auto milliseconds=GetTickCount64();
    if(s.kind==Kind::Unit && picked.unit) picked.cell=picked.unit->GetMapCoords();
    if(picked.cell.X<0 || picked.cell.Y<0 || picked.cell.X>=512 || picked.cell.Y>=512) return;
    auto cell=MapClass::Instance.TryGetCellAt(picked.cell);
    if(!cell) return;
    auto pos=cell->GetCoordsWithBridge();
    bool valid=!choices.empty();
    int radius=s.kind==Kind::Area?s.radius*256:176;
    if(s.orderCursor!=MouseCursorType::Default) drawOrderRange(pos,s.radius,s.orderCursor,valid);
    else if(!Indicator::marker(pos,radius,s.indicatorTexture,s.kind==Kind::Area,valid,s.indicatorOpacity,milliseconds)) circle(pos,radius,rgb);
    for(const auto& c:choices) {
        if(s.jump && c.end!=picked.cell)
            circle(MapClass::Instance.GetCellAt(c.end)->GetCoordsWithBridge(),116,rgb);
        auto origin=Indicator::ground(c.caster);
        if(s.kind==Kind::Direction) {
            auto endCell=MapClass::Instance.TryGetCellAt(c.end);
            if(endCell && !Indicator::direction(origin,endCell->GetCoordsWithBridge(),s,valid))
                arrow(project(origin),project(endCell->GetCoordsWithBridge()),rgb);
        } else if(!Indicator::marker(origin,144,"",true,valid,s.indicatorOpacity,milliseconds)) circle(origin,144,rgb);
    }
}
void drawButtonArt(const ActiveSkills::Spec& spec,RectangleStruct rect,bool enabled) {
    // Enlarge the subject, then crop to the solid rim so opaque image corners cannot escape.
    RectangleStruct icon{rect.X+5,rect.Y+4,54,54},clip{rect.X+10,rect.Y+9,44,44};
    int opacity=enabled?255:112;
    if(spec.icon.empty() || !blit(spec.icon.c_str(),icon,0,std::nullopt,opacity,&clip))
        blit("sfactive_default.png",icon,0,std::nullopt,opacity,&clip);
    blit("sfframe.png",rect);
}
void drawButtonLabel(const ActiveSkills::Spec& spec,RectangleStruct rect,bool enabled) {
    RectangleStruct label{rect.X+13,rect.Y+39,38,10};
    ColorStruct shade{5,12,22};DSurface::Composite->FillRectTrans(&label,&shade,65);
    centered(spec.name,label,label.Y,enabled?color(224,240,245):color(150,160,170),9);
}
void drawCooldown(const Group& group,RectangleStruct rect) {
    int seconds=cooldownSeconds(group);if(!seconds) return;
    auto text=std::to_wstring(seconds);
    RectangleStruct body{rect.X+12,rect.Y+15,40,22};
    int size=22;
    while(size>7) {
        const auto& mask=ResearchText::maskFor(text.c_str(),size);
        if(mask.w<=body.Width && mask.h<=body.Height) break;
        --size;
    }
    const auto& mask=ResearchText::maskFor(text.c_str(),size);
    int x=body.X+(body.Width-std::min(mask.w,body.Width))/2,y=body.Y+(body.Height-mask.h)/2;
    // Outline only the digits, preserving transparent artwork around them.
    for(Point2D offset:std::array<Point2D,4>{{{-1,0},{1,0},{0,-1},{0,1}}})
        ResearchText::draw(text.c_str(),x+offset.X,y+offset.Y,color(0,6,12),size,body.Width);
    ResearchText::draw(text.c_str(),x,y,color(240,248,255),size,body.Width);
}
void draw() {
    synchronize();
    if(!available() || !WWMouseClass::Instance || Research::expanded) {updateOrderCursor();return;}
    auto items=groups();
    if(ui.showSelectedOrderAreas && !aimingOrder()) {
        std::set<std::tuple<short,short,int,MouseCursorType>> shown;
        for(auto id:ui.selection) {
            CellStruct center{};int radius=0;
            MouseCursorType style{};
            if(ActiveSkills::orderArea(id,center,radius,style) && shown.emplace(center.X,center.Y,radius,style).second)
                drawOrderRange(MapClass::Instance.GetCellAt(center)->GetCoordsWithBridge(),radius,style);
        }
    }
    auto mouse=WWMouseClass::Instance->XY1;
    Bar::draw(mouse);
    updateOrderCursor(mapOrderCursor());
    if(items.empty()) return;
    auto surface=DSurface::Composite;
    int per=pageSize(),pages=(static_cast<int>(items.size())+per-1)/per;ui.page%=pages;
    const Group* hover=nullptr;const Group* aiming=nullptr;
    for(int i=ui.page*per;i<std::min<int>((ui.page+1)*per,items.size());++i) {
        auto& g=items[i];auto& s=ActiveSkills::specs[g.index];auto rect=button(i%per);
        bool over=inside(rect,mouse),chosen=g.index==ui.aiming,enabled=!g.ready.empty();
        drawButtonArt(s,rect,enabled);
        if(chosen||over) surface->DrawRect(&rect,color(74,224,206));
        drawButtonLabel(s,rect,enabled);
        drawCooldown(g,rect);
        if(over) hover=&g;if(chosen) aiming=&g;
    }
    if(pages>1) {
        auto rect=button(per);rect.Width=36;surface->FillRect(&rect,color(13,25,37));
        surface->DrawRect(&rect,color(64,85,105));ResearchText::draw(L"›",rect.X+12,rect.Y+9,color(200,230,238),20);
    }
    if(aiming && !hover && inside(DSurface::ViewBounds,mouse)) preview(*aiming,mouse);
    auto info=hover?hover:aiming;
    if(info) {
        const auto& s=ActiveSkills::specs[info->index];
        std::wstring text=s.name+(s.all?L" · 全体施放":L" · 单人施放");
        if(int seconds=cooldownSeconds(*info)) text+=L" · 冷却 "+std::to_wstring(seconds)+L" 秒";
        if(info==aiming) text+=s.kind==Kind::Unit?L" · 选择单位，右键取消":s.kind==Kind::Direction?L" · 选择方向，右键取消":L" · 选择位置，右键取消";
        auto rect=button(0);rect.Y-=26;rect.Height=24;
        rect.Width=std::min(DSurface::ViewBounds.Width-16,ResearchText::maskFor(text.c_str(),14).w+16);
        surface->FillRect(&rect,color(13,25,37));ResearchText::draw(text.c_str(),rect.X+8,rect.Y+3,color(207,229,238),14,rect.Width-12);
    }
}
bool command(EventClass* event) {
    if(event->Type!=EventType::Place) return false;
    auto tag=static_cast<unsigned>(event->Place.RTTIType);
    if((tag&0xffff0000u)==CommandBarOrders::eventTag) {
        CommandBarOrders::execute(event->HouseIndex,tag&0xffff,event->Place.HeapID,event->Place.Location);
        return true;
    }
    if((tag&0xffff0000u)!=commandTag) return false;
    int index=tag&0xffff;
    if(HouseClass::CurrentPlayer && event->HouseIndex==HouseClass::CurrentPlayer->ArrayIndex)
        ui.awaiting.erase({index,static_cast<DWORD>(event->Place.HeapID)});
    ActiveSkills::execute(event->HouseIndex,index,event->Place.HeapID,event->Place.IsNaval,event->Place.Location);
    return true;
}
}
