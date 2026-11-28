// Local presentation only. Texture axes are transformed in world space BEFORE
// the engine's 45-degree map projection; rotating a flattened sprite is wrong.
namespace Indicator {
constexpr int validRed=64,validGreen=168,validBlue=255;
constexpr ULONGLONG rotationPeriod=6000,reticlePeriod=1500;
double rotation(ULONGLONG milliseconds) {
    return (milliseconds%rotationPeriod)*(6.283185307179586/rotationPeriod);
}
double reticleScale(ULONGLONG milliseconds) {
    // Compress, overshoot, then settle with successively smaller rebounds.
    // Each turning point and the loop seam have zero velocity.
    constexpr double times[]={0,0.14,0.32,0.50,0.68,0.84,1};
    constexpr double scales[]={1,0.90,1.12,0.96,1.02,1,1};
    double phase=static_cast<double>(milliseconds%reticlePeriod)/reticlePeriod;
    for(int i=1;i<7;++i) if(phase<=times[i]) {
        double t=(phase-times[i-1])/(times[i]-times[i-1]);
        t=t*t*(3-2*t);
        return scales[i-1]+(scales[i]-scales[i-1])*t;
    }
    return 1;
}
struct Vec {
    double x=0,y=0;
    Vec operator+(Vec b) const {return {x+b.x,y+b.y};}
    Vec operator-(Vec b) const {return {x-b.x,y-b.y};}
    Vec operator*(double k) const {return {x*k,y*k};}
};
Vec groundVector(double x,double y) {
    return {(x-y)*Unsorted::CellWidthInPixels/(2.0*Unsorted::LeptonsPerCell),
            (x+y)*Unsorted::CellHeightInPixels/(2.0*Unsorted::LeptonsPerCell)};
}
Vec screen(CoordStruct p) {
    auto point=project(p);
    return {static_cast<double>(point.X),static_cast<double>(point.Y)};
}
std::shared_ptr<const PngImage> texture(const std::string& custom,const char* fallback,int resource) {
    if(!custom.empty()) {
        auto image=imageFor(custom,0,std::nullopt,true);
        if(!image->pixels.empty() && image->content.Width>0) return image;
    }
    return imageFor(fallback,resource,std::nullopt,true);
}
struct Region {double x,y,w,h;};
Region content(const PngImage& image) {
    const auto& r=image.content;
    return {static_cast<double>(r.X),static_cast<double>(r.Y),static_cast<double>(r.Width),static_cast<double>(r.Height)};
}
// Source region -> screen parallelogram. The visible bounding box is clipped
// before locking the surface, so huge ranges cannot paint over the sidebar.
bool paint(const PngImage& image,Region src,Vec origin,Vec u,Vec v,bool valid,int opacity) {
    auto surface=DSurface::Composite;
    double determinant=u.x*v.y-u.y*v.x;
    if(!surface || surface->GetBytesPerPixel()!=2 || image.pixels.empty() || src.w<=0 || src.h<=0 ||
       !std::isfinite(determinant) || std::abs(determinant)<0.01) return false;
    if(opacity<=0) return true;
    const auto& clip=DSurface::ViewBounds;
    auto a=origin,b=origin+u,c=origin+v,d=origin+u+v;
    int left=std::max({0,clip.X,static_cast<int>(std::floor(std::min({a.x,b.x,c.x,d.x})))});
    int top=std::max({0,clip.Y,static_cast<int>(std::floor(std::min({a.y,b.y,c.y,d.y})))});
    int right=std::min({surface->GetWidth(),clip.X+clip.Width,static_cast<int>(std::ceil(std::max({a.x,b.x,c.x,d.x})))});
    int bottom=std::min({surface->GetHeight(),clip.Y+clip.Height,static_cast<int>(std::ceil(std::max({a.y,b.y,c.y,d.y})))});
    if(right<=left || bottom<=top) return true;
    auto base=static_cast<BYTE*>(surface->Lock(left,top));if(!base) return false;
    const int pitch=surface->GetPitch();
    const int shifts[]={Drawing::BlueShiftLeft,Drawing::GreenShiftLeft,Drawing::RedShiftLeft};
    const int masks[]={255>>Drawing::BlueShiftRight,255>>Drawing::GreenShiftRight,255>>Drawing::RedShiftRight};
    const double inverse=1.0/determinant;
    for(int y=top;y<bottom;++y) {
        auto row=reinterpret_cast<WORD*>(base+(y-top)*pitch);
        double dx=left+0.5-origin.x,dy=y+0.5-origin.y;
        double s=(dx*v.y-dy*v.x)*inverse,t=(u.x*dy-u.y*dx)*inverse;
        for(int x=left;x<right;++x,s+=v.y*inverse,t-=u.y*inverse) {
            if(s<0 || s>=1 || t<0 || t>=1) continue;
            double sx=std::clamp(src.x+s*src.w-0.5,0.0,static_cast<double>(image.width-1));
            double sy=std::clamp(src.y+t*src.h-0.5,0.0,static_cast<double>(image.height-1));
            int ix=static_cast<int>(sx),iy=static_cast<int>(sy);
            int nx=std::min(ix+1,image.width-1),ny=std::min(iy+1,image.height-1);
            double fx=sx-ix,fy=sy-iy;
            const BYTE* pixels[]={&image.pixels[(static_cast<size_t>(iy)*image.width+ix)*4],
                &image.pixels[(static_cast<size_t>(iy)*image.width+nx)*4],
                &image.pixels[(static_cast<size_t>(ny)*image.width+ix)*4],
                &image.pixels[(static_cast<size_t>(ny)*image.width+nx)*4]};
            const double weights[]={(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
            double alpha=0,channels[3]{};
            for(int i=0;i<4;++i) {
                double weight=weights[i]*pixels[i][3];alpha+=weight;
                for(int k=0;k<3;++k) channels[k]+=weight*pixels[i][k];
            }
            if(alpha<0.5) continue;
            // Interpolate premultiplied colors to avoid dark fringes at alpha edges.
            for(double& channel:channels) channel/=alpha;
            if(valid) {
                double light=std::max({channels[0],channels[1],channels[2]});
                channels[0]=light;channels[1]=light*validGreen/255;channels[2]=light*validRed/255;
            } else {
                double light=(channels[2]*54+channels[1]*183+channels[0]*19)/256;
                channels[2]=light;channels[1]=light*0.25;channels[0]=light*0.20;
            }
            int blend=std::clamp(static_cast<int>(alpha*opacity/100.0+0.5),0,255);
            if(!blend) continue;
            WORD background=row[x-left];int result=0;
            for(int k=0;k<3;++k) {
                int fg=std::clamp(static_cast<int>(channels[k]*masks[k]/255+0.5),0,masks[k]);
                int bg=(background>>shifts[k])&masks[k];
                result|=((fg*blend+bg*(255-blend)+127)/255)<<shifts[k];
            }
            row[x-left]=static_cast<WORD>(result);
        }
    }
    surface->Unlock();return true;
}
bool marker(CoordStruct center,int radius,const std::string& custom,bool area,bool valid,int opacity,
            ULONGLONG milliseconds=GetTickCount64()) {
    auto image=texture(custom,area?"sfindicator_ring.png":"sfindicator_target.png",area?111:112);
    double angle=area?rotation(milliseconds):0;
    double diameter=radius*2.0*(area?1:reticleScale(milliseconds));
    double sine=std::sin(angle),cosine=std::cos(angle);
    auto x=groundVector(diameter*cosine,diameter*sine);
    auto y=groundVector(-diameter*sine,diameter*cosine);
    return paint(*image,content(*image),screen(center)-(x+y)*0.5,x,y,valid,opacity);
}
struct ArrowSlices {
    double head=0,tail=0,middle=0,headPixels=0,tailPixels=0;
};
ArrowSlices slices(double length,double width,Region src,int headPercent,int tailPercent) {
    if(length<=0 || width<=0 || src.w<=0 || src.h<=0) return {};
    double scale=width/src.w;
    double head=std::min(length,src.h*headPercent/100.0*scale);
    double tail=std::min(length-head,src.h*tailPercent/100.0*scale);
    return {head,tail,std::max(0.0,length-head-tail),head/scale,tail/scale};
}
bool direction(CoordStruct from,CoordStruct to,const ActiveSkills::Spec& spec,bool valid) {
    double dx=static_cast<double>(to.X)-from.X,dy=static_cast<double>(to.Y)-from.Y,length=std::hypot(dx,dy);
    if(length<1) return false;
    auto image=texture(spec.arrowTexture,"sfindicator_arrow.png",113);
    auto src=content(*image);if(src.w<=0 || src.h<=0) return false;
    double width=spec.arrowWidth*Unsorted::LeptonsPerCell;
    auto parts=slices(length,width,src,spec.arrowHead,spec.arrowTail);
    auto start=screen(from),end=screen(to),along=(end-start)*(1.0/length);
    auto across=groundVector(dy/length*width,-dx/length*width);
    auto section=[&](double begin,double size,Region region) {
        if(size<=0) return true;
        // PNG points up: source top maps toward the endpoint, bottom toward caster.
        return paint(*image,region,start+along*(begin+size)-across*0.5,across,along*(-size),valid,spec.indicatorOpacity);
    };
    const double headPixels=src.h*spec.arrowHead/100.0,tailPixels=src.h*spec.arrowTail/100.0;
    bool ok=section(0,parts.tail,{src.x,src.y+src.h-parts.tailPixels,src.w,parts.tailPixels});
    ok=section(parts.tail,parts.middle,{src.x,src.y+headPixels,src.w,src.h-headPixels-tailPixels}) && ok;
    ok=section(length-parts.head,parts.head,{src.x,src.y,src.w,parts.headPixels}) && ok;
    return ok;
}
CoordStruct ground(TechnoClass* unit) {
    auto at=unit->GetCoords();
    if(auto cell=unit->GetCell()) at.Z=cell->GetCoordsWithBridge().Z;
    return at;
}
}
