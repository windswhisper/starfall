#include "VisualScale.h"
#include "ForceMove.h"
#include "../modules/ActSp/HighPlatforms.h"
#include <FootClass.h>
#include <UnitClass.h>
#include <AnimClass.h>
#include <BuildingClass.h>
#include <DisplayClass.h>
#include <CCINIClass.h>
#include <Unsorted.h>
#include <Surface.h>
#include <TacticalClass.h>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>
#include <vector>

namespace VisualScale {
static int placementScale=100;
static std::map<BuildingTypeClass*,int> placementScales;
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
void setStateFactor(TechnoClass* unit,double factor) {
    auto entry=state.find(unit->UniqueID);
    const double previous=entry==state.end()?1.0:entry->second;
    if(previous==factor) return;
    if(factor<previous && unit->WhatAmI()==AbstractType::Building && unit->IsOnMap &&
       !unit->InLimbo && percent(unit->UniqueID)>100) {
        // MarkForRedraw uses the new dimensions next frame. Clear the old
        // overhang too when a temporary enlargement ends or weakens.
        RectangleStruct oldBounds{};
        unit->GetRenderDimensions(&oldBounds);
        TacticalClass::Instance->RegisterDirtyArea(oldBounds,false);
    }
    if(factor==1.0) state.erase(entry);
    else state.insert_or_assign(unit->UniqueID,factor);
    unit->MarkForRedraw();
}
int placementPercent(BuildingTypeClass* type) {
    if(!type) return 100;
    auto [entry,fresh]=placementScales.try_emplace(type,100);
    if(fresh) {
        const double factor=CCINIClass::INI_Art.ReadDouble(type->ImageFile,"Starfall.PlacementPreviewScale",1.0);
        if(std::isfinite(factor) && factor>=0.01 && factor<=2.0) entry->second=rasterPercent(factor*100.0);
    }
    return entry->second;
}
static int projectileRaster(DWORD id) {
    auto it=projectiles.find(id);
    return it==projectiles.end()?100:rasterPercent(it->second);
}
static int animationPercent(ObjectClass* object) {
    auto id=object->UniqueID;
    if(auto it=attachedAnims.find(id);it!=attachedAnims.end()) return percent(it->second);
    if(auto it=explosions.find(id);it!=explosions.end()) return rasterPercent(it->second);
    if(object->WhatAmI()==AbstractType::Anim) {
        auto anim=static_cast<AnimClass*>(object);
        if(anim->IsBuildingAnim) {
            if(anim->OwnerObject) return percent(anim->OwnerObject->UniqueID);
            // Native building animations live in their building's slot array,
            // often without OwnerObject. Resolve once, also after a save load.
            for(auto building:BuildingClass::Array)
                for(auto slot:building->Anims) if(slot==anim) {
                    attachedAnims[id]=building->UniqueID;
                    return percent(building->UniqueID);
                }
        }
    } else if(object->WhatAmI()==AbstractType::Building) return percent(id);
    return 100;
}
namespace {
using Bytes=std::vector<BYTE>;
using FrameKey=std::tuple<SHPStruct*,int,int,int,int,bool>;
std::map<FrameKey,Bytes> sprites;
size_t cacheBytes=0;
int cacheFrame=-1;

int scaled(int value,int percent) {
    return static_cast<int>(std::lround(value*(percent/100.0)));
}
// Some infantry art paints its contact shadow into the body with palette
// index 12. The same index can occur on the weapon. Only remove a broad,
// shallow patch connected to the very bottom of the visible silhouette.
void removeContactShadow(Bytes& pixels,int width,int height) {
    constexpr BYTE ink=12;
    const auto last=std::find_if(pixels.rbegin(),pixels.rend(),[](BYTE p){return p!=0;});
    if(last==pixels.rend()) return;
    const int bottom=static_cast<int>(pixels.size()-1-(last-pixels.rbegin()))/width;
    const auto base=pixels.begin()+bottom*width;
    if(std::find(base,base+width,ink)==base+width) return;
    Bytes seen(pixels.size());
    std::vector<int> patch;
    for(int x=0;x<width;++x) {
        const int start=bottom*width+x;
        if(pixels[start]!=ink || seen[start]) continue;
        patch.assign(1,start);seen[start]=1;
        int left=x,right=x,top=bottom;
        for(size_t i=0;i<patch.size();++i) {
            int px=patch[i]%width,py=patch[i]/width;
            left=std::min(left,px);right=std::max(right,px);top=std::min(top,py);
            for(int y=std::max(0,py-1);y<=std::min(bottom,py+1);++y)
                for(int nx=std::max(0,px-1);nx<=std::min(width-1,px+1);++nx) {
                    int n=y*width+nx;
                    if(!seen[n] && pixels[n]==ink) {seen[n]=1;patch.push_back(n);}
                }
        }
        if(top>bottom/2 && right-left+1>=std::max(4,width/3) && right-left>bottom-top)
            for(int n:patch) pixels[n]=0;
    }
}
bool appendFrame(SHPStruct* source,SHPFile* file,int index,int scale,int slot,int canvasW,int canvasH,Bytes& output,bool cleanBody=false) {
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
    auto readRow=[&](BYTE* row) {
        if(format==3) {
            int size=src[0]+256*src[1];
            if(size<2 || size>2*h.Width+2) return false;
            std::fill_n(row,h.Width,0);
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
            std::copy_n(src,stride,row);src+=stride;
        }
        return true;
    };
    Bytes body,line(h.Width);
    if(cleanBody) {
        body.resize(h.Width*h.Height);
        for(int y=0;y<h.Height;++y)
            if(!readRow(body.data()+y*h.Width)) return false;
        removeContactShadow(body,h.Width,h.Height);
    }
    const int offset=static_cast<int>(output.size());
    for(int y=0,outY=y0;y<h.Height && outY<y1;++y) {
        const BYTE* row=cleanBody?body.data()+y*h.Width:line.data();
        if(!cleanBody && !readRow(line.data())) return false;
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
SHPStruct* framePair(SHPStruct* source,int index,int scale,bool cleanBody=false) {
    if(!source || index<0 || index>=source->Frames) return nullptr;
    if(cacheBytes>8*1024*1024 && cacheFrame!=Unsorted::CurrentFrame) clearSprites();
    cacheFrame=Unsorted::CurrentFrame;
    auto canvas=[&](int original,int screen){return std::clamp(scaled(original,scale),1,std::min(32000,2*(screen+512)));};
    int width=canvas(source->Width,DSurface::ViewBounds.Width),height=canvas(source->Height,DSurface::ViewBounds.Height);
    FrameKey key{source,index,scale,width,height,cleanBody};
    auto cached=sprites.find(key);
    if(cached==sprites.end()) {
        auto file=source->GetData();
        if(!file || file->Width<=0 || file->Height<=0 || file->Width>2048 || file->Height>2048) return nullptr;
        Bytes data(sizeof(SHPStruct)+2*sizeof(SHPFrame),0);
        short header[]={0,static_cast<short>(width),static_cast<short>(height),2};
        static_assert(sizeof(header)==sizeof(SHPStruct) && sizeof(SHPFrame)==24);
        memcpy(data.data(),header,sizeof(header));
        if(!appendFrame(source,file,index,scale,0,width,height,data,cleanBody) ||
           !appendFrame(source,file,index+file->Frames/2,scale,1,width,height,data)) return nullptr;
        cacheBytes+=data.size();
        cached=sprites.emplace(key,std::move(data)).first;
    }
    return reinterpret_cast<SHPStruct*>(cached->second.data());
}
}
void clearSprites() { sprites.clear();cacheBytes=0;placementScales.clear(); }
}

// Phobos draws its placement ghost directly through CC_Draw_Shape, before the
// product is on the map. Bracket that call without replacing its preview hook.
DEFINE_HOOK(0x6D5284,SF_PlacementScaleBegin,6) {
    VisualScale::placementScale=100;
    if(R->EBX<int>()>=R->ECX<int>()) {
        auto object=DisplayClass::Instance.CurrentBuilding;
        if(object && object->WhatAmI()==AbstractType::Building)
            VisualScale::placementScale=VisualScale::placementPercent(static_cast<BuildingClass*>(object)->Type);
    }
    return 0;
}
DEFINE_HOOK(0x6D5290,SF_PlacementScaleEnd,5) {
    VisualScale::placementScale=100;return 0;
}
namespace {
struct LiftedSpriteDraw {
    Surface* scene;
    int lift;
    Point2D shadow;
};
LiftedSpriteDraw* liftedSprite=nullptr;
}
// Vanilla and Ares choose different passes for SHP vehicle shadows. Keep
// their choice and offset only the shadow blit that reaches the scene.
DEFINE_HOOK(0x73D361,SF_LiftedSpriteVehicleDraw,6) {
    auto unit=R->ESI<UnitClass*>();
    const int lift=ForceMove::screenLift(unit);
    if(!lift && !liftedSprite) return 0;
    LiftedSpriteDraw draw{DSurface::Temp,lift,{}};
    auto previous=std::exchange(liftedSprite,lift?&draw:nullptr);
    unit->DrawAsSHP(R->Stack<Point2D>(0x1C),*R->EBX<RectangleStruct*>(),R->EBP<int>(),0);
    liftedSprite=previous;
    return 0x73D395;
}
DEFINE_HOOK(0x4AED70,SF_PlacementShapeScale,6) {
    const auto caller=R->Stack<DWORD>(0);
    if(caller==0x70645D || caller==0x706532) {
        const auto object=R->ESI<TechnoClass*>();
        if(HighPlatforms::drawsPlatformBody(object)) {
            // Use the body silhouette as the depth mask for rear units.
            constexpr auto depthFlags=static_cast<unsigned>(BlitterFlags::ZRead|BlitterFlags::ZReadWrite);
            R->Stack(0x14,(R->Stack<unsigned>(0x14)&~depthFlags)|static_cast<unsigned>(BlitterFlags::ZReadWrite));
            R->Stack<SHPStruct*>(0x2C,nullptr);
        } else if(const int offset=HighPlatforms::deckDepthOffset(object)) {
            R->Stack(0x1C,R->Stack<int>(0x1C)-offset);
        }
    }
    if(liftedSprite && R->ECX<Surface*>()==liftedSprite->scene &&
       (R->Stack<DWORD>(0x14)&static_cast<DWORD>(BlitterFlags::Darken))) {
        auto shape=R->Stack<SHPStruct*>(4);
        const int frame=R->Stack<int>(8);
        if(shape && frame>=shape->Frames/2 && frame<shape->Frames) {
            liftedSprite->shadow=*R->Stack<Point2D*>(0xC);
            liftedSprite->shadow.Y+=liftedSprite->lift;
            R->Stack(0xC,&liftedSprite->shadow);
        }
    }
    if(VisualScale::placementScale!=100) {
        if(auto shape=VisualScale::framePair(R->Stack<SHPStruct*>(4),R->Stack<int>(8),VisualScale::placementScale)) {
            R->Stack(4,shape);R->Stack<int>(8,0);
        }
    }
    return 0;
}

// All ordinary animation palette paths meet here with the resolved SHP and
// frame in draw-local slots. The native drawer retains lighting and blending.
DEFINE_HOOK(0x423358, SF_ExplosionSpriteScale, 6) {
    if(VisualScale::explosions.empty() && VisualScale::attachedAnims.empty() && VisualScale::state.empty() && VisualScale::base.empty()) return 0;
    const int scale=VisualScale::animationPercent(R->ESI<ObjectClass*>());
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
    if(VisualScale::explosions.empty() && VisualScale::attachedAnims.empty() && VisualScale::state.empty() && VisualScale::base.empty()) return 0;
    auto object=R->Stack<ObjectClass*>(0x0C);
    int scale=VisualScale::animationPercent(object);
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

// BuildingClass has its own GetRenderDimensions, including a camera-position
// cache. Both exits have ESI pointing at the completed, unscaled cached rect.
// Expand only the caller's copy so repeated draws and scale changes stay stable.
DEFINE_HOOK_AGAIN(0x455D2C, SF_BuildingRenderBounds, 7)
DEFINE_HOOK(0x455C96, SF_BuildingRenderBounds, 7) {
    if(VisualScale::state.empty() && VisualScale::base.empty()) return 0;
    static_assert(offsetof(BuildingClass,unknown_rect_63C)==0x63C);
    auto building=reinterpret_cast<BuildingClass*>(R->ESI()-offsetof(BuildingClass,unknown_rect_63C));
    const int scale=VisualScale::percent(building->UniqueID);
    if(scale<=100) return 0;
    // On a cache miss the final native height store is one of the displaced
    // instructions. Commit it before returning, preserving the native cache.
    if(R->Origin()==0x455D2C) building->unknown_rect_63C.Height=R->EAX<int>();
    const auto& native=building->unknown_rect_63C;
    auto image=building->Type->GetImage();
    // Frames are magnified around the SHP canvas center. The native rect can
    // enclose only opaque pixels, so its half-width alone is not a safe margin.
    auto margin=[scale](int span,int screen) {
        return static_cast<int>(std::min<int64_t>((int64_t(span)*(scale-100)+199)/200,screen+512));
    };
    int dx=margin(std::max(native.Width,image?int(image->Width):0),DSurface::ViewBounds.Width);
    int dy=margin(std::max(native.Height,image?int(image->Height):0),DSurface::ViewBounds.Height);
    auto result=R->Stack<RectangleStruct*>(0x48);
    *result={native.X-dx,native.Y-dy,native.Width+2*dx,native.Height+2*dy};
    R->EAX(result);
    return 0x455D44;
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

    bool prepare(Surface* original,RectangleStruct in,Point2D dest,RectangleStruct clip,int percent,
                 const Point2D* screenPivot=nullptr) {
        if(!original || in.Width<=0 || in.Height<=0) return false;
        int bpp=original->GetBytesPerPixel();
        if(bpp!=1 && bpp!=2) return false;
        const double zoom=percent/100.0;
        // The raster origin is at the center of the voxel surface. Splitting
        // water/bridge draws preserves the same pivot for both pieces.
        const int pivotX=screenPivot?in.X+screenPivot->X-dest.X:original->GetWidth()/2;
        const int pivotY=screenPivot?in.Y+screenPivot->Y-dest.Y:original->GetHeight()/2;
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

// Shrink the shared body transform before either the native or a deferred
// renderer receives the voxels. Turret/barrel offsets derive from this matrix.
// The type-wide voxel cache is keyed by facing, not by per-unit scale.
DEFINE_HOOK(0x73B5DB, SF_ShrinkUnitVoxel, 9) {
    const int scale=VisualScale::percent(R->EBP<UnitClass*>()->UniqueID);
    if(scale<100) {
        reinterpret_cast<Matrix3D*>(R->ESP()+0xD4)->Scale(scale/100.0f);
        R->Stack<int>(0x14,-1);
    }
    return 0;
}
// The unit drawer composites body, turret and barrel before this call.
DEFINE_HOOK(0x4373B0, SF_UnitScaleBlit, 7) {
    auto caller=R->Stack<DWORD>(0);
    if(caller!=0x73B37C && caller!=0x73B3EA && caller!=0x73B44B) return 0;
    const auto unit=R->ESI<TechnoClass*>();
    if(const int offset=HighPlatforms::deckDepthOffset(unit)) R->Stack(0x18,R->Stack<int>(0x18)-offset);
    int scale=VisualScale::percent(unit->UniqueID);
    // All three blits have ten stack arguments. Beyond them and DrawObject's
    // saved registers/locals is its caller: DrawAsVXL or the SHP compositor.
    if(scale<100 && R->Stack<DWORD>(0x74)==0x73C1AB) return 0;
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
DEFINE_HOOK(0x706640, SF_BuildingVoxelScaleKey, 7) {
    auto unit=R->ECX<TechnoClass*>();
    if(unit->WhatAmI()==AbstractType::Building && VisualScale::percent(unit->UniqueID)!=100) {
        R->Stack<int>(0x0C,-1);R->Stack<DWORD>(0x10,0);
    }
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
    Point2D buildingOrigin{};
    const Point2D* pivot=nullptr;
    if(caller==0x46B24D) {
        // 11 arguments and the return address separate the two stack frames.
        scale=R->Stack<int>(0x48);
    } else {
        if(!shadow && caller!=0x70723A) return 0;
        auto unit=shadow?R->EBX<TechnoClass*>():R->EBP<TechnoClass*>();
        if(!shadow && unit->WhatAmI()!=AbstractType::Aircraft && unit->WhatAmI()!=AbstractType::Building) return 0;
        scale=VisualScale::percent(unit->UniqueID);
        if(scale!=100 && !shadow && unit->WhatAmI()==AbstractType::Building) {
            // The native voxel drawer passes the mounted screen position at
            // esp+14C before pushing the final blit's 11 arguments and return.
            // Each voxel has its own cropped raster, including the cannon barrel.
            auto building=static_cast<BuildingClass*>(unit);
            buildingOrigin=*R->Stack<Point2D*>(0x17C)-building->Type->GetBuildingAnim(BuildingAnimSlot::Turret).Position;
            pivot=&buildingOrigin;
        }
    }
    if(scale!=100) {
        auto& blit=scaledBlit();
        if(blit.prepare(R->Stack<Surface*>(4),*R->Stack<RectangleStruct*>(8),
            *R->Stack<Point2D*>(12),*R->Stack<RectangleStruct*>(16),scale,pivot)) {
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
    auto unit=R->ECX<TechnoClass*>();
    int scale=VisualScale::percent(unit->UniqueID);
    bool cleanBody=ForceMove::screenLift(unit)>0 && unit->WhatAmI()==AbstractType::Infantry;
    if(scale!=100 || cleanBody) {
        auto shape=VisualScale::framePair(R->Stack<SHPStruct*>(4),R->Stack<int>(8),scale,cleanBody);
        if(shape) {R->Stack(4,shape);R->Stack<int>(8,0);}
    }
    return 0;
}
