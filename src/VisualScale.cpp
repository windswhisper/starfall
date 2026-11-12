#include "VisualScale.h"
#include <FootClass.h>
#include <Unsorted.h>
#include <Surface.h>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <vector>

namespace VisualScale {
static int rasterPercent(double value) {
    // Beyond this magnification a single source pixel already covers the view.
    // Bound only the raster calculation; never discard earned size increments.
    const auto& view=DSurface::ViewBounds;
    const double subpixelLimit=200.0*(512+std::max(view.Width,view.Height));
    return static_cast<int>(std::lround(std::clamp(value,1.0,subpixelLimit)));
}
int percent(DWORD id) {
    auto fixed=base.find(id);
    auto factor=state.find(id);
    if(fixed==base.end() && factor==state.end()) return 100;
    return rasterPercent(std::max(static_cast<double>(minimum),(fixed==base.end()?100:fixed->second)*(factor==state.end()?1.0:factor->second)));
}
static int projectileRaster(DWORD id) {
    auto it=projectiles.find(id);
    return it==projectiles.end()?100:rasterPercent(it->second);
}
static int animationPercent(DWORD id) {
    if(auto it=attachedAnims.find(id);it!=attachedAnims.end()) return percent(it->second);
    if(auto it=explosions.find(id);it!=explosions.end()) return rasterPercent(it->second);
    return 100;
}
namespace {
using Bytes=std::vector<BYTE>;
using FrameKey=std::tuple<SHPStruct*,int,int,int,int>;
std::map<FrameKey,Bytes> sprites;
size_t cacheBytes=0;
int cacheFrame=-1;

int scaled(int value,int percent) {
    return static_cast<int>(std::lround(value*(percent/100.0)));
}
bool appendFrame(SHPStruct* source,SHPFile* file,int index,int scale,int slot,int canvasW,int canvasH,Bytes& output) {
    if(index>=file->Frames) return true;
    auto h=file->GetFrameHeader(index);
    if(!h.Offset || !h.Width || !h.Height) return true;
    const BYTE* src=source->GetPixels(index);
    if(!src || h.Width<0 || h.Height<0 || h.Width>1024 || h.Height>1024) return false;
    int w=std::max(1,scaled(h.Width,scale)),height=std::max(1,scaled(h.Height,scale));
    // Crop before expanding. Memory and work remain bounded by the visible
    // canvas even for a unit which has accumulated thousands of triggers.
    int left=scaled(h.Left,scale)-(scaled(file->Width,scale)-canvasW)/2;
    int top=scaled(h.Top,scale)-(scaled(file->Height,scale)-canvasH)/2;
    int x0=std::clamp(-left,0,w),x1=std::clamp(canvasW-left,0,w);
    int y0=std::clamp(-top,0,height),y1=std::clamp(canvasH-top,0,height);
    if(x1<=x0 || y1<=y0) return true;
    auto sample=[](int x,int sourceSize,int renderedSize) {
        return static_cast<int>(static_cast<int64_t>(x)*sourceSize/renderedSize);
    };
    int format=h.Flags&3, stride=h.Width;
    if(format==2) {
        stride=src[0]+256*src[1]-2; src+=2;
        if(stride<0 || stride>h.Width) return false;
    }
    Bytes row(h.Width);
    const int offset=static_cast<int>(output.size());
    for(int y=0,outY=y0;y<h.Height && outY<y1;++y) {
        if(format==3) {
            int size=src[0]+256*src[1];
            if(size<2 || size>2*h.Width+2) return false;
            std::fill(row.begin(),row.end(),0);
            int x=0;
            for(int i=2;i<size;) {
                BYTE pixel=src[i++];
                if(pixel) { if(x>=h.Width) return false; row[x++]=pixel; }
                else {
                    if(i==size) return false;
                    int run=src[i++];
                    // Some explosion SHPs pad the final transparent run past
                    // the visible width. Only the declared pixels are drawn.
                    if(run>h.Width-x && i!=size) return false;
                    x+=std::min(run,h.Width-x);
                }
            }
            src+=size;
        } else {
            std::copy_n(src,stride,row.begin());src+=stride;
        }
        while(outY<y1 && sample(outY,h.Height,height)==y) {
            size_t start=output.size();
            output.resize(start+2);
            for(int x=x0;x<x1;) {
                BYTE pixel=row[sample(x,h.Width,w)];
                output.push_back(pixel);++x;
                if(!pixel) {
                    int run=1;
                    while(x<x1 && run<255 && !row[sample(x,h.Width,w)]) {++run;++x;}
                    output.push_back(static_cast<BYTE>(run));
                }
            }
            size_t length=output.size()-start;
            output[start]=static_cast<BYTE>(length);
            output[start+1]=static_cast<BYTE>(length>>8);
            ++outY;
        }
    }
    h.Left=static_cast<short>(left+x0);h.Top=static_cast<short>(top+y0);
    h.Width=static_cast<short>(x1-x0);h.Height=static_cast<short>(y1-y0);
    h.Flags|=3;h.Offset=offset;
    memcpy(output.data()+sizeof(SHPStruct)+slot*sizeof(SHPFrame),&h,sizeof(h));
    return true;
}
// The native SHP drawer requests the shadow at frame + Frames/2. Supply a
// two-frame shape, keeping palette indices and the center of the original canvas.
SHPStruct* framePair(SHPStruct* source,int index,int scale) {
    if(!source || index<0 || index>=source->Frames) return nullptr;
    if(cacheBytes>8*1024*1024 && cacheFrame!=Unsorted::CurrentFrame) clearSprites();
    cacheFrame=Unsorted::CurrentFrame;
    auto canvas=[&](int original,int screen){return std::clamp(scaled(original,scale),1,std::min(32000,2*(screen+512)));};
    int width=canvas(source->Width,DSurface::ViewBounds.Width),height=canvas(source->Height,DSurface::ViewBounds.Height);
    FrameKey key{source,index,scale,width,height};
    auto cached=sprites.find(key);
    if(cached==sprites.end()) {
        auto file=source->GetData();
        if(!file || file->Width<=0 || file->Height<=0 || file->Width>2048 || file->Height>2048) return nullptr;
        Bytes data(sizeof(SHPStruct)+2*sizeof(SHPFrame),0);
        short header[]={0,static_cast<short>(width),static_cast<short>(height),2};
        static_assert(sizeof(header)==sizeof(SHPStruct) && sizeof(SHPFrame)==24);
        memcpy(data.data(),header,sizeof(header));
        if(!appendFrame(source,file,index,scale,0,width,height,data) ||
           !appendFrame(source,file,index+file->Frames/2,scale,1,width,height,data)) return nullptr;
        cacheBytes+=data.size();
        cached=sprites.emplace(key,std::move(data)).first;
    }
    return reinterpret_cast<SHPStruct*>(cached->second.data());
}
}
void clearSprites() { sprites.clear();cacheBytes=0; }
}

