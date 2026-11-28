#include "TipsLabel.h"
#include "../../src/ActionSequence.h"
#include "../../src/ConfigText.h"
#include "../../src/PassiveRules.h"
#include "../../src/ResearchText.h"
#include <BuildingClass.h>
#include <CellClass.h>
#include <HouseClass.h>
#include <TacticalClass.h>
#include <unordered_map>

namespace ArtSp::TipsLabel {
namespace {
struct Label {
    std::wstring text;
    std::unique_ptr<BSurface,GameDeleter> image;
};
std::unordered_map<TechnoTypeClass*,Label> labels;

bool visible(TechnoClass* unit,HouseClass* viewer) {
    auto cell=unit->GetCell();
    if(!cell || cell->IsFogged() || cell->IsShrouded() || unit->InWhichLayer()==Layer::Underground ||
       unit->VisualCharacter(false,nullptr)==VisualType::Hidden) return false;
    if(viewer->IsObserver() || unit->Owner==viewer) return true;
    if(unit->Disguised && !unit->IsClearlyVisibleTo(viewer)) return false;
    return unit->CloakState==CloakState::Uncloaked || (unit->Owner && viewer->IsAlliedWith(unit->Owner)) ||
        (viewer->ArrayIndex>=0 && viewer->ArrayIndex<24 && cell->Sensors_InclHouse(viewer->ArrayIndex));
}

Point2D aboveBracket(TechnoClass* unit,TechnoTypeClass* type,Point2D point) {
    if(auto building=abstract_cast<BuildingClass*>(unit)) {
        CoordStruct size{};
        type->Dimension2(&size);
        auto top=TacticalClass::CoordsToScreen({-size.X/2,size.Y/2,size.Z});
        const int depth=building->Type->GetFoundationHeight(false);
        const int halfEdge=7*depth+depth/2;
        point.X+=top.X+3+4*halfEdge;
        point.Y+=top.Y+4-2*halfEdge;
    } else point.Y-=unit->WhatAmI()==AbstractType::Infantry?27:28;
    point.Y+=type->PixelSelectionBracketDelta-4;
    return point;
}

void paint(Label& label,Point2D anchor,DSurface* target,int width,int height) {
    if(!label.image) {
        const auto& mask=ResearchText::maskFor(label.text.c_str(),13);
        if(mask.alpha.empty()) return;
        label.image.reset(GameCreate<BSurface>(mask.w+8,mask.h+4));
        label.image->Fill(Drawing::RGB_To_Int(14,27,43));
        ResearchText::draw(label.text.c_str(),4,2,Drawing::RGB_To_Int(232,242,251),13,mask.w,label.image.get());
    }
    const int x=anchor.X-label.image->Width/2,y=anchor.Y-label.image->Height;
    const int left=std::max(0,x),top=std::max(0,y);
    RectangleStruct dest{left,top,std::min(width,x+label.image->Width)-left,std::min(height,y+label.image->Height)-top};
    if(dest.Width<=0 || dest.Height<=0) return;
    RectangleStruct source{left-x,top-y,dest.Width,dest.Height};
    if(target->CopyFromPart(&dest,label.image.get(),&source,false,true))
        TacticalClass::Instance->RegisterDirtyArea(dest,false);
}
}

void clear() {labels.clear();}
void readRules() {
    clear();
    for(auto type:TechnoTypeClass::Array) {
        auto raw=PassiveRules::readRaw(type->ID,"TipsLabel");
        if(auto section=ActionSequence::mapFields.find(ConfigText::lower(type->ID));section!=ActionSequence::mapFields.end()) {
            if(auto field=section->second.find("tipslabel");field!=section->second.end()) raw=field->second;
        }
        auto text=ConfigText::displayText(raw);
        if(!text.empty()) labels[type].text=std::move(text);
    }
}
void draw() {
    auto surface=DSurface::Temp;
    auto tactical=TacticalClass::Instance;
    auto viewer=HouseClass::CurrentPlayer;
    if(labels.empty() || !surface || !tactical || !viewer) return;
    const int width=std::min(surface->GetWidth(),DSurface::ViewBounds.Width);
    const int height=std::min(surface->GetHeight(),DSurface::ViewBounds.Height);
    for(auto unit:TechnoClass::Array) {
        if(!unit || !unit->IsAlive || unit->Health<=0 || !unit->IsOnMap || unit->InLimbo || unit->Transporter || unit->Absorbed) continue;
        auto type=unit->GetTechnoType();
        auto it=labels.find(type);
        if(it==labels.end() || type->Invisible) continue;
        auto [point,nearView]=tactical->CoordsToClient(unit->GetCoords());
        if(nearView && visible(unit,viewer)) paint(it->second,aboveBracket(unit,type,point),surface,width,height);
    }
}
}
