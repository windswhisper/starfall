#pragma once
#include <EventClass.h>
#include <WWKeyboardClass.h>

// Exercise the game's window procedure, including the map dispatcher and the WW input queue.
namespace WindowMouse {
inline bool tracing=false,enteredMap=false;
inline bool send(Point2D point,UINT message) {
    auto keyboard=WWKeyboardClass::Instance;
    int tail=keyboard->Tail;
    auto& orders=EventClass::OutList;
    int count=orders.Count;
    vector<DWORD> selection;
    for(auto u:ObjectClass::CurrentObjects) selection.push_back(u->UniqueID);
    WPARAM keys=message==WM_LBUTTONDOWN || message==WM_LBUTTONDBLCLK?MK_LBUTTON:
        message==WM_RBUTTONDOWN || message==WM_RBUTTONDBLCLK?MK_RBUTTON:0;
    enteredMap=false;tracing=true;
    reinterpret_cast<WNDPROC>(0x7775C0)(Game::hWnd,message,keys,MAKELPARAM(point.X,point.Y));
    tracing=false;
    if(enteredMap || keyboard->Tail!=tail || selection.size()!=ObjectClass::CurrentObjects.Count) return false;
    for(int i=0;i<ObjectClass::CurrentObjects.Count;++i) if(selection[i]!=ObjectClass::CurrentObjects[i]->UniqueID) return false;
    for(int i=count;i<orders.Count;++i) {
        auto& order=orders[i];
        if(order.Type!=EventType::SpecialPlace &&
            !(order.Type==EventType::Place && (static_cast<unsigned>(order.Place.RTTIType)>>16)==0x5346)) return false;
    }
    return true;
}
inline bool button(Point2D point,int flags) {
    return send(point,flags==1?WM_LBUTTONDOWN:flags==4?WM_LBUTTONUP:flags==16?WM_RBUTTONDOWN:WM_RBUTTONUP);
}
}
DEFINE_HOOK(0x693107,SF_TestWindowMouseMapDispatch,7) {
    if(WindowMouse::tracing) {
        UINT msg=*R->EAX<UINT*>();
        if(msg>=WM_LBUTTONDOWN && msg<=WM_RBUTTONDBLCLK) WindowMouse::enteredMap=true;
    }
    return 0;
}
