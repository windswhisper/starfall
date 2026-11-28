#include "ResearchInternal.h"
#include "ConfigText.h"
#include "ActiveSkills.h"
#include "../modules/ActSp/CommandBarOrders.h"
#include "ForceMove.h"
#include <DisplayClass.h>
#include <FileSystem.h>
#include <deque>
#include <set>
#include <EventClass.h>
#include <MessageListClass.h>
#include <HouseClass.h>
#include <Drawing.h>
#include <PCX.h>
#include "ResearchText.h"
#include "ResearchImages.h"
#include "SonicRipple.h"
#include "ResearchAudio.h"
#include <WWMouseClass.h>
#include <ShapeButtonClass.h>
#include <CCToolTip.h>
#include <BuildingClass.h>
#include <TacticalClass.h>
#include <CellClass.h>
#include <RulesClass.h>
#include <VocClass.h>
#include <gdiplus.h>
#include <objidl.h>
#include <cmath>
#include <sstream>
#include <tuple>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

void Research::Detail::showResearchSelection(HouseClass* house,int option) {
    if(!Research::announceSelection) return;
    std::wstring player=house->UIName;
    if(player.empty()) player=L"玩家 "+std::to_wstring(house->ArrayIndex+1);
    // Keep the full message within the engine's 162-character message buffer.
    const auto message=player+L" 已研究「"+Research::options[option].name.substr(0,120)+L"」。";
    MessageListClass::Instance.PrintMessage(message.c_str(),RulesClass::Instance->MessageDelay,house->ColorSchemeIndex,true);
}

