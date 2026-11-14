#include "../../../src/ResearchUI.cpp"
#include <fstream>
#include <Unsorted.h>

namespace {
void check(bool ok,const char* label) {
    if(ok) return;
    std::ofstream("hex-icons.log",std::ios::app)<<"FAIL "<<label<<"\n";
    TerminateProcess(GetCurrentProcess(),2);
}
std::vector<WORD> snapshot(RectangleStruct rect) {
    auto surface=DSurface::Composite;
    int pitch=surface->GetPitch();
    auto base=static_cast<const BYTE*>(surface->Lock(rect.X,rect.Y));
    check(base!=nullptr,"surface lock");
    std::vector<WORD> result(rect.Width*rect.Height);
    for(int y=0;y<rect.Height;++y) memcpy(result.data()+y*rect.Width,base+y*pitch,rect.Width*2);
    surface->Unlock();
    return result;
}
void capture(RectangleStruct rect) {
    std::ofstream f("hex-icons.ppm",std::ios::binary);
    f<<"P6\n"<<rect.Width<<" "<<rect.Height<<"\n255\n";
    int shifts[]={Drawing::RedShiftLeft,Drawing::GreenShiftLeft,Drawing::BlueShiftLeft};
    int masks[]={255>>Drawing::RedShiftRight,255>>Drawing::GreenShiftRight,255>>Drawing::BlueShiftRight};
    for(auto value:snapshot(rect)) for(int k=0;k<3;++k)
        f.put(static_cast<char>(((value>>shifts[k])&masks[k])*255/masks[k]));
}
}
DEFINE_HOOK(0x55B4EB,SF_TestHexIcons,5) {
    if(GetFileAttributesA("Starfall.test")==INVALID_FILE_ATTRIBUTES || Unsorted::CurrentFrame!=20) return 0;
    Research::ensureReady();
    {
        const wchar_t* filename=L"图标验证.png";
        HMODULE module=nullptr;
        check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&loadPng),&module),"PNG module");
        auto resource=FindResourceW(module,MAKEINTRESOURCEW(104),MAKEINTRESOURCEW(10));
        check(resource!=nullptr,"PNG resource");
        auto size=SizeofResource(module,resource);
        auto bytes=LockResource(LoadResource(module,resource));
        std::ofstream file(filename,std::ios::binary);file.write(static_cast<const char*>(bytes),size);file.close();
        PngImage decoded;
        check(loadPng("图标验证.png",0,decoded) && !decoded.pixels.empty(),"UTF8 PNG file loaded");
        DeleteFileW(filename);
        std::ofstream("hex-icons.log",std::ios::app)<<"PASS UTF8 PNG file loaded\n";
    }
    auto& projects=Research::options;
    check(projects.size()>=15,"project count");
    auto surface=DSurface::Composite;
    RectangleStruct sheet{8,8,480,288};
    int background=color(14,23,35);
    surface->FillRect(&sheet,background);
    std::set<std::string> unique;
    int rareCount=0;
    for(int i=0;i<15;++i) {
        const auto& o=projects[i];
        RectangleStruct r{sheet.X+(i%5)*96,sheet.Y+(i/5)*96,96,96};
        check(unique.insert(o.icon).second,"unique file");
        PngImage raw;
        check(loadPng(o.icon,0,raw) && raw.width==96 && raw.height==96,"registered image");
        drawPng(raw,r,0);
        auto original=snapshot(r);
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
        auto actual=snapshot(r);
        bool rare=o.rarity==Research::Rarity::Rare;
        check((original!=actual)==(o.rarity!=Research::Rarity::Common) && actual.front()==background,"rarity and transparent corner");
        if(rare) {
            ++rareCount;
            size_t blue=0;
            for(auto value:actual) {
                if(value==background) continue;
                int b=((value>>Drawing::BlueShiftLeft)&(255>>Drawing::BlueShiftRight))<<Drawing::BlueShiftRight;
                int g=((value>>Drawing::GreenShiftLeft)&(255>>Drawing::GreenShiftRight))<<Drawing::GreenShiftRight;
                if(b>g+24) ++blue;
            }
            check(blue>700,"blue-violet coverage");
        }
        Research::Option epic=o;
        epic.rarity=Research::Rarity::Epic;
        surface->FillRect(&r,background);
        drawProjectIcon(epic,r);
        auto gold=snapshot(r);
        size_t goldPixels=0;
        for(auto value:gold) {
            if(value==background) continue;
            int red=((value>>Drawing::RedShiftLeft)&(255>>Drawing::RedShiftRight))<<Drawing::RedShiftRight;
            int green=((value>>Drawing::GreenShiftLeft)&(255>>Drawing::GreenShiftRight))<<Drawing::GreenShiftRight;
            int blue=((value>>Drawing::BlueShiftLeft)&(255>>Drawing::BlueShiftRight))<<Drawing::BlueShiftRight;
            if(red>blue+40 && green>blue+20) ++goldPixels;
        }
        check(gold!=original && gold.front()==background && goldPixels>700,"gold coverage and transparent corner");
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
    }
    check(rareCount==5,"rare count");
    capture(sheet);
    std::ofstream("hex-icons.log",std::ios::app)<<"PASS atlas and rarity\n";
    RectangleStruct r{8,8,30,30};
    for(const char* name:{"hx-test.pcx","missing-hx.png"}) {
        Research::Option o;
        o.icon=name;
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
        auto original=snapshot(r);
        o.rarity=Research::Rarity::Rare;
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
        auto tinted=snapshot(r);
        check(original!=tinted && tinted[15*30+15]==background && tinted[4*30+15]!=background,"small transparent PCX or fallback");
        o.rarity=Research::Rarity::Epic;
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
        auto gold=snapshot(r);
        check(gold!=original && gold!=tinted && gold[15*30+15]==background && gold[4*30+15]!=background,"gold transparent PCX or fallback");
        o.rarity=Research::Rarity::Insane;
        surface->FillRect(&r,background);
        drawProjectIcon(o,r);
        auto red=snapshot(r);
        auto p=red[4*30+15];
        auto channel=[&](int left,int right){return ((p>>left)&(255>>right))<<right;};
        int rr=channel(Drawing::RedShiftLeft,Drawing::RedShiftRight);
        check(red[15*30+15]==background && rr>channel(Drawing::GreenShiftLeft,Drawing::GreenShiftRight)+20 &&
              rr>channel(Drawing::BlueShiftLeft,Drawing::BlueShiftRight)+20,"red transparent PCX or fallback");
    }
    std::ofstream("hex-icons.log",std::ios::app)<<"PASS PCX and fallback\n";
    {
        auto common=Research::Rarity::Common;
        Research::clearPresentationCaches();
        auto hot=imageFor("sfrdefault.png",104,common,true);
        check(!hot->pixels.empty() && imageFor("SFRDEFAULT.PNG",104,common,true)==hot,"image path aliases share cache");
        for(int i=0;i<1000;++i) {
            auto file="cache-icon-"+std::to_string(i)+".png";
            check(!imageFor(file,104,common,true)->pixels.empty(),"load generated icon alias");
            check(imageFor("sfrdefault.png",104,common,true)==hot,"keep frequently used icon");
            check(imageCache().size()<=256 && imageCache().bytes()<=imageCache().maxBytes,"image cache bounds");
        }
        check(!imageCache().contains(ImageKey{"cache-icon-0.png",104,common}),"old icon evicted");
        check(imageFor("cache-missing-image.png",0,common,true)->pixels.empty() &&
              !imageFor("cache-missing-image.png",104,common,true)->pixels.empty(),"builtin fallback has independent cache identity");
        for(int i=0;i<20;++i) imageCache().get(ImageKey{"generated-large-"+std::to_string(i),0,common},[] {
            PngImage image;image.width=image.height=1024;image.pixels.resize(4*1024*1024);return image;
        },[](const ImageKey& key,const PngImage& image){return image.pixels.capacity()+2*(std::get<0>(key).size()+1)+sizeof(PngImage);});
        check(imageCache().bytes()<=imageCache().maxBytes &&
              !imageCache().contains(ImageKey{"generated-large-0",0,common}) && !hot->pixels.empty(),"byte pressure evicts without invalidating held icon");
        std::ofstream("hex-icons.log",std::ios::app)<<"PASS bounded image cache and LRU\n";

        const char* file="cache-reload.png";
        DeleteFileA(file);
        check(imageFor(file,0,common,true)->pixels.empty(),"missing image cached");
        auto writeResource=[&](int id) {
            HMODULE module=nullptr;
            check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&loadPng),&module)!=0,"reload image module");
            auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
            auto bytes=LockResource(LoadResource(module,resource));auto size=SizeofResource(module,resource);
            check(bytes && size,"reload image resource");
            std::ofstream out(file,std::ios::binary);out.write(static_cast<const char*>(bytes),size);
        };
        writeResource(104);
        check(imageFor(file,0,common,true)->pixels.empty(),"missing image is not reopened every frame");
        Research::clearPresentationCaches();
        auto original=imageFor(file,0,common,true);
        check(original->width==96,"missing image retried after invalidation");
        writeResource(110);
        check(imageFor(file,0,common,true)==original,"file stays cached until reload");
        drawCardSkin({8,8,96,144});
        check(!cardSkinCache().pixels.empty(),"card skin cached");
        Research::clearPresentationCaches();
        check(imageCache().size()==0 && imageCache().bytes()==0 && cardSkinCache().pixels.empty(),"all image caches cleared");
        check(imageFor(file,0,common,true)->width==64 && original->width==96,"replaced image reloaded with safe old handle");
        DeleteFileA(file);
        std::ofstream("hex-icons.log",std::ios::app)<<"PASS image reload and card cache cleanup\n";
    }
    TerminateProcess(GetCurrentProcess(),0);
    return 0;
}
