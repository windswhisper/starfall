#include "Research.h"
#include "ResearchImages.h"
#include <Drawing.h>
#include <HouseClass.h>
#include <OwnerDraw.h>
#include <commctrl.h>
#include <windowsx.h>
#pragma comment(lib,"comctl32.lib")

namespace {
constexpr int firstButton=0x5340, playerRows=8;
constexpr int headingId=firstButton+playerRows, projectsId=headingId+1;
constexpr int previousId=projectsId+1,nextId=previousId+1,tooltipId=nextId+1;
constexpr int noHouse=-1;

struct DiplomacyPage {
    HWND dialog=nullptr;
    std::array<int,playerRows> rows;
    int selected=noHouse;
    std::vector<int> projects;
    int first=0,capacity=1,iconSize=0,hovered=-1;
    ResearchImages::Bitmap strip,tip;
    std::wstring hoverText;
    POINT hoverPoint{};
    unsigned bondRevision=0,turn=0;
    ULONGLONG hoverSince=0;
};
DiplomacyPage page;

// Create the children before Yuri's Revenge enumerates and skins the dialog.
// Coordinates are in the same dialog units as the original diplomacy template.
HWND addControl(HWND dialog,int id,const char* kind,DWORD style,RECT rect) {
    if(!MapDialogRect(dialog,&rect)) return nullptr;
    return CreateWindowExA(0,kind,"",WS_CHILD|style,rect.left,rect.top,
        rect.right-rect.left,rect.bottom-rect.top,dialog,reinterpret_cast<HMENU>(id),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtr(dialog,GWLP_HINSTANCE)),nullptr);
}
void prepare(HWND dialog) {
    page={};
    page.rows.fill(noHouse);
    // Start hidden. Only rows populated by the native diplomacy dialog get buttons.
    for(int row=0;row<playerRows;++row) {
        const int y=65+row*20;
        addControl(dialog,firstButton+row,"BUTTON",BS_OWNERDRAW|WS_TABSTOP,{378,y,444,y+17});
    }
    addControl(dialog,headingId,"STATIC",SS_LEFT,{69,275,465,288});
    addControl(dialog,projectsId,"STATIC",SS_OWNERDRAW|SS_NOTIFY,{69,294,407,327});
    addControl(dialog,previousId,"BUTTON",BS_OWNERDRAW|WS_TABSTOP,{414,301,438,321});
    addControl(dialog,nextId,"BUTTON",BS_OWNERDRAW|WS_TABSTOP,{442,301,466,321});
    addControl(dialog,tooltipId,"STATIC",SS_OWNERDRAW,{69,210,295,275});
    for(int id=firstButton;id<=tooltipId;++id) {
        if(!GetDlgItem(dialog,id)) {
            for(int child=firstButton;child<=tooltipId;++child)
                if(auto window=GetDlgItem(dialog,child)) DestroyWindow(window);
            return;
        }
    }
    page.dialog=dialog;
}
void setCaption(HWND control,const wchar_t* text) {
    SendMessageA(control,WW_SETTEXTW,0,reinterpret_cast<LPARAM>(text));
}
// Same borrowed-surface setter as the native country flags on this dialog.
void setImage(HWND window,Surface* image) {
    reinterpret_cast<void(__fastcall*)(HWND,Surface*)>(0x603D30)(window,image);
}
void hideTip() {
    page.hovered=-1;
    ShowWindow(GetDlgItem(page.dialog,tooltipId),SW_HIDE);
}
void showProjects() {
    hideTip();
    const auto strip=GetDlgItem(page.dialog,projectsId);
    const auto heading=GetDlgItem(page.dialog,headingId);
    const auto owner=HouseClass::Array.GetItemOrDefault(page.selected);
    if(!owner) return;
    RECT bounds{};
    GetClientRect(strip,&bounds);
    const int width=bounds.right,height=bounds.bottom;
    if(width<8 || height<8) return;
    page.iconSize=std::min({48,width,height});
    const int pitch=page.iconSize+4;
    page.capacity=(width+4)/pitch;
    const int count=static_cast<int>(page.projects.size());
    const int last=count?(count-1)/page.capacity*page.capacity:0;
    page.first=std::clamp(page.first,0,last);
    std::wstring title=owner->UIName;
    title+=count?L"   研究 ("+std::to_wstring(count)+L")":L"   尚無研究項目";
    if(last) title+=L"   "+std::to_wstring(1+page.first/page.capacity)+L"/"+std::to_wstring(1+last/page.capacity);
    setCaption(heading,title.c_str());
    ShowWindow(heading,SW_SHOWNA);
    if(count) {
        if(!page.strip || page.strip->Width!=width || page.strip->Height!=height)
            page.strip.reset(GameCreate<BSurface>(width,height));
        page.strip->Fill(Drawing::RGB_To_Int(10,16,24));
        for(int i=0;i<page.capacity && page.first+i<count;++i)
            ResearchImages::drawBadge(page.strip.get(),page.projects[page.first+i],
                {i*pitch,(height-page.iconSize)/2,page.iconSize,page.iconSize});
        setImage(strip,page.strip.get());
    }
    ShowWindow(strip,count?SW_SHOWNA:SW_HIDE);
    for(int id:{previousId,nextId}) {
        auto button=GetDlgItem(page.dialog,id);
        setCaption(button,id==previousId?L"<":L">");
        EnableWindow(button,id==previousId?page.first>0:page.first<last);
        ShowWindow(button,last?SW_SHOWNA:SW_HIDE);
    }
}
void refreshProjects(int house) {
    auto items=Research::completedProjects(house);
    if(page.selected==house && page.projects==items) return;
    if(page.selected!=house) page.first=0;
    page.selected=house;
    page.projects=std::move(items);
    showProjects();
}
void updateHover(POINT p) {
    HWND strip=GetDlgItem(page.dialog,projectsId);
    if(!IsWindowVisible(strip) || !page.strip) return;
    const int pitch=page.iconSize+4, y=(page.strip->Height-page.iconSize)/2;
    const int slot=p.x/pitch, index=page.first+slot;
    const bool inside=p.x>=0 && p.x%pitch<page.iconSize && p.y>=y && p.y<y+page.iconSize &&
        slot<page.capacity && index<static_cast<int>(page.projects.size());
    const int project=inside?page.projects[index]:-1;
    if(project<0) {if(page.hovered>=0) hideTip();return;}
    const bool bonds=Research::memberBonds.contains(Research::options[project].id);
    const auto now=GetTickCount64();
    if(page.hovered!=project) page.hoverSince=now;
    const unsigned turn=bonds?static_cast<unsigned>((now-page.hoverSince)/5000):0;
    bool unchanged=page.hovered==project && (!bonds || (page.bondRevision==Research::bondRevision && page.turn==turn));
    if(unchanged && Research::options[project].detailFormulas.empty()) return;
    auto text=Research::describe(project,page.selected);
    if(unchanged && page.hoverText==text) return;
    RECT bounds{};
    GetClientRect(page.dialog,&bounds);
    POINT anchor{slot*pitch,y};
    MapWindowPoints(strip,page.dialog,&anchor,1);
    RECT heading{};
    GetWindowRect(GetDlgItem(page.dialog,headingId),&heading);
    MapWindowPoints(HWND_DESKTOP,page.dialog,reinterpret_cast<POINT*>(&heading),2);
    const int bottom=std::min<LONG>(heading.top,anchor.y)-4;
    const int width=std::min(328L,bounds.right);
    if(width<40 || bottom<64) return;
    // Native player flags paint directly onto Alternate, over child controls in their rows.
    int top=0;
    for(int row=0;row<playerRows;++row) if(page.rows[row]!=noHouse) {
        RECT button{};
        GetWindowRect(GetDlgItem(page.dialog,firstButton+row),&button);
        MapWindowPoints(HWND_DESKTOP,page.dialog,reinterpret_cast<POINT*>(&button),2);
        top=std::max(top,static_cast<int>(button.bottom)+4);
    }
    const int available=bottom-top;
    if(available<60) return;
    auto image=ResearchImages::tooltip(project,page.selected,text,width,available,turn);
    const auto tip=GetDlgItem(page.dialog,tooltipId);
    setImage(tip,image.get());
    page.tip=std::move(image);
    page.hovered=project;
    page.hoverText=std::move(text);
    page.hoverPoint=p;page.bondRevision=Research::bondRevision;page.turn=turn;
    const int left=std::clamp<int>(anchor.x,0,bounds.right-width);
    SetWindowPos(tip,HWND_TOP,left,bottom-page.tip->Height,width,page.tip->Height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
bool paintImage(HWND window,BSurface* image) {
    // The diplomacy dialog is composed on Alternate. Copy the finished image
    // during the child's paint, including repaints after selecting another house.
    auto target=DSurface::Alternate;
    RECT bounds{};
    if(!image || !target || !OwnerDraw::GetRectangle(window,&bounds)) return false;
    RectangleStruct source{0,0,image->Width,image->Height};
    RectangleStruct destination{bounds.left,bounds.top,source.Width,source.Height};
    if(!target->CopyFromPart(&destination,image,&source,false,true)) return false;
    ValidateRect(window,nullptr);
    return true;
}
LRESULT CALLBACK imageProc(HWND window,UINT message,WPARAM param,LPARAM data,UINT_PTR id,DWORD_PTR) {
    if(message==WM_PAINT && paintImage(window,id==projectsId?page.strip.get():page.tip.get())) return 0;
    if(id==projectsId && message==WM_MOUSEMOVE) {
        TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};
        TrackMouseEvent(&tracking);
        updateHover({GET_X_LPARAM(data),GET_Y_LPARAM(data)});
    } else if(id==projectsId && message==WM_MOUSELEAVE && page.hovered>=0) {
        hideTip();
    } else if(message==WM_NCDESTROY) {
        RemoveWindowSubclass(window,imageProc,id);
    }
    return DefSubclassProc(window,message,param,data);
}
INT_PTR CALLBACK diplomacyProc(HWND dialog,UINT message,WPARAM param,LPARAM data) {
    if(message==WM_INITDIALOG) prepare(dialog);
    if(dialog==page.dialog && message==WM_COMMAND && HIWORD(param)==BN_CLICKED) {
        const int row=LOWORD(param)-firstButton;
        if(row>=0 && row<playerRows && page.rows[row]!=noHouse &&
           reinterpret_cast<HWND>(data)==GetDlgItem(dialog,firstButton+row)) {
            refreshProjects(page.rows[row]);
            return TRUE;
        }
        int id=LOWORD(param);
        if((id==previousId || id==nextId) && page.selected!=noHouse &&
            reinterpret_cast<HWND>(data)==GetDlgItem(dialog,id)) {
            page.first+=id==nextId?page.capacity:-page.capacity;
            showProjects();
            return TRUE;
        }
    }
    const bool updateRows=dialog==page.dialog && (message==WW_INITDIALOG || message==WM_TIMER);
    const auto previous=page.rows;
    if(updateRows) page.rows.fill(noHouse);
    const auto native=reinterpret_cast<DLGPROC>(0x657DA0);
    const auto result=native(dialog,message,param,data);
    if(updateRows && page.dialog==dialog) {
        if(message==WW_INITDIALOG) {
            for(int id:{projectsId,tooltipId}) SetWindowSubclass(GetDlgItem(dialog,id),imageProc,id,0);
        }
        for(int row=0;row<playerRows;++row) {
            if(previous[row]==page.rows[row] && message!=WW_INITDIALOG) continue;
            const auto button=GetDlgItem(dialog,firstButton+row);
            if(page.rows[row]!=noHouse) setCaption(button,L"研究");
            ShowWindow(button,page.rows[row]==noHouse?SW_HIDE:SW_SHOWNA);
        }
        if(page.selected!=noHouse) {
            if(std::find(page.rows.begin(),page.rows.end(),page.selected)!=page.rows.end()) {
                refreshProjects(page.selected);
                if(page.hovered>=0) updateHover(page.hoverPoint);
            } else {
                page.selected=noHouse;
                for(int id=headingId;id<=tooltipId;++id) ShowWindow(GetDlgItem(dialog,id),SW_HIDE);
                page.hovered=-1;
            }
        }
    }
    if(message==WM_NCDESTROY && dialog==page.dialog) page={};
    return result;
}
}

// The native opener loads the diplomacy procedure into EDX for dialog 115.
DEFINE_HOOK(0x6586D4,SF_DiplomacyResearchDialog,5) {
    if(!Research::isEnabled()) return 0;
    R->EDX(reinterpret_cast<DWORD>(&diplomacyProc));
    return 0x6586D9;
}

// The engine writes some rows twice to put the local player first. Capture the
// final binding for each row rather than treating the row as a house index.
DEFINE_HOOK(0x658330,SF_DiplomacyResearchRow,6) {
    if(page.dialog && R->Stack<HWND>(4)==page.dialog) {
        const int row=R->Stack<int>(8);
        auto house=R->Stack<HouseClass*>(12);
        if(row>=0 && row<playerRows)
            page.rows[row]=house && house->Type && house->Type->Multiplay?house->ArrayIndex:noHouse;
    }
    return 0;
}