namespace {
constexpr int commandId = -0x5346;
constexpr int refreshCommandId=-0x5347;
constexpr int refreshHit=10, closeHit=7, nextRewardHit=8;
unsigned pendingFreeNotices=0;
ULONGLONG nextFreeNoticeAt=0;
Research::RewardId pressedReward=0;
constexpr short commandMarker = 0x5243;
constexpr int buildingCommandBase = -0x100000;
constexpr int ticketCommandBase = -0x110000;
void playClick() {
    if(ResearchAudio::play(Research::SoundCue::Button)) return;
    auto rules=RulesClass::Instance;
    if(!rules) return;
    int sound=rules->GenericClick;
    if(sound>=0 && sound<VocClass::Array.Count) VocClass::PlayGlobal(sound,0x2000,1.0f);
}
bool hasAvailableResearch() {
    auto house=HouseClass::CurrentPlayer;
    if(!house || !house->Type || Research::balance(house->ArrayIndex)<Research::price) return false;
    const auto country=ConfigText::lower(house->Type->ID);
    for(int i=0;i<static_cast<int>(Research::options.size());++i) {
        const auto& option=Research::options[i];int rarity=static_cast<int>(option.rarity);
        if(!option.grantOnly && Research::meetsCountryOrTech(house->ArrayIndex,option,country) && rarity>=0 && rarity<Research::rarityCount && Research::rarityWeights[rarity]>0 &&
           Research::ownershipAllows(house->ArrayIndex,option) && Research::available(i) &&
           Research::meetsPrerequisites(house->ArrayIndex,i) && Research::canApply(house->ArrayIndex,i)) return true;
    }
    return false;
}

bool visible() {
    auto player = HouseClass::CurrentPlayer;
    return DSurface::Composite && ScenarioClass::Instance && player && !player->IsObserver()
        && !player->Defeated && !ScenarioClass::Instance->UserInputLocked && Research::isEnabled();
}
constexpr int tabSize=48, badgeSize=40, badgePitch=44;
constexpr int inertHit=5, nextPageHit=6;
int iconColumns() {return std::max(2,(DSurface::ViewBounds.Width-tabSize-16)/badgePitch);}
RectangleStruct completedRect(const RectangleStruct& tab,int slot) {
    return {tab.X-4-badgeSize-badgePitch*slot,tab.Y+(tab.Height-badgeSize)/2,badgeSize,badgeSize};
}
RectangleStruct infoRect(const RectangleStruct& tab) {
    return {tab.X+tab.Width-20,tab.Y+tab.Height+4,20,20};
}
struct BadgePage {
    int first,count,pages;
};
BadgePage badgePage(int total) {
    const int slots=iconColumns();
    if(total<=slots) return {0,total,1};
    const int perPage=slots-1;
    const int pages=(total+perPage-1)/perPage;
    const int first=(Research::badgePageIndex%pages)*perPage;
    return {first,std::min(perPage,total-first),pages};
}
std::vector<int> currentOffers();
struct Layout {
    RectangleStruct tab,panel;
    int cardWidth,cardHeight,gap;
    RectangleStruct card(int slot) const {
        return {panel.X+(cardWidth+gap)*slot,panel.Y+38,cardWidth,cardHeight};
    }
    RectangleStruct refresh(int slot) const {
        auto c=card(slot);
        int width=std::min(98,c.Width-8);
        return {c.X+(c.Width-width)/2,c.Y+c.Height+6,width,28};
    }
    RectangleStruct close() const {return {panel.X+panel.Width-28,panel.Y,28,26};}
    RectangleStruct nextReward() const {return {panel.X+(panel.Width-98)/2,panel.Y+panel.Height-28,98,28};}
};
struct CardAnimation {
    bool wasOpen=false,pending=false,selecting=false;
    int pendingOption=-1,pendingSlot=-1;
    Research::RewardId pendingReward=0;
    ULONGLONG openAt=0,selectAt=0;
    std::array<ULONGLONG,Research::choices> refreshAt{};
    std::vector<int> selectionOffers;
    Layout selectionLayout{};
};
CardAnimation cardAnimation;
Layout layout() {
    const auto& view=DSurface::ViewBounds;
    int count=Research::expanded?static_cast<int>(currentOffers().size()):0;
    int slots=std::max(1,count);
    int gap=view.Width>=720?16:8;
    int footer=Research::expanded && (Research::rewardPanel || Research::allowCardRefresh) && count?34:0;
    int width=std::min({244,(view.Width-24-(slots-1)*gap)/slots,(view.Height-60-footer)*2/3});
    int h=width*3/2;
    int panelWidth=width*slots+(slots-1)*gap;
    int panelHeight=38+h+footer;
    return {{view.X+view.Width-8-tabSize,view.Y+24,tabSize,tabSize},
        {view.X+(view.Width-panelWidth)/2,view.Y+(view.Height-panelHeight)/2,panelWidth,panelHeight},
        width,h,gap};
}
bool inside(const RectangleStruct& r, Point2D p) {
    return p.X >= r.X && p.X < r.X+r.Width && p.Y >= r.Y && p.Y < r.Y+r.Height;
}
const std::vector<int>& houseOffers(HouseClass* house) {
    return Research::offers(house->ArrayIndex,house->Type ? house->Type->ID : "");
}
std::vector<int> currentOffers() {
    auto house=HouseClass::CurrentPlayer;
    if(Research::rewardPanel) return Research::freeOptions(Research::rewardPanel,house->ArrayIndex);
    return houseOffers(house);
}
bool canSwitchReward(const std::vector<Research::RewardId>& rewards,int house) {
    return Research::rewardPanel && (rewards.size()>1 || Research::balance(house)>=Research::price);
}
Research::RewardId nextReward(const std::vector<Research::RewardId>& rewards,int house) {
    auto current=std::find(rewards.begin(),rewards.end(),Research::rewardPanel);
    if(current!=rewards.end() && ++current!=rewards.end()) return *current;
    return Research::balance(house)>=Research::price || rewards.empty()?0:rewards.front();
}
int hitTest(const Layout& l, Point2D p) {
    int player=HouseClass::CurrentPlayer->ArrayIndex;
    if (inside(l.tab,p)) return Research::expanded || Research::balance(player)>=Research::price ||
        !Research::pendingFreeRewards(player).empty()?0:inertHit;
    if (inside(infoRect(l.tab),p)) return inertHit;
    auto page=badgePage(static_cast<int>(Research::completedProjects(HouseClass::CurrentPlayer->ArrayIndex).size()));
    if(page.pages>1 && inside(completedRect(l.tab,iconColumns()-1),p)) return nextPageHit;
    for(int i=0;i<page.count;++i) if(inside(completedRect(l.tab,i),p)) return inertHit;
    if(Research::expanded && inside(l.panel,p)) {
        if(inside(l.close(),p)) return closeHit;
        if(Research::rewardPanel && inside(l.nextReward(),p) && canSwitchReward(Research::pendingFreeRewards(player),player)) return nextRewardHit;
        auto offered=currentOffers();
        auto house=HouseClass::CurrentPlayer;
        for(int i=0;i<static_cast<int>(offered.size());++i) {
            if(inside(l.card(i),p)) return i+1;
            if(!Research::rewardPanel && Research::allowCardRefresh && inside(l.refresh(i),p))
                return Research::canRefresh(house->ArrayIndex,house->Type->ID,i)?refreshHit+i:inertHit;
        }
        return inertHit;
    }
    return Research::expanded && inside(DSurface::ViewBounds,p)?inertHit:-1;
}
int color(int r,int g,int b) { return Drawing::RGB_To_Int(r,g,b); }
int rarityInk(Research::Rarity rarity,int fallback) {
    if(static_cast<size_t>(rarity)>=Research::rarityCount) return fallback;
    const auto& rgb=Research::Detail::rarityColors[static_cast<size_t>(rarity)];
    return rgb?color((*rgb)[0],(*rgb)[1],(*rgb)[2]):fallback;
}
struct PngImage { int width=0,height=0; std::vector<BYTE> pixels; RectangleStruct content{}; };
void measureImageContent(PngImage& image) {
    int left=image.width,top=image.height,right=0,bottom=0;
    for(int y=0;y<image.height;++y) for(int x=0;x<image.width;++x) {
        if(image.pixels[(static_cast<size_t>(y)*image.width+x)*4+3]<16) continue;
        left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);
    }
    if(right>left && bottom>top) image.content={left,top,right-left,bottom-top};
}
using ImageKey=std::tuple<std::string,int,std::optional<Research::Rarity>>;
auto& imageCache() {
    static Resources::Cache<ImageKey,PngImage> cache(256,64*1024*1024);
    return cache;
}
struct CardSkin {
    std::weak_ptr<const PngImage> source;
    int width=0,height=0;
    std::vector<WORD> pixels;
    std::vector<BYTE> alpha;
};
CardSkin& cardSkinCache() {static CardSkin skin;return skin;}
bool decodePng(Gdiplus::Bitmap* bitmap,PngImage& out) {
    if(!bitmap || bitmap->GetLastStatus()!=Gdiplus::Ok) return false;
    const UINT w=bitmap->GetWidth(),h=bitmap->GetHeight();
    if(!w || !h || w>4096 || h>4096) return false;
    Gdiplus::Rect bounds(0,0,w,h); Gdiplus::BitmapData data{};
    if(bitmap->LockBits(&bounds,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)!=Gdiplus::Ok) return false;
    out.width=w;out.height=h;out.pixels.resize(static_cast<size_t>(w)*h*4);
    for(UINT y=0;y<h;++y) memcpy(out.pixels.data()+static_cast<size_t>(y)*w*4,static_cast<BYTE*>(data.Scan0)+static_cast<ptrdiff_t>(y)*data.Stride,w*4);
    bitmap->UnlockBits(&data);return true;
}
bool loadPng(const std::string& name,int resource,PngImage& out) {
    static ULONG_PTR gdiplusToken=[] { ULONG_PTR token=0; Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&token,&input,nullptr); return token; }();
    if(!gdiplusToken) return false;
    const auto path=ResearchAudio::widePath(name);
    if(!path.empty()) {
        Gdiplus::Bitmap image(path.c_str(),FALSE);
        if(decodePng(&image,out)) return true;
    }
    if(!resource) return false;
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&loadPng),&module)) return false;
    HRSRC found=FindResourceW(module,MAKEINTRESOURCEW(resource),MAKEINTRESOURCEW(10));
    if(!found) return false;
    DWORD bytes=SizeofResource(module,found); HGLOBAL loaded=LoadResource(module,found);
    const void* source=loaded?LockResource(loaded):nullptr;
    if(!source || !bytes) return false;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes); if(!memory) return false;
    void* destination=GlobalLock(memory); if(!destination) { GlobalFree(memory); return false; }
    memcpy(destination,source,bytes);GlobalUnlock(memory);
    IStream* stream=nullptr;
    if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&stream))) { GlobalFree(memory); return false; }
    bool ok=false;
    { Gdiplus::Bitmap image(stream,FALSE); ok=decodePng(&image,out); }
    stream->Release();return ok;
}
bool drawPng(const PngImage& image,RectangleStruct rect,int crop,int opacity=255,const RectangleStruct* clip=nullptr,Surface* surface=DSurface::Composite) {
    if(!surface || surface->GetBytesPerPixel()!=2 || rect.Width<=0 || rect.Height<=0) return false;
    int x=std::max(0,rect.X),y=std::max(0,rect.Y);
    int right=std::min(surface->GetWidth(),rect.X+rect.Width),bottom=std::min(surface->GetHeight(),rect.Y+rect.Height);
    if(clip) {
        x=std::max(x,clip->X);y=std::max(y,clip->Y);
        right=std::min(right,clip->X+clip->Width);bottom=std::min(bottom,clip->Y+clip->Height);
    }
    if(x>=right || y>=bottom) return true;
    auto base=static_cast<BYTE*>(surface->Lock(x,y));if(!base) return false;
    int pitch=surface->GetPitch();
    crop=std::min({crop,(image.width-1)/2,(image.height-1)/2});
    const int shifts[]={Drawing::RedShiftLeft,Drawing::GreenShiftLeft,Drawing::BlueShiftLeft};
    const int rightShifts[]={Drawing::RedShiftRight,Drawing::GreenShiftRight,Drawing::BlueShiftRight};
    for(int dy=y;dy<bottom;++dy) {
        auto row=reinterpret_cast<WORD*>(base+(dy-y)*pitch);
        int sy=crop+static_cast<int>(static_cast<long long>(dy-rect.Y)*(image.height-2*crop)/rect.Height);
        for(int dx=x;dx<right;++dx) {
            int sx=crop+static_cast<int>(static_cast<long long>(dx-rect.X)*(image.width-2*crop)/rect.Width);
            const BYTE* p=&image.pixels[(static_cast<size_t>(sy)*image.width+sx)*4];
            int alpha=(p[3]*std::clamp(opacity,0,255)+127)/255;
            if(!alpha) continue;
            int pixel=0;
            for(int k=0;k<3;++k) {
                int mask=255>>rightShifts[k],fg=p[2-k]>>rightShifts[k],bg=(row[dx-x]>>shifts[k])&mask;
                pixel|=((fg*alpha+bg*(255-alpha)+127)/255)<<shifts[k];
            }
            row[dx-x]=static_cast<WORD>(pixel);
        }
    }
    surface->Unlock();return true;
}
// Map luminance to the rarity palette while keeping alpha unchanged.
void colorRarityIcon(PngImage& image,Research::Rarity rarity) {
    auto makePalette=[](const std::array<std::array<int,3>,4>& stops) {
        std::array<std::array<BYTE,3>,256> colors{};
        for(int i=0;i<256;++i) {
            int band=std::min(i/85,2),fraction=i-85*band;
            for(int c=0;c<3;++c)
                colors[i][2-c]=static_cast<BYTE>((stops[band][c]*(85-fraction)+stops[band+1][c]*fraction+42)/85);
        }
        return colors;
    };
    static const auto rare=makePalette({{{11,12,34},{53,62,161},{134,111,232},{233,226,255}}});
    static const auto epic=makePalette({{{38,24,5},{139,84,15},{225,166,45},{255,240,177}}});
    static const auto insane=makePalette({{{42,5,9},{147,18,33},{232,54,63},{255,195,185}}});
    const auto& custom=Research::Detail::rarityColors[static_cast<size_t>(rarity)];
    if(!custom && rarity==Research::Rarity::Common) return;
    const auto* palette=&(rarity==Research::Rarity::Insane?insane:rarity==Research::Rarity::Epic?epic:rare);
    std::array<std::array<BYTE,3>,256> tinted;
    if(custom) {
        std::array<std::array<int,3>,4> stops{};
        for(size_t c=0;c<3;++c) {
            int channel=(*custom)[c];
            stops[0][c]=channel/12;
            stops[1][c]=channel/2;
            stops[2][c]=channel;
            stops[3][c]=channel+(255-channel)*2/3;
        }
        tinted=makePalette(stops);
        palette=&tinted;
    }
    for(size_t i=0;i<image.pixels.size();i+=4) {
        BYTE* p=image.pixels.data()+i;
        int light=(54*p[2]+183*p[1]+19*p[0]+128)>>8;
        std::copy_n((*palette)[light].data(),3,p);
    }
}
bool readPcxPixels(BSurface& src,PngImage& out) {
    int w=src.GetWidth(),h=src.GetHeight();
    if(w<=0 || h<=0 || w>4096 || h>4096 || src.GetBytesPerPixel()!=2) return false;
    const WORD key=static_cast<WORD>(color(255,0,255));
    const int shifts[]={Drawing::BlueShiftLeft,Drawing::GreenShiftLeft,Drawing::RedShiftLeft};
    const int masks[]={255>>Drawing::BlueShiftRight,255>>Drawing::GreenShiftRight,255>>Drawing::RedShiftRight};
    int pitch=src.GetPitch();
    out.pixels.resize(static_cast<size_t>(w)*h*4);
    auto base=static_cast<const BYTE*>(src.Lock(0,0));
    if(!base) {out.pixels.clear();return false;}
    for(int y=0;y<h;++y) {
        auto row=reinterpret_cast<const WORD*>(base+y*pitch);
        for(int x=0;x<w;++x) {
            BYTE* p=&out.pixels[4*(y*w+x)];
            for(int c=0;c<3;++c) p[c]=static_cast<BYTE>(((row[x]>>shifts[c])&masks[c])*255/masks[c]);
            p[3]=row[x]==key?0:255;
        }
    }
    src.Unlock();
    out.width=w;out.height=h;
    return true;
}
std::shared_ptr<const PngImage> imageFor(const std::string& path,int resource,std::optional<Research::Rarity> rarity,bool png) {
    // Untinted UI art can share an original image with an uncolored Common project.
    if(!rarity && !Research::Detail::rarityColors[static_cast<size_t>(Research::Rarity::Common)]) rarity=Research::Rarity::Common;
    return imageCache().get(ImageKey{Resources::pathKey(path),resource,rarity},[&] {
        PngImage image;
        if(png) loadPng(path,resource,image);
        else {
            auto& pcx=PCX::Instance;
            auto surface=pcx.GetSurface(path.c_str());
            if(!surface && pcx.LoadFile(path.c_str())) surface=pcx.GetSurface(path.c_str());
            if(surface) readPcxPixels(*surface,image);
        }
        if(rarity && *rarity!=Research::Rarity::Invalid) colorRarityIcon(image,*rarity);
        if(!image.pixels.empty()) measureImageContent(image);
        return image;
    },[](const ImageKey& key,const PngImage& image) {
        return image.pixels.capacity()+2*(std::get<0>(key).size()+1)+sizeof(PngImage);
    });
}
bool blit(const char* name, RectangleStruct rect,int crop=0,std::optional<Research::Rarity> rarity={},int opacity=255,const RectangleStruct* clip=nullptr,Surface* surface=DSurface::Composite) {
    // Zero IDs preserve legacy .pcx aliases for externally supplied PNGs.
    struct Builtin { const char* stem;int id; };
    static const Builtin builtins[]={{"sfrlab",101},{"sfrpanel",0},{"sfrfocus",0},{"sfrdefault",104},{"sfrheal",0},{"sfrdrain",0},{"sfrripst",0},{"sfframe",109},{"sfactive_default",110}};
    std::string path=name;int resource=0;
    for(const auto& builtin:builtins) {
        if(!_stricmp(name,(std::string(builtin.stem)+".pcx").c_str()) || !_stricmp(name,(std::string(builtin.stem)+".png").c_str())) {
            std::string pngName=std::string(builtin.stem)+".png";
            if(!builtin.id) {
                if(_stricmp(name,pngName.c_str()) && blit(pngName.c_str(),rect,crop,rarity,opacity,clip,surface)) return true;
                break;
            }
            path=pngName;resource=builtin.id;break;
        }
    }
    bool png=resource || (path.size()>=4 && !_stricmp(path.c_str()+path.size()-4,".png"));
    auto image=imageFor(path,resource,rarity,png);
    return !image->pixels.empty() && drawPng(*image,rect,crop,opacity,clip,surface);
}
void drawProjectIcon(const Research::Option& project,RectangleStruct rect,Surface* surface=DSurface::Composite,int opacity=255) {
    if(project.icon.empty() || !blit(project.icon.c_str(),rect,0,project.rarity,opacity,nullptr,surface))
        blit("sfrdefault.pcx",rect,0,project.rarity,opacity,nullptr,surface);
}
std::wstring targetLabel(const Research::Option& option,bool compact) {
    if(!option.targetLabel.empty()) return option.targetLabel;
    if(option.effectList) {
        std::wstring label;
        for(const auto& effect:option.effects) {
            Research::Option part;part.effect=effect.effect;part.cost=effect.cost;part.types=effect.types;part.technology=effect.technology;part.from=effect.from;part.to=effect.to;
            auto value=targetLabel(part,true);
            if(value.empty() || label.find(value)!=std::wstring::npos) continue;
            if(!label.empty()) label+=L"、";
            label+=value;
            if(compact && label.size()>10) {label+=L"等";break;}
        }
        return label;
    }
    if(option.effect==Research::Effect::CategoryCost) return std::wstring(L"本方")+Research::categoryName(option.cost.category);
    if(option.effect==Research::Effect::Credits) return L"本方资金";
    if(option.effect==Research::Effect::RandomResearch || option.effect==Research::Effect::FreeResearch) return L"本方科研";
    if(option.effect==Research::Effect::Technology) {
        auto type=TechnoTypeClass::Find(option.technology.c_str());
        if(type && type->UIName && *type->UIName) return type->UIName;
        return std::wstring(option.technology.begin(),option.technology.end());
    }
    if(option.effect==Research::Effect::ConvertInfantry || option.effect==Research::Effect::ConvertVehicle) {
        auto source=TechnoTypeClass::Find(option.from.c_str()),target=TechnoTypeClass::Find(option.to.c_str());
        if(source && target && source->UIName && target->UIName)
            return compact?std::wstring(source->UIName):std::wstring(source->UIName)+L" → "+target->UIName;
        return option.effect==Research::Effect::ConvertVehicle?L"本方载具":L"本方步兵";
    }
    std::wstring label;
    if(std::find(option.types.begin(),option.types.end(),"*")!=option.types.end()) return L"全部单位";
    for(const auto& id:option.types) {
        const wchar_t* category=id=="@infantry"?L"全部步兵":id=="@vehicle"?L"全部载具":
            id=="@aircraft"?L"全部飞行器":id=="@building"?L"全部建筑":nullptr;
        auto type=category?nullptr:TechnoTypeClass::Find(id.c_str());
        if(!label.empty()) label+=L"、";
        if(category) label+=category;
        else if(type && type->UIName && *type->UIName) label+=type->UIName;
        else label.append(id.begin(),id.end());
        if(compact && option.types.size()>1) {label+=L"等";break;}
    }
    return label;
}
std::wstring cardSubtitle(const Research::Option& project) {
    return std::wstring(Research::rarityName(project.rarity))+L"  ·  "+targetLabel(project,true);
}
std::vector<std::wstring> tooltipLines(const std::wstring& text,int width,int maxHeight) {
    std::wistringstream detail(text);
    std::vector<std::wstring> detailLines;
    for(std::wstring line;std::getline(detail,line);) {
        if(line.empty()) {detailLines.emplace_back();continue;}
        for(size_t pos=0;pos<line.size();) {
            size_t low=1,high=line.size()-pos,best=1;
            while(low<=high) {
                size_t count=low+(high-low)/2;
                if(ResearchText::maskFor(line.substr(pos,count).c_str(),15).w<=width-28) {best=count;low=count+1;}
                else high=count-1;
            }
            detailLines.push_back(line.substr(pos,best));pos+=best;
        }
    }
    const int baseHeight=60;
    const int maxLines=std::max(1,1+(maxHeight-baseHeight-8)/20);
    if(detailLines.size()>static_cast<size_t>(maxLines)) {
        detailLines.resize(maxLines);
        if(!detailLines.back().empty()) detailLines.back().pop_back();
        detailLines.back()+=L"…";
    }
    return detailLines;
}
struct BondIcon {int project;bool owned;};
struct TipRow {
    std::wstring text;
    bool bright=true;
    std::vector<BondIcon> icons;
    int iconSize=0;
    int height() const {return iconSize?iconSize+4:20;}
};
struct TipPanel {
    std::wstring title;
    std::vector<TipRow> rows;
    bool bond=false;
    int height() const {
        int body=0;
        for(const auto& row:rows) body+=row.height();
        return 40+std::max(20,body);
    }
};
using TipPage=std::vector<TipPanel>;
int pageHeight(const TipPage& page) {
    int height=0;
    for(const auto& panel:page) height+=panel.height()+4;
    return height?height-4:0;
}
std::vector<TipPage> makeTooltipPages(int option,int house,const std::wstring& text,int width,int maxHeight) {
    const auto& project=Research::options[option];
    auto groups=Research::memberBonds.find(project.id);
    const bool hasBonds=groups!=Research::memberBonds.end();
    TipPanel main{std::wstring(L"[")+Research::rarityName(project.rarity)+L"] "+project.name};
    for(auto& line:tooltipLines(text,width,hasBonds?std::max(60,maxHeight/2):maxHeight))
        main.rows.push_back({std::move(line)});
    std::vector<TipPage> pages{{main}};
    if(!hasBonds) return pages;
    const bool repeatMain=maxHeight>=main.height()+4+76;
    auto nextPage=[&](){pages.emplace_back();if(repeatMain) pages.back().push_back(main);};
    const int freshSpace=maxHeight-(repeatMain?main.height()+4:0);
    auto space=[&]() {return maxHeight-pageHeight(pages.back())-(pages.back().empty()?0:4);};
    for(int index:groups->second) {
        const auto& bond=Research::bonds[index];
        const int count=Research::bondCount(house,bond);
        auto title=L"羁绊 · "+bond.name+L"  "+std::to_wstring(count)+L"/"+std::to_wstring(bond.members.size());
        std::vector<BondIcon> members;
        for(const auto& id:bond.members) {
            auto it=std::find_if(Research::options.begin(),Research::options.end(),[&](const auto& o){return o.id==id;});
            if(it==Research::options.end()) continue;
            members.push_back({static_cast<int>(it-Research::options.begin()),Research::owns(house,id)});
        }
        std::vector<TipRow> rewards;
        for(const auto& tier:bond.tiers) {
            std::wstring body=L"（"+std::to_wstring(tier.required)+L"）";
            if(!tier.summary.empty()) body+=tier.summary;
            else for(size_t i=0;i<tier.effects.size();++i) {
                if(i) body+=L"，";
                body+=Research::bondRewardText(tier.effects[i]);
            }
            for(auto& line:tooltipLines(body,width,INT_MAX))
                rewards.push_back({std::move(line),count>=tier.required});
        }
        // Preserve member order and a single horizontal strip, even on small diplomacy pages.
        constexpr int gap=4;
        const int innerWidth=std::max(1,width-28);
        const int preferred=(innerWidth+gap)/std::max(1,static_cast<int>(members.size()))-gap;
        const int size=std::max(1,std::min({std::clamp(preferred,24,32),freshSpace-44,innerWidth}));
        const int capacity=std::max(1,(innerWidth+gap)/(size+gap));
        for(size_t first=0;first<members.size();first+=capacity) {
            if(first) nextPage();
            TipRow strip;
            strip.iconSize=size;
            auto end=std::min(members.size(),first+capacity);
            strip.icons.assign(members.begin()+first,members.begin()+end);
            TipPanel panel{title,{},true};
            panel.rows.push_back(std::move(strip));
            int fullHeight=panel.height()+static_cast<int>(rewards.size())*20;
            if(panel.height()>space() || (fullHeight>space() && fullHeight<=freshSpace)) nextPage();
            for(const auto& reward:rewards) {
                if(panel.height()+reward.height()>space()) {
                    pages.back().push_back(std::move(panel));
                    nextPage();
                    panel={title,{},true};
                }
                panel.rows.push_back(reward);
            }
            pages.back().push_back(std::move(panel));
        }
    }
    if(pages.size()>1) for(size_t i=0;i<pages.size();++i)
        pages[i].front().title=L"["+std::to_wstring(i+1)+L"/"+std::to_wstring(pages.size())+L"] "+pages[i].front().title;
    return pages;
}
const std::vector<TipPage>& tooltipPages(int project,int house,const std::wstring& text,int width,int maxHeight) {
    static std::tuple<int,int,std::wstring,int,int,unsigned,unsigned> key;
    static std::vector<TipPage> pages;
    auto next=std::make_tuple(project,house,text,width,maxHeight,Research::uiGeneration,Research::bondRevision);
    if(next!=key || pages.empty()) {
        pages=makeTooltipPages(project,house,text,width,maxHeight);key=std::move(next);
    }
    return pages;
}
void paintTooltip(Surface* surface,int option,RectangleStruct rect,const TipPanel& panel) {
    const auto& project=Research::options[option];
    surface->FillRect(&rect,color(15,19,35));
    surface->DrawRect(&rect,color(117,125,178));
    const auto& title=panel.title;
    int titleColor=panel.bond?color(99,219,206):rarityInk(project.rarity,project.rarity==Research::Rarity::Insane?color(255,111,117):project.rarity==Research::Rarity::Epic?color(255,213,112):color(211,201,255));
    ResearchText::draw(title.c_str(),rect.X+14,rect.Y+8,titleColor,17,rect.Width-28,surface);
    int y=rect.Y+(panel.bond?33:39);
    for(const auto& row:panel.rows) {
        int x=rect.X+14;
        if(row.iconSize) {
            for(const auto& icon:row.icons) {
                RectangleStruct tile{x,y,row.iconSize,row.iconSize};
                surface->FillRect(&tile,color(12,18,29));
                RectangleStruct glyph{x+2,y+2,row.iconSize-4,row.iconSize-4};
                drawProjectIcon(Research::options[icon.project],glyph,surface,icon.owned?255:88);
                surface->DrawRect(&tile,icon.owned?color(105,176,178):color(51,61,76));
                x+=row.iconSize+4;
            }
        } else {
            int ink=row.bright?color(233,237,250):color(132,138,151);
            ResearchText::draw(row.text.c_str(),x,y,ink,15,rect.Width-28,surface);
        }
        y+=row.height();
    }
}
void paintPage(Surface* surface,int option,int x,int y,int width,const TipPage& page) {
    for(const auto& panel:page) {
        int height=panel.height();
        paintTooltip(surface,option,{x,y,width,height},panel);
        y+=height+4;
    }
}
void drawTooltip(const Layout& l,int slot,int option, const RectangleStruct* anchor=nullptr) {
    const auto& view=DSurface::ViewBounds;
    constexpr int width=328;
    const int house=HouseClass::CurrentPlayer->ArrayIndex;
    const auto& pages=tooltipPages(option,house,Research::describe(option,house),width,view.Height);
    static int previous=-1;static ULONGLONG since=0,last=0;
    auto now=GetTickCount64();
    if(option!=previous || now-last>500) {since=now;previous=option;}
    last=now;
    const auto& page=pages[((now-since)/5000)%pages.size()];
    const int height=pageHeight(page);
    auto card=l.card(slot);
    int x=anchor?anchor->X-width/2:card.X+(card.Width-width)/2;
    int y=anchor?anchor->Y+anchor->Height+4:l.panel.Y+l.panel.Height+4;
    if(!anchor && y+height>view.Y+view.Height) {
        x=card.X+card.Width+4;
        if(x+width>view.X+view.Width) x=card.X-width-4;
        y=card.Y+12;
    }
    x=std::clamp(x,view.X,std::max(view.X,view.X+view.Width-width));
    y=std::clamp(y,view.Y,std::max(view.Y,view.Y+view.Height-height));
    paintPage(DSurface::Composite,option,x,y,width,page);
}
void drawResearchButton(RectangleStruct tab,int points,double flash) {
    auto surface=DSurface::Composite;
    RectangleStruct face{tab.X+4,tab.Y+4,tab.Width-8,tab.Height-8};
    if(!blit("sfrlab.pcx",face,5)) {
        surface->FillRect(&face,color(17,29,48));
        ResearchText::draw(L"研",face.X+10,face.Y+8,color(212,236,255),20);
    }
    if(surface->GetBytesPerPixel()!=2 || tab.X<0 || tab.Y<0 ||
       tab.X+tab.Width>surface->GetWidth() || tab.Y+tab.Height>surface->GetHeight()) return;
    const bool full=points>=Research::price;
    int litRows=std::clamp(points,0,Research::price)*tab.Height/Research::price;
    if(points>0) litRows=std::max(1,litRows);
    int phase=full?static_cast<int>(GetTickCount()%800):0;
    int pulse=std::min(phase,800-phase);
    int rim=color(19,43,57),track=color(42,64,79),inner=color(19,32,48);
    int glow=color(12,126+50*pulse/400,165+45*pulse/400);
    int light=color(60+120*pulse/400,218+35*pulse/400,251);
    if(flash>0) {
        glow=color(12+static_cast<int>(226*flash),148+static_cast<int>(100*flash),200+static_cast<int>(55*flash));
        light=color(120+static_cast<int>(135*flash),234+static_cast<int>(21*flash),255);
    }
    const int pitch=surface->GetPitch();
    auto base=static_cast<BYTE*>(surface->Lock(tab.X,tab.Y));
    if(!base) return;
    // Clip the chamfered frame by screen height, so both sides rise together.
    for(int y=0;y<tab.Height;++y) {
        int dy=std::min(y,tab.Height-1-y);
        int cut=std::max(0,6-dy);
        bool lit=y>=tab.Height-litRows;
        auto row=reinterpret_cast<WORD*>(base+y*pitch);
        for(int x=cut;x<tab.Width-cut;++x) {
            int dx=std::min(x,tab.Width-1-x);
            int depth=std::min({dy,dx,(dx+dy-6)*2/3});
            if(depth>=4) continue;
            row[x]=static_cast<WORD>(depth==3?inner:depth==0?(lit?glow:rim):(lit?light:track));
        }
    }
    surface->Unlock();
}
void drawPointsTip(const RectangleStruct& tab,int points,int freeCount) {
    const auto& view=DSurface::ViewBounds;
    int width=std::min(212,view.Width);
    RectangleStruct tip{std::max(view.X,tab.X+tab.Width-width),tab.Y+tab.Height+5,width,50};
    DSurface::Composite->FillRect(&tip,color(14,23,36));
    DSurface::Composite->DrawRect(&tip,color(75,111,131));
    wchar_t line[64]{};
    swprintf_s(line,L"科研點數  %d / %d",points,Research::price);
    ResearchText::draw(line,tip.X+10,tip.Y+4,color(218,240,251),16,width-20);
    if(freeCount) swprintf_s(line,L"可领取 %d 次免费科研",freeCount);
    else if(points<Research::price) swprintf_s(line,L"還差 %d 點可研究",Research::price-points);
    else wcscpy_s(line,L"可選擇一項升級");
    ResearchText::draw(line,tip.X+10,tip.Y+26,color(159,194,210),14,width-20);
}
void drawInfoTip(const RectangleStruct& icon) {
    const auto& view=DSurface::ViewBounds;
    int width=std::min(180,view.Width);
    RectangleStruct tip{std::max(view.X,icon.X+icon.Width-width),icon.Y+icon.Height+4,width,52};
    DSurface::Composite->FillRect(&tip,color(14,23,36));
    DSurface::Composite->DrawRect(&tip,color(75,111,131));
    ResearchText::draw(L"流火实验室 0.40",tip.X+10,tip.Y+5,color(218,240,251),16,width-20);
    ResearchText::draw(L"By 斩断云天",tip.X+10,tip.Y+28,color(159,194,210),14,width-20);
}
void centered(const std::wstring& text,RectangleStruct bounds,int y,int ink,int size) {
    int available=std::max(1,bounds.Width-8);
    int width=std::min(ResearchText::maskFor(text.c_str(),size).w,available);
    ResearchText::draw(text.c_str(),bounds.X+(bounds.Width-width)/2,y,ink,size,available);
}
// Cache the scaled foreground and alpha, never pixels blended with the map.
void drawCardSkin(RectangleStruct rect) {
    auto art=imageFor("sfrcard.png",108,std::nullopt,true);
    auto& skin=cardSkinCache();
    auto& [source,width,height,pixels,alpha]=skin;
    auto surface=DSurface::Composite;
    constexpr size_t maxSkinPixels=1024*1024;
    if(rect.Width<=0 || rect.Height<=0) return;
    if(art->pixels.empty() || surface->GetBytesPerPixel()!=2 ||
       static_cast<size_t>(rect.Width)*rect.Height>maxSkinPixels) {
        surface->FillRect(&rect,color(16,28,46));
        surface->DrawRect(&rect,color(155,161,204));
        return;
    }
    if(rect.Width!=width || rect.Height!=height || source.lock()!=art) {
        source=art;
        width=rect.Width;height=rect.Height;
        std::vector<WORD>(static_cast<size_t>(width)*height).swap(pixels);
        std::vector<BYTE>(pixels.size()).swap(alpha);
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
            auto p=&art->pixels[4*((y*art->height/height)*art->width+x*art->width/width)];
            pixels[y*width+x]=static_cast<WORD>(color(p[2],p[1],p[0]));
            alpha[y*width+x]=p[3];
        }
    }
    int left=std::max(0,rect.X),top=std::max(0,rect.Y);
    int right=std::min(surface->GetWidth(),rect.X+width),bottom=std::min(surface->GetHeight(),rect.Y+height);
    if(left>=right || top>=bottom) return;
    int pitch=surface->GetPitch();
    auto dest=static_cast<BYTE*>(surface->Lock(left,top));
    if(!dest) return;
    const int shifts[]={Drawing::RedShiftLeft,Drawing::GreenShiftLeft,Drawing::BlueShiftLeft};
    const int masks[]={255>>Drawing::RedShiftRight,255>>Drawing::GreenShiftRight,255>>Drawing::BlueShiftRight};
    for(int y=top;y<bottom;++y) {
        auto row=reinterpret_cast<WORD*>(dest+(y-top)*pitch);
        for(int x=left;x<right;++x) {
            size_t index=static_cast<size_t>(y-rect.Y)*width+x-rect.X;int a=alpha[index];
            if(!a) continue;
            if(a==255) {row[x-left]=pixels[index];continue;}
            int result=0;
            for(int k=0;k<3;++k) {
                int fg=(pixels[index]>>shifts[k])&masks[k],bg=(row[x-left]>>shifts[k])&masks[k];
                result|=((fg*a+bg*(255-a)+127)/255)<<shifts[k];
            }
            row[x-left]=static_cast<WORD>(result);
        }
    }
    surface->Unlock();
}
void cardLines(const std::wstring& text,RectangleStruct bounds,int y,int size,int ink,int maxLines) {
    int maxWidth=bounds.Width-20;
    int measured=ResearchText::maskFor(text.c_str(),size).w;
    int lines=std::clamp((measured+maxWidth-1)/maxWidth,1,maxLines);
    int lineWidth=std::min(maxWidth,(measured+lines-1)/lines+size/2);
    size_t pos=0;
    for(int line=0;pos<text.size() && line<maxLines;++line) {
        std::wstring part;
        while(pos<text.size()) {
            auto next=part+text[pos];
            if(!part.empty() && ResearchText::maskFor(next.c_str(),size).w>lineWidth) break;
            part=std::move(next);++pos;
        }
        // Keep closing punctuation with the preceding character on a wrapped line.
        if(pos<text.size() && part.size()>1 && std::wstring_view(L"，。！？、；：）」").find(text[pos])!=std::wstring_view::npos) {
            --pos;part.pop_back();
        }
        if(line+1==maxLines && pos<text.size() && !part.empty()) part.back()=L'…';
        centered(part,bounds,y+line*(size+4),ink,size);
    }
}
void drawCard(const Layout& l,int slot,int index,Point2D pointer,bool showRefresh=true,bool forceEnabled=false) {
    auto c=l.card(slot);
    const auto& o=Research::options[index];
    auto house=HouseClass::CurrentPlayer;
    bool enabled=forceEnabled || Research::canApply(house->ArrayIndex,index);
    bool hovering=inside(c,pointer);
    drawCardSkin(c);
    int accent=rarityInk(o.rarity,o.rarity==Research::Rarity::Insane?color(255,99,106):o.rarity==Research::Rarity::Epic?color(255,205,91):
        o.rarity==Research::Rarity::Rare?color(109,211,250):color(169,205,213));
    if(hovering) {
        auto outline=c;
        DSurface::Composite->DrawRect(&outline,enabled?accent:color(100,112,123));
    }
    int iconSize=std::min(96,c.Height/3);
    RectangleStruct icon{c.X+(c.Width-iconSize)/2,c.Y+(c.Height/2-iconSize)/2,iconSize,iconSize};
    drawProjectIcon(o,icon);
    int top=c.Y+c.Height/2+6;
    int titleSize=c.Width>=190?21:16;
    centered(o.name,c,top,color(242,242,251),titleSize);
    std::wstring label=cardSubtitle(o);
    centered(label,c,top+titleSize+6,accent,c.Width>=190?14:12);
    int summaryY=top+titleSize+29;
    int size=c.Width>=190?16:13;
    int bottom=c.Y+c.Height-(enabled?10:29);
    cardLines(o.summary,c,summaryY,size,color(203,214,230),std::clamp((bottom-summaryY)/(size+4),1,3));
    if(!enabled) centered(o.effect==Research::Effect::Technology?L"已解鎖":L"暫無目標",c,c.Y+c.Height-26,color(213,166,143),14);
    if(showRefresh && !Research::rewardPanel && Research::allowCardRefresh) {
        auto button=l.refresh(slot);
        bool used=Research::slotUsed(house->ArrayIndex,slot);
        bool active=Research::canRefresh(house->ArrayIndex,house->Type->ID,slot);
        auto surface=DSurface::Composite;
        surface->FillRect(&button,active && inside(button,pointer)?color(38,61,84):color(15,25,40));
        surface->DrawRect(&button,active?accent:color(64,75,89));
        centered(used?L"已刷新":active?L"↻  刷新":L"無可替換",button,button.Y+4,active?color(226,240,254):color(144,153,167),15);
    }
}
// Render on black and white to recover premultiplied color and coverage for
// the entire card (skin, icon and text). Scaling a snapshot already blended
// with terrain would also move that terrain through transparent PNG regions.
void drawAnimatedCard(const Layout& l,int slot,int index,Point2D pointer,double scale,int shiftY,int opacity,bool selected) {
    auto surface=DSurface::Composite;auto c=l.card(slot);
    if(surface->GetBytesPerPixel()!=2 || c.X<0 || c.Y<0 || c.X+c.Width>surface->GetWidth() || c.Y+c.Height>surface->GetHeight()) {
        drawCard(l,slot,index,pointer,false,selected);return;
    }
    static std::vector<WORD> before,onBlack,onWhite;
    before.resize(static_cast<size_t>(c.Width)*c.Height);onBlack.resize(before.size());onWhite.resize(before.size());
    int pitch=surface->GetPitch();
    auto capture=[&](std::vector<WORD>& pixels,bool restore) {
        auto base=static_cast<BYTE*>(surface->Lock(c.X,c.Y));if(!base) return false;
        for(int y=0;y<c.Height;++y) {
            auto src=base+y*pitch;auto dst=pixels.data()+static_cast<size_t>(y)*c.Width;
            if(restore) memcpy(src,dst,c.Width*sizeof(WORD));else memcpy(dst,src,c.Width*sizeof(WORD));
        }
        surface->Unlock();return true;
    };
    if(!capture(before,false)) return;
    surface->FillRect(&c,color(0,0,0));
    drawCard(l,slot,index,pointer,false,selected);
    if(!capture(onBlack,false)) {capture(before,true);return;}
    surface->FillRect(&c,color(255,255,255));
    drawCard(l,slot,index,pointer,false,selected);
    if(!capture(onWhite,false)) {capture(before,true);return;}
    if(!capture(before,true)) return;
    int w=std::max(1,static_cast<int>(std::lround(c.Width*scale)));
    int h=std::max(1,static_cast<int>(std::lround(c.Height*scale)));
    int dx=c.X+(c.Width-w)/2,dy=c.Y+(c.Height-h)/2+shiftY;
    const auto& view=DSurface::ViewBounds;
    int left=std::max({dx,0,view.X}),top=std::max({dy,0,view.Y});
    int right=std::min({dx+w,surface->GetWidth(),view.X+view.Width});
    int bottom=std::min({dy+h,surface->GetHeight(),view.Y+view.Height});
    if(left>=right || top>=bottom || opacity<=0) return;
    auto base=static_cast<BYTE*>(surface->Lock(left,top));if(!base) return;
    const int shifts[]={Drawing::RedShiftLeft,Drawing::GreenShiftLeft,Drawing::BlueShiftLeft};
    const int masks[]={255>>Drawing::RedShiftRight,255>>Drawing::GreenShiftRight,255>>Drawing::BlueShiftRight};
    for(int y=top;y<bottom;++y) {
        auto row=reinterpret_cast<WORD*>(base+(y-top)*pitch);
        int sy=(y-dy)*c.Height/h;
        for(int x=left;x<right;++x) {
            size_t index=static_cast<size_t>(sy)*c.Width+(x-dx)*c.Width/w;
            WORD dark=onBlack[index],light=onWhite[index],bg=row[x-left];
            int result=0;
            for(int k=0;k<3;++k) {
                int a=(dark>>shifts[k])&masks[k],b=(bg>>shifts[k])&masks[k];
                int coverage=std::clamp(masks[k]-((light>>shifts[k])&masks[k])+a,0,masks[k]);
                int alpha=(coverage*opacity+masks[k]/2)/masks[k];
                result|=std::clamp((a*opacity+b*(255-alpha)+127)/255,0,masks[k])<<shifts[k];
            }
            row[x-left]=static_cast<WORD>(result);
        }
    }
    surface->Unlock();
}
void glowSweep(RectangleStruct rect,double progress,int strength) {
    auto surface=DSurface::Composite;const auto& view=DSurface::ViewBounds;
    if(surface->GetBytesPerPixel()!=2) return;
    int left=std::max({0,rect.X,view.X}),top=std::max({0,rect.Y,view.Y});
    int right=std::min({surface->GetWidth(),rect.X+rect.Width,view.X+view.Width});
    int bottom=std::min({surface->GetHeight(),rect.Y+rect.Height,view.Y+view.Height});
    if(left>=right || top>=bottom) return;
    auto base=static_cast<BYTE*>(surface->Lock(left,top));if(!base) return;
    int pitch=surface->GetPitch();
    const int shifts[]={Drawing::RedShiftLeft,Drawing::GreenShiftLeft,Drawing::BlueShiftLeft};
    const int masks[]={255>>Drawing::RedShiftRight,255>>Drawing::GreenShiftRight,255>>Drawing::BlueShiftRight};
    const int tint[]={180>>Drawing::RedShiftRight,225>>Drawing::GreenShiftRight,255>>Drawing::BlueShiftRight};
    double center=-0.6+progress*2.2;
    for(int y=top;y<bottom;++y) {
        auto row=reinterpret_cast<WORD*>(base+(y-top)*pitch);
        for(int x=left;x<right;++x) {
            double diagonal=double(x-rect.X)/rect.Width+0.35*double(y-rect.Y)/rect.Height;
            int alpha=static_cast<int>(strength*std::max(0.0,1.0-std::abs(diagonal-center)/0.17));
            if(alpha<=0) continue;
            WORD old=row[x-left];int out=0;
            for(int k=0;k<3;++k) {int bg=(old>>shifts[k])&masks[k];out|=((tint[k]*alpha+bg*(255-alpha)+127)/255)<<shifts[k];}
            row[x-left]=static_cast<WORD>(out);
        }
    }
    surface->Unlock();
}
// Paint only the moving trails. The simulation and its random generator are never touched.
constexpr int focusImpactMs=530, focusDurationMs=720;
void drawResearchFocus(const RectangleStruct& tab,int elapsed) {
    auto surface=DSurface::Composite;
    const auto& view=DSurface::ViewBounds;
    const int left=std::max(0,view.X),top=std::max(0,view.Y);
    const int right=std::min(surface->GetWidth(),view.X+view.Width);
    const int bottom=std::min(surface->GetHeight(),view.Y+view.Height);
    if(left>=right || top>=bottom || surface->GetBytesPerPixel()!=2) return;
    const int pitch=surface->GetPitch();
    auto base=static_cast<BYTE*>(surface->Lock(left,top));
    if(!base) return;
    const double cx=tab.X+tab.Width*.5,cy=tab.Y+tab.Height*.5;
    const int rs=Drawing::RedShiftLeft,gs=Drawing::GreenShiftLeft,bs=Drawing::BlueShiftLeft;
    const int rm=255>>Drawing::RedShiftRight,gm=255>>Drawing::GreenShiftRight,bm=255>>Drawing::BlueShiftRight;
    // Lighten each channel so the translucent edges retain highlights in the terrain.
    auto spark=[&](double x,double y,double radius,double light) {
        for(int py=std::max(top,int(y-radius));py<std::min(bottom,int(y+radius)+1);++py) {
            auto row=reinterpret_cast<WORD*>(base+(py-top)*pitch);
            for(int px=std::max(left,int(x-radius));px<std::min(right,int(x+radius)+1);++px) {
                double dx=px-x,dy=py-y;
                double v=light*(1-(dx*dx+dy*dy)/(radius*radius));
                if(v<=0) continue;
                WORD& p=row[px-left];
                int r=std::max((p>>rs)&rm,int(rm*v*v*v));
                int g=std::max((p>>gs)&gm,int(gm*v*v));
                int b=std::max((p>>bs)&bm,int(bm*v));
                p=static_cast<WORD>((r<<rs)|(g<<gs)|(b<<bs));
            }
        }
    };
    struct Ray { double x,y,bend; int delay; };
    constexpr Ray rays[]={{.48,.09,-.035,0},{.40,.22,.032,30},{.33,.33,-.05,14},
                          {.19,.40,.043,47},{.075,.36,-.024,22}};
    for(const auto& ray:rays) {
        double t=double(elapsed-ray.delay)/(focusImpactMs-ray.delay);
        if(t<0 || t>1.12) continue;
        double head=std::min(1.,.023+.977*t*t);
        double tail=std::max(0.,t*t-.22);
        if(tail>=head) continue;
        double dx=ray.x*view.Width,dy=ray.y*view.Height,bend=ray.bend*view.Height;
        int steps=std::max(1,int(std::ceil((head-tail)*(std::abs(dx)+dy+4*std::abs(bend)))));
        for(int i=0;i<=steps;++i) {
            double f=double(i)/steps,p=tail+(head-tail)*f;
            double x=cx-dx*(1-p),y=cy+dy*(1-p)+4*bend*p*(1-p);
            spark(x,y,1.4+2.3*f,(.16+.84*f)*std::min(1.,.38+7*t));
        }
    }
    if(elapsed>=focusImpactMs) {
        double t=double(elapsed-focusImpactMs)/(focusDurationMs-focusImpactMs);
        double radius=tab.Width*(.42+.48*t);
        for(int i=0;i<240;++i) {
            double angle=i*(6.28318530718/240);
            spark(cx+radius*std::cos(angle),cy+radius*std::sin(angle),2.1,1-t);
        }
    }
    surface->Unlock();
}

