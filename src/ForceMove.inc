// Horizontal positions stay in the ground layer. Lift is a draw offset, so
// in-transit units remain targetable even when they do not reserve a ground slot.
namespace ForceMove {
enum class Easing { Linear, EaseOut, EaseIn, EaseInOut };
enum class Mode { Offset, Push, Pull, Launch };
struct Spec {
    Mode mode=Mode::Offset;
    Easing easing=Easing::Linear;
    int dx=0,dy=0,distance=0,height=0,duration=0;
    std::optional<double> blowWeight;
    bool impact=false,tilt=true,ignoreCollision=true,affectsIronCurtain=false;
};
constexpr int mapExtent=512*256;
bool leptons(const string& text,int& result,bool signedValue=false) {
    double value=0;
    auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
    if(error!=std::errc{} || end!=text.data()+text.size() || !std::isfinite(value) ||
       value<(signedValue?-512:0) || value>512) return false;
    result=static_cast<int>(std::lround(value*256));
    return true;
}
template<class Field>
bool parse(Spec& spec,Field field) {
    auto mode=field("Action.ForceMove.Mode"),center=field("Action.ForceMove.Center");
    auto offset=field("Action.ForceMove.Offset"),distance=field("Action.ForceMove.Distance");
    auto height=field("Action.ForceMove.Height");
    auto tilt=field("Action.ForceMove.Tilt","yes");
    if(tilt!="yes" && tilt!="no") return false;
    spec.tilt=tilt=="yes";
    auto ignore=field("Action.ForceMove.IgnoreCollision","yes");
    if(ignore!="yes" && ignore!="no") return false;
    spec.ignoreCollision=ignore=="yes";
    auto weight=field("Action.ForceMove.BlowWeight");
    if(!weight.empty()) {
        double limit;
        auto [end,error]=std::from_chars(weight.data(),weight.data()+weight.size(),limit);
        if(error!=std::errc{} || end!=weight.data()+weight.size() || !std::isfinite(limit)) return false;
        spec.blowWeight=limit;
    }
    auto iron=field("Action.ForceMove.AffectsIronCurtain","no");
    if(iron!="yes" && iron!="no") return false;
    spec.affectsIronCurtain=iron=="yes";
    auto easing=field("Action.ForceMove.Easing","linear");
    if(easing=="easeout") spec.easing=Easing::EaseOut;
    else if(easing=="easein") spec.easing=Easing::EaseIn;
    else if(easing=="easeinout") spec.easing=Easing::EaseInOut;
    else if(easing!="linear") return false;
    spec.duration=number(field("Action.ForceMove.Duration"));
    if(spec.duration<1) return false;
    if(!offset.empty()) {
        auto comma=offset.find(',');
        if(!mode.empty() || !center.empty() || !distance.empty() || comma==string::npos ||
           !leptons(trim(offset.substr(0,comma)),spec.dx,true) || !leptons(trim(offset.substr(comma+1)),spec.dy,true)) return false;
    } else {
        if(mode=="push") spec.mode=Mode::Push;
        else if(mode=="pull") spec.mode=Mode::Pull;
        else if(mode=="launch") spec.mode=Mode::Launch;
        else return false;
        if(!center.empty() && center!="self" && center!="impact") return false;
        spec.impact=center=="impact";
        if(!distance.empty() && !leptons(distance,spec.distance)) return false;
        if(spec.mode!=Mode::Launch && !spec.distance) return false;
    }
    if(!leptons(height.empty()?(spec.mode==Mode::Launch?"1":"0"):height,spec.height)) return false;
    return spec.dx || spec.dy || spec.distance || spec.height;
}
struct Motion {
    FootClass* foot=nullptr; // Rebound by UniqueID after load.
    int x=0,y=0,dx=0,dy=0,height=0,startHeight=0,duration=0,elapsed=0;
    unsigned lastFrame=0;
    bool blocked=false,tilt=true,ignoreCollision=true;
    Easing easing=Easing::Linear;
    Point2D position() const {
        if(easing==Easing::Linear)
            return {x+static_cast<int>(int64_t(dx)*elapsed/duration),y+static_cast<int>(int64_t(dy)*elapsed/duration)};
        double t=static_cast<double>(elapsed)/duration;
        double progress=easing==Easing::EaseIn?t*t:easing==Easing::EaseOut?t*(2-t):t*t*(3-2*t);
        return {x+static_cast<int>(std::lround(dx*progress)),y+static_cast<int>(std::lround(dy*progress))};
    }
    int lift() const {
        double t=static_cast<double>(elapsed)/duration;
        return static_cast<int>(std::lround((1-t)*startHeight+4*t*(1-t)*height));
    }
};
std::map<DWORD,Motion> motions;
constexpr unsigned settleFrames=12;
constexpr double maxTilt=Math::Pi/4;
struct SettlingTilt {
    int dx,dy;
    unsigned start;
};
std::map<DWORD,SettlingTilt> settling;
bool tilts(const Motion& motion) {
    return motion.tilt && (motion.dx || motion.dy);
}
void releaseTilt(FootClass* foot,const Motion& motion) {
    if(tilts(motion) && foot->WhatAmI()==AbstractType::Unit && foot->GetTechnoType()->Voxel)
        settling[foot->UniqueID]={motion.dx,motion.dy,static_cast<unsigned>(Unsorted::CurrentFrame)};
}
struct Lean {float forward,side;};
Lean visualTilt(UnitClass* unit) {
    int dx,dy;
    double angle=maxTilt;
    auto active=motions.find(unit->UniqueID);
    if(active!=motions.end() && tilts(active->second)) {
        dx=active->second.dx;dy=active->second.dy;
    } else {
        auto it=settling.find(unit->UniqueID);
        if(it==settling.end()) return {};
        unsigned elapsed=static_cast<unsigned>(Unsorted::CurrentFrame)-it->second.start;
        if(elapsed>=settleFrames) return {};
        double t=static_cast<double>(elapsed)/settleFrames;
        angle*=1-t*t*(3-2*t);
        dx=it->second.dx;dy=it->second.dy;
    }
    // Model X points forward and model Y left. Map Y increases southward.
    double facing=unit->PrimaryFacing.Current().GetRadian<32>();
    double c=std::cos(facing),s=std::sin(facing),length=std::hypot(dx,dy);
    double forward=(c*dx-s*dy)/length,right=(s*dx+c*dy)/length;
    double sine=std::sin(angle);
    return {static_cast<float>(std::asin(sine*forward)),static_cast<float>(std::atan2(sine*right,std::cos(angle)))};
}
bool locked(const AbstractClass* unit) {
    return !motions.empty() && unit && motions.contains(unit->UniqueID);
}
bool unreserved(const AbstractClass* unit) {
    if(!unit || motions.empty()) return false;
    auto it=motions.find(unit->UniqueID);
    return it!=motions.end() && it->second.ignoreCollision;
}
bool eligible(TechnoClass* unit) {
    if(!unit || !unit->IsAlive || unit->Health<=0 || !unit->IsOnMap || unit->InLimbo || unit->Transporter ||
       unit->Absorbed || unit->IsInAir() || unit->IsImmobilized || unit->IsBeingWarpedOut() ||
       unit->IsSinking || unit->IsCrashing) return false;
    if(unit->WhatAmI()!=AbstractType::Unit && unit->WhatAmI()!=AbstractType::Infantry) return false;
    auto foot=static_cast<FootClass*>(unit);
    return foot->Locomotor && foot->TubeIndex<0 && !foot->IsAttackedByLocomotor && !foot->IsDeploying;
}
void stopOrders(FootClass* foot) {
    foot->ClearMegaMissionData();
    foot->NavQueue.Count=0;
    foot->SetTarget(nullptr);
    foot->SetDestination(nullptr,true);
    foot->StopMoving();
    foot->ForceMission(Mission::Sleep);
}
// Stop_Moving lets a drive already on a turn track finish that track. A moving
// target needs a stationary instance of the same locomotor at its current XY.
bool interruptTravel(FootClass* foot) {
    auto old=foot->Locomotor.GetInterfacePtr();
    if(!old->Is_Moving() && !old->Is_Moving_Now()) {stopOrders(foot);return true;}
    IPiggyback* piggy=nullptr;
    if(SUCCEEDED(old->QueryInterface(__uuidof(IPiggyback),reinterpret_cast<void**>(&piggy)))) {
        bool borrowed=piggy->Is_Piggybacking();piggy->Release();
        if(borrowed) return false;
    }
    IPersistStream* persist=nullptr;
    if(FAILED(old->QueryInterface(__uuidof(IPersistStream),reinterpret_cast<void**>(&persist)))) return false;
    CLSID clsid{};
    auto hr=persist->GetClassID(&clsid);persist->Release();
    if(FAILED(hr)) return false;
    ILocomotion* replacement=nullptr;
    if(FAILED(LocomotionClass::CreateInstance(&replacement,&clsid,nullptr,CLSCTX_INPROC_SERVER)) || !replacement) return false;
    if(FAILED(replacement->Link_To_Object(foot))) {replacement->Release();return false;}
    bool powered=old->Is_Powered();
    stopOrders(foot);
    old->Mark_All_Occupation_Bits(MarkType::Up);
    foot->Mark(MarkType::Up);
    foot->Locomotor.Attach(replacement);
    if(!powered) replacement->Power_Off();
    replacement->Unlimbo();
    foot->Mark(MarkType::Down);
    return true;
}
bool terrainAllows(FootClass* foot,CellClass* cell) {
    auto type=foot->GetTechnoType();
    if(!cell || type->SpeedType<SpeedType::Foot || type->SpeedType>SpeedType::FloatBeach) return false;
    auto land=foot->OnBridge?LandType::Clear:cell->LandType;
    if(land<LandType::Clear || static_cast<size_t>(land)>=std::size(GroundType::Array) ||
       GroundType::Array[static_cast<int>(land)].Cost[static_cast<int>(type->SpeedType)]<=0) return false;
    return !cell->GetBuilding() && !cell->GetTerrain(foot->OnBridge) &&
        cell->IsClearToMove(type->SpeedType,true,true,-1,type->MovementZone,-1,foot->OnBridge);
}
bool underneath(FootClass* foot,ObjectClass* obj) {
    if(!obj || obj==foot || !obj->IsAlive || obj->Health<=0 || !obj->IsOnMap || obj->InLimbo ||
       obj->OnBridge!=foot->OnBridge || unreserved(obj)) return false;
    auto kind=obj->WhatAmI();
    return (kind==AbstractType::Infantry || kind==AbstractType::Unit || kind==AbstractType::Aircraft) &&
        !obj->IsInAir() && !static_cast<TechnoClass*>(obj)->Transporter;
}
bool clearForSolid(FootClass* foot) {
    auto cell=foot->GetCell();
    if(!terrainAllows(foot,cell)) return false;
    for(auto obj=foot->OnBridge?cell->AltObject:cell->FirstObject;obj;obj=obj->NextObject)
        if(underneath(foot,obj)) return false;
    return true;
}
bool start(TechnoClass* unit,const Spec& spec,const CoordStruct& center) {
    if(!eligible(unit)) return false;
    if(spec.blowWeight && !(unit->GetTechnoType()->Weight < *spec.blowWeight)) return false;
    if(!spec.affectsIronCurtain && unit->IsIronCurtained() && !unit->ForceShielded) return false;
    auto foot=static_cast<FootClass*>(unit);
    auto origin=foot->Location;
    Motion motion;
    motion.foot=foot;motion.x=origin.X;motion.y=origin.Y;
    motion.dx=spec.dx;motion.dy=spec.dy;
    if(spec.distance) {
        double dx=origin.X-center.X,dy=origin.Y-center.Y,len=std::hypot(dx,dy);
        if(len>0) {
            double scale=(spec.mode==Mode::Pull?-std::min<double>(len,spec.distance):spec.distance)/len;
            motion.dx=static_cast<int>(std::lround(dx*scale));
            motion.dy=static_cast<int>(std::lround(dy*scale));
        }
    }
    motion.lastFrame=Unsorted::CurrentFrame;
    motion.height=spec.height;motion.duration=spec.duration;motion.tilt=spec.tilt;motion.easing=spec.easing;
    if(!motion.dx && !motion.dy && !motion.height) return false;
    motion.ignoreCollision=spec.ignoreCollision;
    auto previous=motions.find(unit->UniqueID);
    bool wasUnreserved=unreserved(foot);
    if(previous!=motions.end()) {
        motion.startHeight=previous->second.lift();
        if(wasUnreserved && !spec.ignoreCollision && !clearForSolid(foot)) return false;
    } else if(!interruptTravel(foot)) return false;
    if(!wasUnreserved && spec.ignoreCollision) foot->UnmarkAllOccupationBits(foot->Location);
    if(tilts(motion)) settling.erase(unit->UniqueID);
    else if(auto old=motions.find(unit->UniqueID);old!=motions.end()) releaseTilt(foot,old->second);
    motions[unit->UniqueID]=motion;
    if(wasUnreserved && !spec.ignoreCollision) foot->MarkAllOccupationBits(foot->Location);
    return true;
}
// Sweep even one-frame moves in short steps to catch intervening cells.
bool moveTo(FootClass* foot,int x,int y,bool ignoreCollision) {
    auto from=foot->Location,pos=from;
    auto cell=foot->GetCell();
    bool bridge=foot->OnBridge;
    auto type=foot->GetTechnoType();
    int dx=x-from.X,dy=y-from.Y;
    int steps=(std::max(std::abs(dx),std::abs(dy))+31)/32;
    for(int i=1;i<=steps;++i) {
        CoordStruct next{from.X+static_cast<int>(int64_t(dx)*i/steps),from.Y+static_cast<int>(int64_t(dy)*i/steps),pos.Z};
        auto p=CellClass::Coord2Cell(next);
        auto& map=MapClass::Instance;
        if(next.X<0 || next.Y<0 || next.X>=mapExtent || next.Y>=mapExtent || !map.IsWithinUsableArea(p,false)) break;
        auto dest=map.TryGetCellAt(p);
        if(!dest) break;
        if(dest!=cell) {
            bool deck=dest->ContainsBridge() && (bridge || cell->Level>=dest->Level+CellClass::BridgeLevels-1);
            auto speed=type->SpeedType;
            int level=dest->Level+(deck?CellClass::BridgeLevels:0);
            if(!ignoreCollision && (std::abs(level-cell->Level-(bridge?CellClass::BridgeLevels:0))>1 ||
               speed<SpeedType::Foot || speed>SpeedType::FloatBeach || dest->GetBuilding() ||
               (deck?dest->AltObject:dest->FirstObject) ||
               !dest->IsClearToMove(speed,false,false,-1,type->MovementZone,-1,deck) ||
               foot->IsCellOccupied(dest,FacingType::None,level,cell,false)!=Move::OK)) break;
            bridge=deck;cell=dest;
        }
        next.Z=map.GetCellFloorHeight(next)+(bridge?CellClass::BridgeHeight:0);
        pos=next;
    }
    if(pos!=from) {
        foot->Mark(MarkType::Up);
        foot->OnBridge=bridge;
        foot->SetLocation(pos);
        foot->Mark(MarkType::Down);
    }
    return pos.X==x && pos.Y==y;
}
void crush(TechnoClass* victim,TechnoClass* falling) {
    int damage=UnitHealth::read(victim).current;
    victim->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,falling,true,false,falling?falling->Owner:nullptr);
}
// A death weapon or a passive can remove either participant during damage.
// Re-resolve native handles before using them again.
bool land(FootClass* foot) {
    auto cell=foot->GetCell();
    TargetClass mover(foot);
    auto building=cell->GetBuilding();
    if(building && building->IsAlive && building->Health>0) {
        int damage=UnitHealth::read(foot).current;
        building->ReceiveDamage(&damage,0,RulesClass::Instance->C4Warhead,foot,false,false,foot->Owner);
        foot=mover.As_Foot();
        if(foot && foot->IsAlive && foot->Health>0) crush(foot,nullptr);
        return false;
    }
    vector<TargetClass> victims;
    for(auto obj=foot->OnBridge?cell->AltObject:cell->FirstObject;obj;obj=obj->NextObject)
        if(underneath(foot,obj)) victims.emplace_back(obj);
    for(auto& handle:victims) {
        auto victim=handle.As_Techno();
        if(underneath(foot,victim)) crush(victim,foot);
        foot=mover.As_Foot();
        if(!foot || !foot->IsAlive || foot->Health<=0 || foot->InLimbo) return false;
    }
    if(!clearForSolid(foot)) {crush(foot,nullptr);return false;}
    return true;
}
void resume(FootClass* foot) {
    foot->ClearMegaMissionData();
    foot->NavQueue.Count=0;
    foot->SetTarget(nullptr);
    foot->AbortMotion();
    foot->QueuedMission=Mission::None;
    foot->SuspendedMission=Mission::None;
    foot->ForceMission(Mission::Guard);
    foot->EnterIdleMode(false,true);
}
void tick() {
    auto frame=static_cast<unsigned>(Unsorted::CurrentFrame);
    std::erase_if(settling,[frame](const auto& e){return frame-e.second.start>=settleFrames;});
    if(motions.empty()) return;
    // A restored motion has no native pointer. Resolve all of them in one scan.
    if(std::any_of(motions.begin(),motions.end(),[](const auto& e){return !e.second.foot;}))
        for(auto foot:FootClass::Array) if(foot) {
            auto found=motions.find(foot->UniqueID);
            if(found!=motions.end()) found->second.foot=foot;
        }
    vector<DWORD> completed;
    for(auto it=motions.begin();it!=motions.end();) {
        auto& m=it->second;
        auto foot=m.foot;
        if(!eligible(foot)) {
            bool ghost=m.ignoreCollision;
            it=motions.erase(it);
            if(foot && foot->IsAlive && foot->Health>0 && foot->IsOnMap && !foot->InLimbo && !foot->Transporter) {
                if(ghost) foot->MarkAllOccupationBits(foot->Location);
                if(foot->CurrentMission==Mission::Sleep) foot->ForceMission(Mission::Guard);
            }
            continue;
        }
        auto now=static_cast<unsigned>(Unsorted::CurrentFrame);
        if(m.lastFrame==now) {++it;continue;}
        m.lastFrame=now;
        ++m.elapsed;
        if(!m.blocked) {
            auto pos=m.position();
            m.blocked=!moveTo(foot,pos.X,pos.Y,m.ignoreCollision);
        }
        foot->MarkForRedraw();
        if(m.elapsed==m.duration || (!m.ignoreCollision && m.blocked && !m.height && !m.startHeight)) {
            completed.push_back(it->first);
        }
        ++it;
    }
    // Complete all movement before applying impacts. Several objects may
    // land this frame, and explosions may invalidate other queued landings.
    for(auto id:completed) {
        auto it=motions.find(id);
        if(it==motions.end()) continue;
        auto motion=it->second;
        auto foot=motion.foot;
        // Drive locomotors assume a stationary vehicle is at its cell center.
        // Keep the landing in the same cell so collision and bridge handling
        // still use the actual destination reached by the motion.
        if(foot->WhatAmI()==AbstractType::Unit) {
            auto pos=foot->Location;
            moveTo(foot,(pos.X/256)*256+128,(pos.Y/256)*256+128,motion.ignoreCollision);
        }
        bool survived=!motion.ignoreCollision || land(foot);
        motions.erase(id);
        if(!survived) continue;
        if(motion.ignoreCollision) foot->MarkAllOccupationBits(foot->Location);
        releaseTilt(foot,motion);
        TargetClass handle(foot);
        foot->UpdatePosition(PCPType::During);
        if(auto live=handle.As_Foot();live && live->IsAlive && live->Health>0) resume(live);
    }
}
}
