#pragma once

#include <TechnoClass.h>
#include <map>

namespace VisualScale {
    // Store only non-default values. Base size is serialized; state factors are derived.
    inline std::map<DWORD,int64_t> base;
    inline std::map<DWORD,double> state;
    inline int minimum=25;
    // Non-default launch snapshots. Rendering and detonation read the same value.
    inline std::map<DWORD,int> projectiles;
    inline std::map<DWORD,int> explosions;
    // Attached animation ID -> owning unit ID; its scale is read at draw time.
    inline std::map<DWORD,DWORD> attachedAnims;
    int percent(DWORD id);
    void clearSprites();
}