// All ordinary animation palette paths meet here with the resolved SHP and
// frame in draw-local slots. The native drawer retains lighting and blending.
DEFINE_HOOK(0x423358, SF_ExplosionSpriteScale, 6) {
    if(VisualScale::explosions.empty() && VisualScale::attachedAnims.empty()) return 0;
    const int scale=VisualScale::animationPercent(R->ESI<ObjectClass*>()->UniqueID);
    if(scale==100) return 0;
    auto shape=VisualScale::framePair(R->Stack<SHPStruct*>(0x28),R->Stack<int>(0x2C),
        scale);
    if(shape) {
        R->Stack(0x28,shape);
        R->Stack<int>(0x2C,0);
    }
    return 0;
}

// Extend the native dirty rectangle on both sides by the maximum extra
// reach of a centered SHP. This also covers frames with asymmetric artwork.
DEFINE_HOOK(0x5F4AF0, SF_ExplosionRenderBounds, 6) {
    if(VisualScale::explosions.empty() && VisualScale::attachedAnims.empty()) return 0;
    auto object=R->Stack<ObjectClass*>(0x0C);
    int scale=VisualScale::animationPercent(object->UniqueID);
    if(scale<=100) return 0;
    auto padding=[scale](int size,int screen) {
        int enlarged=std::min(VisualScale::scaled(size,scale),2*(screen+512));
        return std::max(0,(enlarged-size+1)/2);
    };
    int dx=padding(R->Stack<int>(0x10),DSurface::ViewBounds.Width);
    int dy=padding(R->Stack<int>(0x14),DSurface::ViewBounds.Height);
    R->ESI(R->ESI<int>()-dx); R->EDI(R->EDI<int>()-dy);
    R->EBX(R->EBX<int>()+2*dx); R->ECX(R->ECX<int>()+2*dy);
    return 0;
}