void draw() {
    struct Notice {unsigned generation=0;int house=-1;bool ready=false,free=false;ULONGLONG end=0;};
    static Notice notice;
    int player=HouseClass::CurrentPlayer->ArrayIndex;
    if(notice.generation!=Research::uiGeneration || notice.house!=player) notice={Research::uiGeneration,player,false};
    const ULONGLONG now=GetTickCount64();
    bool sound=!Research::soundFiles[static_cast<size_t>(Research::SoundCue::Available)].empty();
    bool available=(sound || Research::showAvailableAnimation) && hasAvailableResearch();
    const bool newFree=pendingFreeNotices && now>=nextFreeNoticeAt;
    if(newFree) {--pendingFreeNotices;nextFreeNoticeAt=now+focusDurationMs;}
    if(newFree || (available && !notice.ready)) {
        notice.free=newFree;
        if(Research::showAvailableAnimation) notice.end=now+focusDurationMs;
        if(newFree) ResearchAudio::playPath(Research::freeRewardSound);
        else if(sound) ResearchAudio::play(Research::SoundCue::Available);
    }
    if((!available && !notice.free) || !Research::showAvailableAnimation) notice.end=0;
    notice.ready=available;
    auto surface=DSurface::Composite;
    if(cardAnimation.selecting && now-cardAnimation.selectAt>=700) {
        cardAnimation.selecting=false;Research::expanded=false;Research::rewardPanel=0;
        cardAnimation.selectionOffers.clear();
    }
    const auto freeRewards=Research::pendingFreeRewards(player);
    if(Research::expanded && Research::rewardPanel && !cardAnimation.pending && !cardAnimation.selecting &&
       std::find(freeRewards.begin(),freeRewards.end(),Research::rewardPanel)==freeRewards.end()) {
        Research::rewardPanel=freeRewards.empty()?0:freeRewards.front();
        Research::expanded=Research::rewardPanel || Research::balance(player)>=Research::price;
        Research::pressed=Research::pressedOption=-1;
        cardAnimation.wasOpen=false;
    }
    if(Research::expanded && !cardAnimation.wasOpen) {
        cardAnimation.openAt=now;cardAnimation.refreshAt={};
        if(!currentOffers().empty()) ResearchAudio::play(Research::SoundCue::Cards);
    }
    if(!Research::expanded) {cardAnimation.pending=false;cardAnimation.selecting=false;}
    cardAnimation.wasOpen=Research::expanded;
    const auto l=cardAnimation.selecting?cardAnimation.selectionLayout:layout();
    const auto pointer=WWMouseClass::Instance->XY1;
    const int points=Research::balance(HouseClass::CurrentPlayer->ArrayIndex);
    bool affordable=points>=Research::price;
    if(Research::expanded && !Research::rewardPanel && !affordable && !cardAnimation.pending && !cardAnimation.selecting) Research::expanded=false;
    const int white=color(236,239,248), muted=color(184,193,214), accent=color(182,159,255);
    if(Research::expanded) {
        auto bounds=DSurface::ViewBounds;
        ColorStruct shade{5,10,20};
        surface->FillRectTrans(&bounds,&shade,45);
    }
    auto tab=l.tab;
    double flash=0;
    if(notice.end>now) {
        int elapsed=focusDurationMs-static_cast<int>(notice.end-now);
        drawResearchFocus(tab,elapsed);
        if(elapsed>=focusImpactMs) flash=double(notice.end-now)/(focusDurationMs-focusImpactMs);
    }
    drawResearchButton(tab,points,flash);
    const int freeCount=static_cast<int>(freeRewards.size());
    if(freeCount) {
        const auto label=std::to_wstring(freeCount);
        const int width=std::max(18,ResearchText::maskFor(label.c_str(),13).w+8);
        RectangleStruct badge{tab.X+tab.Width-width,tab.Y,width,18};
        surface->FillRect(&badge,color(22,67,100));
        surface->DrawRect(&badge,color(100,216,255));
        centered(label,badge,badge.Y+1,white,13);
    }
    auto info=infoRect(tab);
    surface->FillRect(&info,color(14,23,36));
    surface->DrawRect(&info,inside(info,pointer)?accent:color(75,111,131));
    ResearchText::draw(L"i",info.X+7,info.Y+1,color(182,228,245),15);
    auto completed=Research::completedProjects(HouseClass::CurrentPlayer->ArrayIndex);
    auto page=badgePage(static_cast<int>(completed.size()));
    int hoveredCompleted=-1;
    RectangleStruct hoverRect{};
    for(int slot=0;slot<page.count;++slot) {
        auto rect=completedRect(tab,slot);
        int index=completed[page.first+slot];
        ResearchImages::drawBadge(surface,index,rect);
        if(inside(rect,pointer)) { hoveredCompleted=index;hoverRect=rect;surface->DrawRect(&rect,accent); }
    }
    if(page.pages>1) {
        auto rect=completedRect(tab,iconColumns()-1);
        surface->FillRect(&rect,color(14,23,36));
        surface->DrawRect(&rect,color(75,111,131));
        centered(L"‹",rect,rect.Y+(rect.Height-23)/2,color(182,228,245),23);
    }
    if(!Research::expanded) {
        if(inside(tab,pointer)) drawPointsTip(tab,points,freeCount);
        if(inside(info,pointer)) drawInfoTip(info);
        if(hoveredCompleted>=0) drawTooltip(l,0,hoveredCompleted,&hoverRect);
        return;
    }
    auto panel=l.panel;
    auto close=l.close();
    centered(Research::rewardPanel?L"免费科研":Research::title,panel,panel.Y,color(235,245,252),23);
    surface->FillRect(&close,color(18,29,44));
    surface->DrawRect(&close,inside(close,pointer)?accent:muted);
    centered(L"×",close,close.Y,white,20);
    if(!cardAnimation.selecting && canSwitchReward(freeRewards,player)) {
        auto button=l.nextReward();
        surface->FillRect(&button,color(19,34,49));
        surface->DrawRect(&button,inside(button,pointer)?accent:muted);
        centered(nextReward(freeRewards,player)?L"下一份奖励":L"普通科研",button,button.Y+4,white,15);
    }
    auto offered=cardAnimation.selecting?cardAnimation.selectionOffers:currentOffers();
    int hovered=-1;
    if(offered.empty()) {
        auto empty=l.card(0);
        drawCardSkin(empty);
        centered(L"目前沒有待研究項目",empty,empty.Y+empty.Height/2,muted,16);
    }
    for(int n=0;n<static_cast<int>(offered.size());++n) {
        int i=n;
        if(cardAnimation.selecting) i=n+1==static_cast<int>(offered.size())?cardAnimation.pendingSlot:(n>=cardAnimation.pendingSlot?n+1:n);
        if(cardAnimation.selecting) {
            double t=std::clamp(double(now-cardAnimation.selectAt)/700.0,0.0,1.0);
            bool selected=i==cardAnimation.pendingSlot;
            double opacity=selected?std::clamp((1.0-t)/0.28,0.0,1.0):std::max(0.0,1.0-t*1.55);
            double scale=selected?1.0+0.17*std::sin(2.5*3.141592653589793*t)*std::exp(-2.5*t):1.0;
            int lift=selected?-static_cast<int>(5*t):-static_cast<int>(48*t);
            drawAnimatedCard(l,i,offered[i],{-10000,-10000},scale,lift,static_cast<int>(255*opacity),true);
            if(selected && t<0.8) {
                auto card=l.card(i);
                int w=static_cast<int>(std::lround(card.Width*scale)),h=static_cast<int>(std::lround(card.Height*scale));
                card.X+=(card.Width-w)/2;card.Y+=(card.Height-h)/2+lift;card.Width=w;card.Height=h;
                const auto& view=DSurface::ViewBounds;
                for(int border=2;border>=0;--border) {
                    RectangleStruct glow{card.X-border,card.Y-border,card.Width+2*border,card.Height+2*border};
                    if(glow.X>=view.X && glow.Y>=view.Y && glow.X+glow.Width<=view.X+view.Width && glow.Y+glow.Height<=view.Y+view.Height) {
                        int pulse=static_cast<int>(std::sin(3.141592653589793*t)*opacity*170/(border+1));
                        surface->DrawRect(&glow,color(30+pulse/2,80+pulse,110+pulse/2));
                    }
                }
                glowSweep(card,std::min(1.0,t*1.6),static_cast<int>(130*(1.0-t)));
            }
        } else {
            double t=std::clamp((double(now-cardAnimation.openAt)-i*65.0)/320.0,0.0,1.0);
            if(t<1.0) {
                double eased=1.0-std::pow(1.0-t,3.0);
                double scale=0.88+0.12*eased+0.025*std::sin(3.141592653589793*t);
                drawAnimatedCard(l,i,offered[i],{-10000,-10000},scale,static_cast<int>((1.0-eased)*30),static_cast<int>(255*eased),false);
            } else drawCard(l,i,offered[i],pointer);
            if(cardAnimation.refreshAt[i] && now-cardAnimation.refreshAt[i]<600)
                glowSweep(l.card(i),double(now-cardAnimation.refreshAt[i])/600.0,135);
            if(t>=1.0 && inside(l.card(i),pointer)) hovered=i;
        }
    }
    static int lastOption=-1;
    static DWORD since=0;
    int current=hovered>=0?offered[hovered]:-1;
    if(current!=lastOption) {lastOption=current;since=GetTickCount();}
    if(!cardAnimation.pending && !cardAnimation.selecting && hovered>=0 && GetTickCount()-since>=300) drawTooltip(l,hovered,current);
    if(hoveredCompleted>=0) drawTooltip(l,0,hoveredCompleted,&hoverRect);
    if(inside(tab,pointer)) drawPointsTip(tab,points,freeCount);
    if(inside(info,pointer)) drawInfoTip(info);
}
#include "ActiveSkillUI.inc"
}
void ResearchImages::drawBadge(Surface* surface,int project,RectangleStruct rect) {
    surface->FillRect(&rect,color(14,23,35));
    RectangleStruct glyph{rect.X+2,rect.Y+2,rect.Width-4,rect.Height-4};
    drawProjectIcon(Research::options[project],glyph,surface);
    surface->DrawRect(&rect,color(56,73,89));
}
ResearchImages::Bitmap ResearchImages::tooltip(int project,int house,const std::wstring& text,int width,int maxHeight,unsigned turn) {
    const auto& pages=tooltipPages(project,house,text,width,maxHeight);
    const auto& page=pages[turn%pages.size()];
    Bitmap image(GameCreate<BSurface>(width,pageHeight(page)));
    image->Fill(color(9,13,23));
    paintPage(image.get(),project,0,0,width,page);
    return image;
}
DEFINE_HOOK(0x6D0A20,SF_PrepareCommandBarOrders,8) {
    if(ActiveHUD::Bar::prepareNativeBar()) R->Stack<bool>(4,true);
    return 0;
}
// The expanded bar has painted the slot backgrounds and is about to draw its gadgets.
DEFINE_HOOK(0x6D0D4F,SF_PaintCommandBarOrders,5) {
    ActiveHUD::Bar::paintNativeBar();
    return 0;
}
DEFINE_HOOK(0x724AE2,SF_CommandBarTooltipText,5) {
    const unsigned id=R->EAX<ToolTip*>()->GadgetID;
    if((id&0xFFFF0000u)!=ActiveHUD::Bar::tooltipBase) return 0;
    const unsigned index=id-ActiveHUD::Bar::tooltipBase;
    R->EAX(index<CommandBarOrders::entries.size()?CommandBarOrders::entries[index].tooltip.c_str():L"");
    return 0x724B06;
}
// SetCursor and RestoreCursor both dispatch through UpdateCursor. Resolve the
// requested shape before the engine paints it or resets its animation frame.
DEFINE_HOOK(0x5BDC80, SF_OrderTargetCursor, 7) {
    const auto cursor=ActiveHUD::mapOrderCursor();
    if(cursor!=MouseCursorType::Default) {
        R->Stack(4,cursor);
        R->Stack<bool>(8,false);
    }
    return 0;
}
DEFINE_HOOK(0x5BDAB0, SF_OrderTargetCursorMinimap, 7) {
    if(ActiveHUD::mapOrderCursor()!=MouseCursorType::Default) R->Stack<bool>(4,false);
    return 0;
}
DEFINE_HOOK(0x4F4583, SF_ResearchDraw, 6) {
    SonicRipple::draw(DSurface::Composite,DSurface::ViewBounds,Unsorted::CurrentFrame);
    ResearchAudio::pump();
    if(visible() && WWMouseClass::Instance) draw();
    else cardAnimation=CardAnimation{};
    ActiveHUD::draw();
    return 0;
}
namespace {
void cancelOrderAim() {
    ActiveHUD::Bar::cancel();
    ActiveHUD::ui.aiming=-1;
    ActiveHUD::ui.pressed=ActiveHUD::nothing;
    ActiveHUD::updateOrderCursor();
}
bool hudButton(Point2D pointer,int flags) {
    bool researchVisible=visible();
    int researchHit=researchVisible?hitTest(layout(),pointer):-1;
    if(ActiveHUD::aimingOrder()) {
        if(flags&16) {cancelOrderAim();return true;}
        const int slot=ActiveHUD::Bar::hit(pointer);
        const bool ownButton=slot>=0 && slot==ActiveHUD::ui.barAiming;
        if(ownButton || (inside(DSurface::ViewBounds,pointer) && researchHit<0 && slot<0)) {
            ActiveHUD::input(pointer,flags);
        } else {
            ActiveHUD::ui.pressed=ActiveHUD::nothing;
            ActiveHUD::ui.barPressed=ActiveHUD::nothing;
        }
        if(!ActiveHUD::aimingOrder()) ActiveHUD::updateOrderCursor();
        return true;
    }
    if(ActiveHUD::ui.pressed!=ActiveHUD::nothing && researchHit>=0 && (flags&4)) {
        ActiveHUD::ui.pressed=ActiveHUD::nothing;
        return true;
    }
    if((researchHit<0 || ((flags&16) && (ActiveHUD::ui.aiming>=0 || ActiveHUD::ui.barAiming>=0))) &&
       Research::pressed<0 && ActiveHUD::input(pointer,flags)) return true;
    if(researchHit>=0 && (flags&1)) {ActiveHUD::ui.aiming=-1;ActiveHUD::Bar::cancel();}
    if(!researchVisible) return false;
    if(Research::expanded && (cardAnimation.pending || cardAnimation.selecting)) {
        Research::pressed=-1;Research::pressedOption=-1;
        return true;
    }
    auto hit=researchHit;
    int option=-1;
    if((hit>=1 && hit<=Research::choices) || (hit>=refreshHit && hit<refreshHit+Research::choices)) {
        auto offered=currentOffers();
        int slot=hit>=refreshHit?hit-refreshHit:hit-1;
        if(slot<static_cast<int>(offered.size())) option=offered[slot];
    }
    if(flags & 1) {Research::pressed=hit;Research::pressedOption=option;pressedReward=Research::rewardPanel;}
    if(hit<0 && Research::pressed<0) return false;
    if(flags & 4) {
        if(hit>=0 && hit==Research::pressed && option==Research::pressedOption && pressedReward==Research::rewardPanel) {
            if(hit==nextRewardHit) {
                int player=HouseClass::CurrentPlayer->ArrayIndex;
                Research::rewardPanel=nextReward(Research::pendingFreeRewards(player),player);
                cardAnimation.wasOpen=false;
                playClick();
            }
            else if(hit==closeHit) {
                Research::expanded=false;
                Research::rewardPanel=0;
                playClick();
            }
            else if(hit>=refreshHit && hit<refreshHit+Research::choices) {
                if(!Research::rewardPanel && option>=0) {
                    CellStruct payload{static_cast<short>(hit-refreshHit),static_cast<short>(option)};
                    EventClass event(HouseClass::CurrentPlayer->ArrayIndex,EventType::SpecialPlace,refreshCommandId,payload);
                    if(EventClass::OutList.Add(event)) playClick();
                }
            }
            else if(hit==nextPageHit) {
                int pages=badgePage(static_cast<int>(Research::completedProjects(HouseClass::CurrentPlayer->ArrayIndex).size())).pages;
                Research::badgePageIndex=(Research::badgePageIndex+1)%pages;
                playClick();
            }
            else if(hit==0) {
                const auto rewards=Research::pendingFreeRewards(HouseClass::CurrentPlayer->ArrayIndex);
                Research::expanded=!Research::expanded;
                Research::rewardPanel=Research::expanded && !rewards.empty()?rewards.front():0;
                playClick();
            }
            else if(hit>=1 && hit<=Research::choices && option>=0 && (Research::rewardPanel || Research::balance(HouseClass::CurrentPlayer->ArrayIndex)>=Research::price)) {
                int index=option;
                if(!Research::canApply(HouseClass::CurrentPlayer->ArrayIndex,index)) {
                    Research::pressed=-1;
                    return true;
                }
                const auto id=Research::rewardPanel;
                const int base=(id&Research::ticketFlag)?ticketCommandBase:buildingCommandBase;
                CellStruct payload=id?CellStruct{static_cast<short>(id&0xffff),static_cast<short>((id>>16)&0xffff)}:CellStruct{commandMarker,static_cast<short>(index)};
                EventClass event(HouseClass::CurrentPlayer->ArrayIndex,EventType::SpecialPlace,id?base-index:commandId,payload);
                if(EventClass::OutList.Add(event)) {
                    cardAnimation.pending=true;cardAnimation.pendingOption=index;cardAnimation.pendingSlot=hit-1;
                    cardAnimation.pendingReward=id;cardAnimation.selectionOffers=currentOffers();
                    cardAnimation.selectionLayout=layout();
                    playClick();
                }
            }
        }
        Research::pressed=-1;
    }
    return true;
}

struct HUDMouseCapture {
    unsigned buttons=0,canceled=0;
    unsigned orderClicks=0;
    std::array<bool,256> orderKeys{};
    HWND window=nullptr; // Only set if Starfall acquired Windows mouse capture.
    unsigned generation=0;
    void release() {
        auto owner=window;
        window=nullptr;
        if(owner && GetCapture()==owner) ReleaseCapture();
    }
    void cancel() {
        canceled|=buttons;
        buttons=0;
        Research::pressed=Research::pressedOption=-1;
        ActiveHUD::ui.pressed=ActiveHUD::nothing;
        ActiveHUD::ui.aiming=-1;
        ActiveHUD::Bar::cancel();
        ActiveHUD::updateOrderCursor();
        orderClicks=0;
        release();
    }
} hudCapture;

bool windowMouse(HWND window,UINT msg,WPARAM key,LPARAM coordinates) {
    if(!window || window!=Game::hWnd) return false;
    if(msg==WM_KILLFOCUS || msg==WM_CANCELMODE || (msg==WM_CAPTURECHANGED && hudCapture.buttons && reinterpret_cast<HWND>(coordinates)!=window)) {
        hudCapture.cancel();
        if(msg==WM_KILLFOCUS) hudCapture.orderKeys.fill(false);
        return false;
    }
    if(msg==WM_KEYUP && key<hudCapture.orderKeys.size() && hudCapture.orderKeys[key]) {
        hudCapture.orderKeys[key]=false;
        return true;
    }
    if(msg==WM_KEYDOWN && key<hudCapture.orderKeys.size()) {
        if(hudCapture.orderKeys[key]) return true;
        if(ActiveHUD::aimingOrder() && Game::IsActive && Unsorted::TacticalActive && ActiveHUD::available() &&
           key!=VK_SHIFT && key!=VK_CONTROL && key!=VK_LWIN && key!=VK_RWIN) {
            hudCapture.orderKeys[key]=true;
            if(key==VK_ESCAPE) cancelOrderAim();
            return true;
        }
    }
    unsigned button;
    bool press;
    switch(msg) {
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: button=1;press=true;break;
        case WM_LBUTTONUP: button=1;press=false;break;
        case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: button=2;press=true;break;
        case WM_RBUTTONUP: button=2;press=false;break;
        default: return false;
    }
    if(hudCapture.generation!=ActiveSkills::generation) {
        hudCapture.cancel();hudCapture.generation=ActiveSkills::generation;
    }
    const bool battlefield=Unsorted::TacticalActive && Unsorted::ScenarioStarted && Game::IsActive &&
        !Unsorted::UserInputLocked && ScenarioClass::Instance && !ScenarioClass::Instance->UserInputLocked &&
        HouseClass::CurrentPlayer && TacticalClass::Instance && DSurface::Composite && WWMouseClass::Instance;
    Point2D point{static_cast<short>(LOWORD(coordinates)),static_cast<short>(HIWORD(coordinates))};
    if(!press) {
        if(hudCapture.canceled&button) {
            hudCapture.canceled&=~button;
            return true;
        }
        if(!(hudCapture.buttons&button)) return false; // A map drag must finish in the map handler.
        hudCapture.buttons&=~button;
        if(!battlefield) hudCapture.cancel();
        else if(button==1) {
            hudButton(point,4);
            Research::pressed=Research::pressedOption=-1;
            ActiveHUD::ui.pressed=ActiveHUD::nothing;
            ActiveHUD::ui.barPressed=ActiveHUD::nothing;
        }
        if(!hudCapture.buttons) hudCapture.release();
        return true;
    }
    hudCapture.canceled&=~button;
    if(!battlefield) return false;
    // Windows reports the second press of a double-click after the first release has already confirmed or canceled.
    if((msg==WM_LBUTTONDBLCLK || msg==WM_RBUTTONDBLCLK) && (hudCapture.orderClicks&button)) {
        hudCapture.orderClicks&=~button;
        hudCapture.canceled|=button;
        return true;
    }
    hudCapture.orderClicks&=~button;
    const bool aimingOrder=ActiveHUD::aimingOrder();
    if(!aimingOrder && !inside(DSurface::ViewBounds,point) && ActiveHUD::Bar::hit(point)<0) {
        ActiveHUD::Bar::cancel();
        return false;
    }
    if(!hudButton(point,button==1?1:16)) return false;
    if(aimingOrder) hudCapture.orderClicks|=button;
    hudCapture.buttons|=button;
    if(!GetCapture()) {
        SetCapture(window);
        if(GetCapture()==window) hudCapture.window=window;
    }
    return true;
}
}

