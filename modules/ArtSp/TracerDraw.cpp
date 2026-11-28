#include "Tracers.h"
#include <Drawing.h>
#include <MapClass.h>
#include <Surface.h>
#include <TacticalClass.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ArtSp::Tracers {
namespace {
struct Trail {
    Style style;
    CoordStruct from, delta;
    double distance;
    unsigned start, frames;
};
constexpr size_t maxTrails=512;
std::vector<Trail> trails;

WORD blend(WORD background, const ColorStruct& tail,const ColorStruct& head,double t) {
    const auto under=Drawing::Int_To_RGB(background);
    auto channel=[t](int back,int low,int high) {
        return static_cast<int>(std::lerp(double(back),std::lerp(double(low),double(high),t),t));
    };
    return static_cast<WORD>(Drawing::RGB_To_Int(channel(under.R,tail.R,head.R),
        channel(under.G,tail.G,head.G),channel(under.B,tail.B,head.B)));
}
}

void launch(const Style& style,const CoordStruct& from,const CoordStruct& to,unsigned frame) {
    if(from==to || trails.size()>=maxTrails) return;
    auto delta=to-from;
    double distance=std::hypot(double(delta.X),double(delta.Y),double(delta.Z));
    trails.push_back({style,from,delta,distance,frame,static_cast<unsigned>(std::ceil(distance/style.speed))});
}
void clearTrails() { trails.clear(); }
void expire(unsigned frame) {
    std::erase_if(trails,[frame](const Trail& trail){return frame-trail.start>=trail.frames;});
}
void draw(Surface* surface,RectangleStruct viewport,unsigned frame) {
    auto tactical=TacticalClass::Instance;
    if(trails.empty() || !surface || !tactical || surface->GetBytesPerPixel()!=2) return;
    const int left=std::max(viewport.X,0), top=std::max(viewport.Y,0);
    const int right=std::min(viewport.X+viewport.Width,surface->GetWidth());
    const int bottom=std::min(viewport.Y+viewport.Height,surface->GetHeight());
    if(left>=right || top>=bottom) return;
    BYTE* pixels=nullptr;
    int pitch=0;
    for(const auto& trail:trails) {
        const unsigned age=frame-trail.start;
        if(age>=trail.frames) continue;
        const auto& style=trail.style;
        const double head=std::min(trail.distance,(double(age)+1)*style.speed);
        const double tail=std::max(0.0,head-style.length);
        const auto worldAt=[&](double distance){return trail.from+trail.delta*(distance/trail.distance);};
        const auto offset=Point2D{viewport.X,viewport.Y};
        const auto a=tactical->CoordsToClient(worldAt(tail)).first+offset;
        const auto b=tactical->CoordsToClient(worldAt(head)).first+offset;
        const int radius=style.width/2;
        int x0=std::max(left,std::min(a.X,b.X)-radius), x1=std::min(right-1,std::max(a.X,b.X)+radius);
        int y0=std::max(top,std::min(a.Y,b.Y)-radius), y1=std::min(bottom-1,std::max(a.Y,b.Y)+radius);
        if(x0>x1 || y0>y1) continue;
        const WORD solidColor=static_cast<WORD>(Drawing::RGB_To_Int(style.color));
        const int dx=b.X-a.X,dy=b.Y-a.Y;
        const bool horizontal=std::abs(dx)>=std::abs(dy);
        const int steps=std::max(std::abs(dx),std::abs(dy));
        CellClass* previous=nullptr;
        bool visible=false,drawn=false;
        for(int i=0;i<=steps;++i) {
            const double t=steps?double(i)/steps:1.0;
            const int x=a.X+static_cast<int>(std::lround(dx*t)),y=a.Y+static_cast<int>(std::lround(dy*t));
            if(x<x0-radius || x>x1+radius || y<y0-radius || y>y1+radius) continue;
            auto cell=MapClass::Instance.TryGetCellAt(worldAt(std::lerp(tail,head,t)));
            if(cell!=previous) {
                previous=cell;
                visible=cell && !cell->IsShrouded() && !cell->IsFogged();
            }
            if(!visible) continue;
            if(!pixels) {
                pixels=static_cast<BYTE*>(surface->Lock(left,top));
                if(!pixels) return;
                pitch=surface->GetPitch();
            }
            for(int k=0;k<style.width;++k) {
                int px=x+(horizontal?0:k-radius),py=y+(horizontal?k-radius:0);
                if(px<left || px>=right || py<top || py>=bottom) continue;
                auto& pixel=reinterpret_cast<WORD*>(pixels+(py-top)*pitch)[px-left];
                pixel=style.gradient ? blend(pixel,style.tailColor,style.color,t) : solidColor;
                drawn=true;
            }
        }
        if(drawn) tactical->RegisterDirtyArea({x0-viewport.X,y0-viewport.Y,x1-x0+1,y1-y0+1},false);
    }
    if(pixels) surface->Unlock();
}
}