DEFINE_HOOK(0x4228EB, SF_ExplosionDestroyed, 6) {
    auto id=R->ESI<ObjectClass*>()->UniqueID;
    VisualScale::explosions.erase(id);
    VisualScale::attachedAnims.erase(id);
    return 0;
}

// VXLs first render into the game's fixed 256-square voxel surface. Resample
// that raster at the final blit, never projecting oversized geometry into it.
// Keep the native blitter for palette conversion, transparency and depth tests.
namespace {
struct ScaledBlit {
    BSurface surface{1,1};
    std::vector<BYTE> pixels;
    std::vector<int> columns;
    RectangleStruct source{},destination{};
    Point2D position{};

    bool prepare(Surface* original,RectangleStruct in,Point2D dest,RectangleStruct clip,int percent) {
        if(!original || in.Width<=0 || in.Height<=0) return false;
        int bpp=original->GetBytesPerPixel();
        if(bpp!=1 && bpp!=2) return false;
        const double zoom=percent/100.0;
        // The raster origin is at the center of the voxel surface. Splitting
        // water/bridge draws preserves the same pivot for both pieces.
        const int pivotX=original->GetWidth()/2,pivotY=original->GetHeight()/2;
        auto axis=[&](int old,int pos,int pivot,int offset) {
            return pos-(old-pivot)+static_cast<int>(std::round((old+offset-pivot)*zoom));
        };
        int left=axis(in.X,dest.X,pivotX,0),top=axis(in.Y,dest.Y,pivotY,0);
        int right=axis(in.X,dest.X,pivotX,in.Width),bottom=axis(in.Y,dest.Y,pivotY,in.Height);
        // Coordinates passed to both native blitters are relative to their clip.
        int x0=std::max(left,0),y0=std::max(top,0);
        int x1=std::min(right,clip.Width),y1=std::min(bottom,clip.Height);
        if(x0>=x1 || y0>=y1) {
            source=destination={0,0,0,0};position={0,0};return true;
        }
        if(in.X<0 || in.Y<0 || in.X+in.Width>original->GetWidth() || in.Y+in.Height>original->GetHeight()) return false;
        int w=x1-x0,h=y1-y0;
        pixels.resize(static_cast<size_t>(w)*h*bpp);
        auto row0=static_cast<const BYTE*>(original->Lock(in.X,in.Y));
        if(!row0) return false;
        int pitch=original->GetPitch();
        columns.resize(w);
        for(int x=0;x<w;++x)
            columns[x]=bpp*static_cast<int>(static_cast<int64_t>(x0+x-left)*in.Width/(right-left));
        int previousY=-1;
        for(int y=0;y<h;++y) {
            int sy=static_cast<int>(static_cast<int64_t>(y0+y-top)*in.Height/(bottom-top));
            auto out=pixels.data()+static_cast<size_t>(y)*w*bpp;
            if(sy==previousY) {memcpy(out,out-w*bpp,static_cast<size_t>(w)*bpp);continue;}
            previousY=sy;
            auto row=row0+sy*pitch;
            for(int x=0;x<w;++x) {
                out[x*bpp]=row[columns[x]];
                if(bpp==2) out[x*2+1]=row[columns[x]+1];
            }
        }
        original->Unlock();
        surface.Width=w; surface.Height=h; surface.BytesPerPixel=bpp;
        surface.Buffer=MemoryBuffer(pixels.data(),static_cast<int>(pixels.size()));
        source={0,0,w,h};destination={x0,y0,w,h};position={x0,y0};
        return true;
    }
};
ScaledBlit& scaledBlit() {static ScaledBlit blit;return blit;}
}