// The window procedure dispatches map orders at 0x777640, then queues the same
// mouse message for gadgets at 0x7779CB. Consume owned edges before either path.
DEFINE_HOOK(0x777627, SF_HUDWindowMouse, 8) {
    return windowMouse(R->Stack<HWND>(0x14),R->Stack<UINT>(0x18),R->Stack<WPARAM>(0x1C),R->Stack<LPARAM>(0x20))
        ?0x777A5B:0;
}
// Reuse the game's serialized SpecialPlace envelope with a reserved negative ID.
// All peers grant the upgrade only when the queued command is executed.
DEFINE_HOOK(0x4C6CC8, SF_ResearchCommand, 5) {
    auto event=R->ESI<EventClass*>();
    if(ActiveHUD::command(event)) return 0x4C8109;
    const bool ticket=event->Type==EventType::SpecialPlace && event->SpecialPlace.ID<=ticketCommandBase && event->SpecialPlace.ID>=ticketCommandBase-SHRT_MAX;
    if(ticket || (event->Type==EventType::SpecialPlace && event->SpecialPlace.ID<=buildingCommandBase && event->SpecialPlace.ID>=buildingCommandBase-SHRT_MAX)) {
        int sender=event->HouseIndex,option=(ticket?ticketCommandBase:buildingCommandBase)-event->SpecialPlace.ID;
        Research::RewardId id=(ticket?Research::ticketFlag:0) | static_cast<unsigned short>(event->SpecialPlace.Location.X)|(static_cast<unsigned long>(static_cast<unsigned short>(event->SpecialPlace.Location.Y))<<16);
        bool success=false;
        if(sender>=0 && sender<HouseClass::Array.Count) {
            auto house=HouseClass::Array.Items[sender];
            if(house && !house->Defeated && !house->IsObserver()) success=Research::claimFree(id,sender,option);
            if(success) Research::Detail::showResearchSelection(house,option);
        }
        if(HouseClass::CurrentPlayer && sender==HouseClass::CurrentPlayer->ArrayIndex && cardAnimation.pending && cardAnimation.pendingOption==option && cardAnimation.pendingReward==id) {
            cardAnimation.pending=false;cardAnimation.selecting=success;cardAnimation.selectAt=GetTickCount64();
            if(success) ResearchAudio::play(Research::SoundCue::Select);
        }
        return 0x4C8109;
    }
    if(event->Type==EventType::SpecialPlace && event->SpecialPlace.ID==refreshCommandId) {
        int sender=event->HouseIndex;
        bool success=false;
        if(sender>=0 && sender<HouseClass::Array.Count) {
            auto house=HouseClass::Array.Items[sender];
            if(house && !house->Defeated && !house->IsObserver())
                success=Research::refresh(sender,house->Type->ID,event->SpecialPlace.Location.X,event->SpecialPlace.Location.Y);
        }
        int slot=event->SpecialPlace.Location.X;
        if(success && HouseClass::CurrentPlayer && sender==HouseClass::CurrentPlayer->ArrayIndex && slot>=0 && slot<Research::choices && Research::expanded && !Research::rewardPanel) {
            cardAnimation.refreshAt[slot]=GetTickCount64();
            ResearchAudio::play(Research::SoundCue::Refresh);
        }
        return 0x4C8109;
    }
    if(event->Type!=EventType::SpecialPlace || event->SpecialPlace.ID!=commandId
        || event->SpecialPlace.Location.X!=commandMarker) return 0;
    int sender=event->HouseIndex, option=event->SpecialPlace.Location.Y;
    bool success=false;
    if(sender>=0 && sender<HouseClass::Array.Count && option>=0) {
        auto house=HouseClass::Array.Items[sender];
        if(house && !house->Defeated && !house->IsObserver()) {
            success=Research::purchase(sender,option,house->Type ? house->Type->ID : "");
            if(success) Research::Detail::showResearchSelection(house,option);
        }
    }
    if(HouseClass::CurrentPlayer && sender==HouseClass::CurrentPlayer->ArrayIndex && cardAnimation.pending && cardAnimation.pendingOption==option && !cardAnimation.pendingReward) {
        cardAnimation.pending=false;cardAnimation.selecting=success;cardAnimation.selectAt=GetTickCount64();
        if(success) ResearchAudio::play(Research::SoundCue::Select);
    }
    return 0x4C8109;
}

void Research::clearPresentationCaches(bool stopAudio) {
    if(stopAudio) {pendingFreeNotices=0;nextFreeNoticeAt=0;cardAnimation={};}
    imageCache().clear();
    cardSkinCache()=CardSkin{};
    ResearchAudio::clearCache(stopAudio);
}

void Research::Detail::notifyFreeResearch(int house) {
    auto player=HouseClass::CurrentPlayer;
    if(player && player->ArrayIndex==house) ++pendingFreeNotices;
}
