#include "Research.h"
#include <EventClass.h>
#include <HouseClass.h>
#include <Drawing.h>
#include <PCX.h>
#include "ResearchText.h"
#include <WWMouseClass.h>

namespace {
constexpr int commandId = -0x5346;
constexpr short commandMarker = 0x5243;

bool visible() {
    auto player = HouseClass::CurrentPlayer;
    return DSurface::Composite && ScenarioClass::Instance && player && !player->IsObserver()
        && !player->Defeated && !ScenarioClass::Instance->UserInputLocked;
}
struct Layout {
    RectangleStruct tab, panel;
    RectangleStruct row(int i) const {
        return {panel.X + 12, panel.Y + 45 + 78 * i, 360, 74};
    }
};
Layout layout() {
    const auto& view = DSurface::ViewBounds;
    int right = view.X + view.Width - 8;
    int top = view.Y + 24;
    return {{right - 46, top, 46, 46}, {right - 384, top + 50, 384, 286}};
}
bool inside(const RectangleStruct& r, Point2D p) {
    return p.X >= r.X && p.X < r.X+r.Width && p.Y >= r.Y && p.Y < r.Y+r.Height;
}
const std::vector<int>& houseOffers(HouseClass* house) {
    return Research::offers(house->ArrayIndex,house->Type ? house->Type->ID : "");
}
int hitTest(const Layout& l, Point2D p) {
    if (inside(l.tab,p)) return 0;
    if (!Research::expanded || !inside(l.panel,p)) return -1;
    for (int i=0; i<static_cast<int>(houseOffers(HouseClass::CurrentPlayer).size()); ++i) if (inside(l.row(i),p)) return i+1;
    return 4;
}
int color(int r,int g,int b) { return Drawing::RGB_To_Int(r,g,b); }
bool blit(const char* name, RectangleStruct rect) {
    // Look up each surface in the game cache: it can be rebuilt between matches.
    auto& pcx=PCX::Instance;
    auto image=pcx.GetSurface(name);
    if(!image && pcx.LoadFile(name)) image=pcx.GetSurface(name);
    return image && pcx.BlitToSurface(&rect,DSurface::Composite,image,static_cast<WORD>(color(255,0,255)));
}
std::wstring targetLabel(const Research::Option& option,bool compact) {
    if(option.effect==Research::Effect::Credits) return L"本方资金";
    if(option.effect==Research::Effect::Technology) return option.technology=="allied"?L"盟军科技":option.technology=="soviet"?L"苏军科技":L"尤里科技";
    if(option.effect==Research::Effect::ConvertInfantry) {
        auto source=TechnoTypeClass::Find(option.from.c_str()),target=TechnoTypeClass::Find(option.to.c_str());
        if(source && target && source->UIName && target->UIName)
            return compact?std::wstring(source->UIName):std::wstring(source->UIName)+L" → "+target->UIName;
        return L"本方步兵";
    }
    std::wstring label;
    if(std::find(option.types.begin(),option.types.end(),"*")!=option.types.end()) return L"全部单位";
    for(const auto& id:option.types) {
        auto type=TechnoTypeClass::Find(id.c_str());
        if(!label.empty()) label+=L"、";
        if(type && type->UIName && *type->UIName) label+=type->UIName;
        else label.append(id.begin(),id.end());
        if(compact && option.types.size()>1) {label+=L"等";break;}
    }
    return label;
}
void drawTooltip(const Layout& l,int slot,int option) {
    const auto& view=DSurface::ViewBounds;
    constexpr int width=328;
    const auto& project=Research::options[option];
    std::vector<std::wstring> typeLines;
    auto targets=targetLabel(project,false);
    // 19 full-width glyphs fit the tooltip even with the larger Chinese font.
    for(size_t pos=0;pos<targets.size();pos+=19) typeLines.push_back(targets.substr(pos,19));
    const int height=100+static_cast<int>(typeLines.size())*20;
    int x=l.panel.X-width-6, y=l.row(slot).Y;
    if(x<view.X+4) { x=l.panel.X+l.panel.Width-width; y=l.panel.Y+l.panel.Height+4; }
    y=std::clamp(y,view.Y,std::max(view.Y,view.Y+view.Height-height));
    RectangleStruct rect{x,y,width,height};
    auto surface=DSurface::Composite;
    surface->FillRect(&rect,color(15,19,35));
    surface->DrawRect(&rect,color(117,125,178));
    wchar_t conditions[96]{};
    auto detail=Research::describe(option,conditions);
    ResearchText::draw(Research::options[option].name.c_str(),x+14,y+8,color(211,201,255),17,width-28);
    ResearchText::draw(detail.c_str(),x+14,y+39,color(233,237,250),15,width-28);
    ResearchText::draw(conditions,x+14,y+69,color(170,183,207),14);
    for(size_t n=0;n<typeLines.size();++n)
        ResearchText::draw(typeLines[n].c_str(),x+14,y+92+20*static_cast<int>(n),color(170,183,207),14,width-28);
}
void draw() {
    auto surface=DSurface::Composite;
    const auto l=layout();
    const auto pointer=WWMouseClass::Instance->XY1;
    const int points=Research::balance(HouseClass::CurrentPlayer->ArrayIndex);
    bool affordable=points>=Research::price;
    const int white=color(236,239,248), muted=color(184,193,214), accent=color(182,159,255);
    auto tab=l.tab;
    if(!blit("sfrlab.pcx",tab)) {
        surface->FillRect(&tab,color(18,22,38));
        ResearchText::draw(L"科研",tab.X+6,tab.Y+14,white);
    }
    if(inside(tab,pointer) || Research::expanded || (affordable && !houseOffers(HouseClass::CurrentPlayer).empty())) surface->DrawRect(&tab,accent);
    if(!Research::expanded) return;
    auto panel=l.panel;
    if(!blit("sfrpanel.pcx",panel)) surface->FillRect(&panel,color(17,23,35));
    ResearchText::draw(Research::title.c_str(),panel.X+20,panel.Y+6,white,21,panel.Width-180);
    wchar_t status[48]{};
    swprintf_s(status,L"科研点 %d / 100",points);
    ResearchText::draw(status,panel.X+panel.Width-154,panel.Y+11,affordable?accent:muted,15,148);

    const auto& offered=houseOffers(HouseClass::CurrentPlayer);
    if(offered.empty()) ResearchText::draw(L"当前没有待研究项目",panel.X+26,panel.Y+70,muted);
    for(int slot=0;slot<static_cast<int>(offered.size());++slot) {
        int i=offered[slot];
        auto row=l.row(slot);
        bool enabled=Research::canApply(HouseClass::CurrentPlayer->ArrayIndex,i);
        if(enabled && inside(row,pointer)) {
            if(!blit("sfrfocus.pcx",row)) surface->FillRect(&row,color(48,39,72));
        }
        if(!Research::options[i].icon.empty()) blit(Research::options[i].icon.c_str(),{row.X+7,row.Y+6,62,62});
        auto heading=Research::options[i].name+L" · "+targetLabel(Research::options[i],true);
        ResearchText::draw(heading.c_str(),row.X+82,row.Y+8,white,18,row.Width-((!affordable || !enabled)?154:92));

        ResearchText::draw(Research::options[i].summary.c_str(),row.X+82,row.Y+39,muted,15,row.Width-94);
        if(!affordable || !enabled) ResearchText::draw(enabled ? L"点数不足" : Research::options[i].effect==Research::Effect::Technology ? L"已解锁" : L"暂无目标",
            row.X+row.Width-64,row.Y+12,accent);
    }
    const int hovered=hitTest(l,pointer)-1;
    if(hovered>=0 && hovered<static_cast<int>(offered.size())) drawTooltip(l,hovered,offered[hovered]);
}
}
DEFINE_HOOK(0x4F4583, SF_ResearchDraw, 6) {
    if(visible() && WWMouseClass::Instance) draw();
    return 0;
}
// At this point the tactical gadget has already resolved the input coordinates.
// Consume the whole press/release pair so selecting an upgrade cannot order the army.
DEFINE_HOOK(0x4AAC92, SF_ResearchInput, 5) {
    if(!visible()) return 0;
    int flags=R->Stack<int>(0x34);
    auto hit=hitTest(layout(),R->Stack<Point2D>(0x1c));
    if(flags & 1) Research::pressed=hit;
    if(hit<0 && Research::pressed<0) return 0;
    if(flags & 4) {
        if(hit>=0 && hit==Research::pressed) {
            if(hit==0) Research::expanded=!Research::expanded;
            else if(hit<=3 && Research::balance(HouseClass::CurrentPlayer->ArrayIndex)>=Research::price && hit<=static_cast<int>(houseOffers(HouseClass::CurrentPlayer).size())) {
                int index=houseOffers(HouseClass::CurrentPlayer)[hit-1];
                if(!Research::canApply(HouseClass::CurrentPlayer->ArrayIndex,index)) {
                    Research::pressed=-1;
                    if(auto key=R->Stack<DWORD*>(0x38)) *key=0;
                    R->EAX(1);return 0x4AAD19;
                }
                CellStruct payload{commandMarker,static_cast<short>(index)};
                EventClass event(HouseClass::CurrentPlayer->ArrayIndex,EventType::SpecialPlace,commandId,payload);
                if(EventClass::OutList.Add(event)) Research::expanded=false;
            }
        }
        Research::pressed=-1;
    }
    auto key=R->Stack<DWORD*>(0x38);
    if(key) *key=0;
    R->EAX(1);
    return 0x4AAD19;
}
// Reuse the game's serialized SpecialPlace envelope with a reserved negative ID.
// All peers grant the upgrade only when the queued command is executed.
DEFINE_HOOK(0x4C6CC8, SF_ResearchCommand, 5) {
    auto event=R->ESI<EventClass*>();
    if(event->Type!=EventType::SpecialPlace || event->SpecialPlace.ID!=commandId
        || event->SpecialPlace.Location.X!=commandMarker) return 0;
    int sender=event->HouseIndex, option=event->SpecialPlace.Location.Y;
    if(sender>=0 && sender<HouseClass::Array.Count && option>=0) {
        auto house=HouseClass::Array.Items[sender];
        if(house && !house->Defeated && !house->IsObserver()) {
            Research::purchase(sender,option,house->Type ? house->Type->ID : "");
        }
    }
    return 0x4C8109;
}