// The unit drawer composites body, turret and barrel before this call.
DEFINE_HOOK(0x4373B0, SF_UnitScaleBlit, 7) {
    auto caller=R->Stack<DWORD>(0);
    if(caller!=0x73B37C && caller!=0x73B3EA && caller!=0x73B44B) return 0;
    int scale=VisualScale::percent(R->ESI<TechnoClass*>()->UniqueID);
    if(scale!=100) {
        auto& blit=scaledBlit();
        auto dst=R->Stack<RectangleStruct*>(4);
        if(blit.prepare(R->Stack<Surface*>(8),*R->Stack<RectangleStruct*>(16),{dst->X,dst->Y},*R->EDX<RectangleStruct*>(),scale)) {
            R->Stack(8,&blit.surface);R->Stack(4,&blit.destination);R->Stack(16,&blit.source);
            R->Stack(12,&blit.source);
        }
    }
    return 0;
}
// Aircraft and shadows blit their indexed voxel rasters directly. Bypass
// type-wide cached images so the matching draw goes through these two blits.
DEFINE_HOOK(0x414976, SF_AircraftScaleKey, 5) {
    if(VisualScale::percent(R->EBP<TechnoClass*>()->UniqueID)!=100) R->Stack<int>(0x10,-1);
    return 0;
}
DEFINE_HOOK(0x706BD0, SF_ScaleShadowKey, 5) {
    if(VisualScale::percent(R->ECX<TechnoClass*>()->UniqueID)!=100) {
        R->Stack<int>(0x0C,-1); R->Stack<DWORD>(0x10,0);
    }
    return 0;
}
// At this point DrawVoxel has finished reading its voxel-model scratch slot.
// Keep the scale there until the final blit overwrites the local bullet pointer with screen X.
DEFINE_HOOK(0x46B18E, SF_ProjectileVoxelScale, 9) {
    auto bullet=R->Stack<ObjectClass*>(0x10);
    R->Stack<int>(0x18,VisualScale::projectileRaster(bullet->UniqueID));
    return 0;
}
DEFINE_HOOK(0x4AF2A0, SF_ScaleIndexedVoxel, 7) {
    auto caller=R->Stack<DWORD>(0);
    bool shadow=caller==0x707437;
    int scale=100;
    if(caller==0x46B24D) {
        // 11 arguments and the return address separate the two stack frames.
        scale=R->Stack<int>(0x48);
    } else {
        if(!shadow && caller!=0x70723A) return 0;
        auto unit=shadow?R->EBX<TechnoClass*>():R->EBP<TechnoClass*>();
        if(!shadow && unit->WhatAmI()!=AbstractType::Aircraft) return 0;
        scale=VisualScale::percent(unit->UniqueID);
    }
    if(scale!=100) {
        auto& blit=scaledBlit();
        if(blit.prepare(R->Stack<Surface*>(4),*R->Stack<RectangleStruct*>(8),
            *R->Stack<Point2D*>(12),*R->Stack<RectangleStruct*>(16),scale)) {
            R->Stack(4,&blit.surface);R->Stack(8,&blit.source);R->Stack(12,&blit.position);
        }
    }
    return 0;
}
// Replace only the draw-local shape and frame, shared by body and ground shadow.
DEFINE_HOOK(0x468275, SF_ProjectileSpriteScale, 6) {
    int scale=VisualScale::projectileRaster(R->ESI<ObjectClass*>()->UniqueID);
    if(scale!=100) {
        auto shape=VisualScale::framePair(R->Stack<SHPStruct*>(0x10),R->EAX<int>(),scale);
        if(shape) {R->Stack(0x10,shape);R->EAX(0);}
    }
    return 0;
}
DEFINE_HOOK(0x705E00, SF_SpriteDrawScale, 7) {
    int scale=VisualScale::percent(R->ECX<TechnoClass*>()->UniqueID);
    if(scale!=100) {
        auto shape=VisualScale::framePair(R->Stack<SHPStruct*>(4),R->Stack<int>(8),scale);
        if(shape) {R->Stack(4,shape);R->Stack<int>(8,0);}
    }
    return 0;
}
