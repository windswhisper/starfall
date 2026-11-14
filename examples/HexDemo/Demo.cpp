// Demonstration-only build: the normal Starfall DLL is not modified.
#include "../../src/Starfall.cpp"
#include <Drawing.h>
#include <WWMouseClass.h>
EXPORT_FUNC(SF_ResearchInput);
namespace {
void saveFrame(const char* path) {
    auto surface=DSurface::Composite; const auto v=DSurface::ViewBounds;
    if(!surface || v.Width<=0 || v.Height<=0) return;
    auto base=static_cast<BYTE*>(surface->Lock(v.X,v.Y)); if(!base) return;
    int stride=(v.Width*3+3)&~3; std::vector<BYTE> pixels(stride*v.Height);
    for(int y=0;y<v.Height;++y) {
        auto src=reinterpret_cast<const WORD*>(base+y*surface->GetPitch());
        auto dst=pixels.data()+(v.Height-1-y)*stride;
        for(int x=0;x<v.Width;++x) {
            WORD c=src[x];
            dst[3*x+0]=static_cast<BYTE>(((c>>Drawing::BlueShiftLeft)&(255>>Drawing::BlueShiftRight))*255/(255>>Drawing::BlueShiftRight));
            dst[3*x+1]=static_cast<BYTE>(((c>>Drawing::GreenShiftLeft)&(255>>Drawing::GreenShiftRight))*255/(255>>Drawing::GreenShiftRight));
            dst[3*x+2]=static_cast<BYTE>(((c>>Drawing::RedShiftLeft)&(255>>Drawing::RedShiftRight))*255/(255>>Drawing::RedShiftRight));
        }
    }
    surface->Unlock();
    BITMAPFILEHEADER fh{}; BITMAPINFOHEADER ih{}; fh.bfType=0x4d42; fh.bfOffBits=sizeof(fh)+sizeof(ih); fh.bfSize=fh.bfOffBits+static_cast<DWORD>(pixels.size());
    ih.biSize=sizeof(ih); ih.biWidth=v.Width; ih.biHeight=v.Height; ih.biPlanes=1; ih.biBitCount=24; ih.biCompression=BI_RGB; ih.biSizeImage=static_cast<DWORD>(pixels.size());
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<char*>(&fh),sizeof(fh));f.write(reinterpret_cast<char*>(&ih),sizeof(ih));f.write(reinterpret_cast<char*>(pixels.data()),pixels.size());
}
void demoClick(Point2D p) {
    if(auto mouse=WWMouseClass::Instance) { mouse->SetCoords(p); mouse->XY1=p; }
    REGISTERS regs{}; DWORD stack[20]{}, key=0;
    regs.ESP(stack); regs.Stack(0x1c,p); regs.Stack(0x38,&key);
    regs.Stack(0x34,1); SF_ResearchInput(&regs);
    regs.Stack(0x34,4); SF_ResearchInput(&regs);
}
}
DEFINE_HOOK(0x55B4EB,SF_HexDemoAutoplay,5) {
    if(GetFileAttributesA("Starfall.HexDemo.enabled")==INVALID_FILE_ATTRIBUTES) return 0;
    auto house=HouseClass::CurrentPlayer;
    if(!house || !house->Type) return 0;
    const int frame=Unsorted::CurrentFrame;
    if(frame==30) {
        Research::ensureReady();
        const auto& offers=Research::offers(house->ArrayIndex,house->Type->ID);
        if(offers.size()==3 && Research::options[offers[0]].id=="sf_hx_tankengine") {
            const auto view=DSurface::ViewBounds;
            demoClick({view.X+view.Width-32,view.Y+48});
        }
    }
    if(frame==90 && Research::expanded && !Research::rewardPanel) {
        const auto view=DSurface::ViewBounds;
        const int gap=view.Width>=720?16:8;
        const int width=std::min({244,(view.Width-24-2*gap)/3,(view.Height-60)*2/3});
        const int height=width*3/2;
        const int panelWidth=3*width+2*gap, panelHeight=38+height;
        demoClick({view.X+(view.Width-panelWidth)/2+width/2,
                   view.Y+(view.Height-panelHeight)/2+38+height/2});
    }
    if(GetFileAttributesA("Starfall.HexDemo.test")!=INVALID_FILE_ATTRIBUTES) {
        if(frame==50) saveFrame("hex-demo-cards.bmp");
        if(frame==300) saveFrame("hex-demo-battle.bmp");
        if(frame==300) {
            std::ofstream f("hex-demo-test.log",std::ios::app);
            f<<"projects="<<Research::options.size()<<" owned="<<Research::owns(house->ArrayIndex,"sf_hx_tankengine")
             <<" points="<<Research::balance(house->ArrayIndex)<<" teams="<<TeamClass::Array.Count<<"\n";
        }
        if(frame==700) TerminateProcess(GetCurrentProcess(),0);
    }
    return 0;
}
