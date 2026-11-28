// Private stream in the save container, independent of the game compression stream.
// No process address or unordered-container iteration order is persisted.
namespace {
constexpr wchar_t stateStreamName[]=L"Starfall.Passives";
constexpr DWORD passengerPassivesSaveTag=0x59504653; // SFPY
constexpr DWORD signalSaveTag=0x5A504653; // SFPZ
constexpr DWORD mapPassiveSaveTag=0x5D504653;
constexpr DWORD forceMoveSaveTag=0x5E504653;
constexpr DWORD forceMoveTiltSaveTag=0x5F504653;
constexpr DWORD forceMoveCollisionSaveTag=0x60504653;
constexpr DWORD sequenceSaveTag=0x61504653;
constexpr DWORD seenOffersSaveTag=0x62504653;
constexpr DWORD unhitSaveTag=0x63504653;
constexpr DWORD luaAnimSaveTag=0x64504653;
constexpr DWORD bulletOwnerSaveTag=0x65504653;
constexpr DWORD cloakableSaveTag=0x66504653;
constexpr DWORD pickupSaveTag=0x69504653;
constexpr DWORD researchRoundsSaveTag=0x6A504653;
constexpr DWORD transportCapacitySaveTag=0x6B504653;
constexpr DWORD bombSnapshotSaveTag=0x6C504653;
constexpr DWORD deathTypeSaveTag=0x6F504653;
constexpr DWORD mountSaveTag=0x71504653;
constexpr DWORD landingSaveTag=0x72504653;
constexpr DWORD splitTargetsSaveTag=0x73504653;
constexpr DWORD ejectSpeedSaveTag=0x78504653;
constexpr DWORD researchCounterSaveTag=0x79504653;
constexpr DWORD repeatResearchSaveTag=0x7C504653;
constexpr DWORD researchBondSaveTag=0x7B504653;
constexpr DWORD researchStageSaveTag=0x7A504653;
constexpr DWORD ejectSaveTag=0x77504653;
constexpr DWORD jumpEffectSaveTag=0x76504653;
constexpr DWORD jumpSaveTag=0x75504653;
constexpr DWORD tauntSaveTag=0x74504653;
constexpr DWORD deathTeamSaveTag=0x70504653;
constexpr DWORD replacedResearchSaveTag=0x6E504653;
constexpr DWORD freeResearchSaveTag=0x6D504653;
constexpr DWORD linkedSaveTag=0x68504653;
constexpr DWORD forceMoveAirSaveTag=0x67504653;
constexpr DWORD luaTimerSaveTag=0x5C504653;
constexpr DWORD buildingRewardSourceSaveTag=0x5B504653;
constexpr DWORD auraSaveTag=0x58504653; // SFPX
constexpr DWORD selfCostSaveTag=0x57504653; // SFPW
constexpr DWORD targetCostSaveTag=0x56504653; // SFPV
constexpr DWORD saveTag=0x31504653; // SFP1
struct Writer {
    vector<BYTE> bytes;
    void word(DWORD n) {
        const BYTE* p=reinterpret_cast<const BYTE*>(&n);
        bytes.insert(bytes.end(),p,p+sizeof(n));
    }
    void optionalInt(std::optional<int> value) {
        word(value.has_value());
        if(value) word(*value);
    }
    void real(double value) {
        auto bits=std::bit_cast<unsigned long long>(value);
        word(static_cast<DWORD>(bits));
        word(static_cast<DWORD>(bits>>32));
    }
    void str(const string& s) {
        word(static_cast<DWORD>(s.size()));
        bytes.insert(bytes.end(),s.begin(),s.end());
    }
};
struct Reader {
    const vector<BYTE>& bytes;
    size_t pos=0;
    bool ok=true;
    DWORD word() {
        if(bytes.size()-pos<4) { ok=false; return 0; }
        DWORD n; memcpy(&n,bytes.data()+pos,4); pos+=4; return n;
    }
    std::optional<int> optionalInt() {
        DWORD present=word();
        if(present==1) return static_cast<int>(word());
        if(present!=0) ok=false;
        return std::nullopt;
    }
    double real() {
        unsigned long long bits=word();
        bits|=static_cast<unsigned long long>(word())<<32;
        auto value=std::bit_cast<double>(bits);
        if(!std::isfinite(value)) ok=false;
        return value;
    }
    double percent(bool fractional) {
        double value=fractional?real():static_cast<int>(word());
        if(value<1 || value>INT_MAX) ok=false;
        return value;
    }
    string str(size_t maxLength=127) {
        DWORD n=word();
        if(n>maxLength || n>bytes.size()-pos) { ok=false; return {}; }
        string s(reinterpret_cast<const char*>(bytes.data()+pos),n);
        pos+=n; return s;
    }
};
#include "../../modules/ActSp/UnitOrderSave.inc"
constexpr wchar_t flightStreamName[]=L"Starfall.ProjectileScript";
bool saveFlightState(IStorage* storage) {
    Writer w;
    w.word(1);w.word(static_cast<DWORD>(ProjectileScript::mapSpeeds.size()));
    for(const auto& [key,speed]:ProjectileScript::mapSpeeds) {w.str(key);w.word(speed);}
    w.word(static_cast<DWORD>(ProjectileScript::flights.size()));
    for(const auto& [id,f]:ProjectileScript::flights) {
        w.word(id);w.word(f.age);w.word(f.life);w.word(f.nextTick);w.word(f.weaponSpeed);w.real(f.launchSpeed);
        for(auto v:{f.velocity,f.remainder}) {w.real(v.X);w.real(v.Y);w.real(v.Z);}
        w.word(f.aim.X);w.word(f.aim.Y);w.word(f.aim.Z);w.word(f.target.m_ID);w.word(f.target.m_RTTI);
        LuaTaskData::write(w,f.state);
        if(w.bytes.size()>64*1024*1024) return false;
    }
    IStream* stream=nullptr;
    if(FAILED(storage->CreateStream(flightStreamName,STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,0,&stream))) return false;
    ULONG n=0;auto size=static_cast<ULONG>(w.bytes.size());
    bool ok=SUCCEEDED(stream->Write(w.bytes.data(),size,&n)) && n==size && SUCCEEDED(stream->Commit(STGC_DEFAULT));
    stream->Release();return ok;
}
bool loadFlightState(IStorage* storage) {
    IStream* stream=nullptr;
    auto hr=storage->OpenStream(flightStreamName,nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&stream);
    if(hr==STG_E_FILENOTFOUND) return true;
    if(FAILED(hr)) return false;
    STATSTG stat{};ULONG got=0;
    bool ok=SUCCEEDED(stream->Stat(&stat,STATFLAG_NONAME)) && stat.cbSize.QuadPart<=64*1024*1024;
    vector<BYTE> bytes(ok?stat.cbSize.LowPart:0);
    ok=ok && SUCCEEDED(stream->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&got)) && got==bytes.size();
    stream->Release();
    Reader r{bytes};if(!ok || r.word()!=1) return false;
    decltype(ProjectileScript::mapSpeeds) overrides;
    for(auto n=r.word();n && r.ok;--n) {
        auto key=r.str();int speed=int(r.word());
        if(key.empty() || !overrides.emplace(key,speed).second) return false;
    }
    decltype(ProjectileScript::flights) restored;
    for(auto n=r.word();r.ok && n--;) {
        auto id=r.word();ProjectileScript::Flight f;
        f.age=r.word();f.life=r.word();f.nextTick=r.word();f.weaponSpeed=r.word();f.launchSpeed=r.real();
        f.velocity={r.real(),r.real(),r.real()};f.remainder={r.real(),r.real(),r.real()};
        f.aim={int(r.word()),int(r.word()),int(r.word())};f.target.m_ID=r.word();
        auto kind=r.word();f.target.m_RTTI=static_cast<unsigned char>(kind);
        f.state=LuaTaskData::read(r);
        if(!id || (kind && kind!=unsigned(AbstractType::Abstract) && kind!=unsigned(AbstractType::Cell)) ||
           f.age<0 || f.age>=f.life || f.life>1000000 || f.nextTick<0 ||
           ProjectileScript::length(f.velocity)>4000000 || ProjectileScript::length(f.remainder)>1 ||
           f.state.type!=LuaTaskData::Type::Table || !restored.emplace(id,std::move(f)).second) return false;
    }
    if(!r.ok || r.pos!=bytes.size()) return false;
    ProjectileScript::mapSpeeds.swap(overrides);
    ProjectileScript::flights.swap(restored);return true;
}

bool saveState(IStream* stream) {
    if(!stream) return false;
    const bool saveRepeats=!Research::repeatCounts.empty();
    const bool saveBonds=saveRepeats || !Research::bondProgress.empty();
    const bool saveStages=saveBonds || !Research::stageProgress.empty();
    const bool saveCounters=saveStages || !Research::counterValues.empty();
    const bool saveEject=saveCounters || !ForceMove::ejectOrders.empty() || !ForceMove::descending.empty() ||
        std::any_of(ForceMove::motions.begin(),ForceMove::motions.end(),[](const auto& e){return e.second.landing && e.second.landing->openHeight;});
    const bool saveJumps=saveEject || std::any_of(ForceMove::motions.begin(),ForceMove::motions.end(),[](const auto& e){return e.second.landing.has_value();});
    const bool saveSplits=saveJumps || !Taunt::controls.empty() || !splitTargets.empty();
    const bool saveLandings=saveSplits || std::any_of(pending.begin(),pending.end(),[](const Event& e){return e.landing.has_value();});
    const bool saveMounts=saveLandings || !UnitMount::links.empty();
    const bool saveDeathTypes=saveMounts || std::any_of(pending.begin(),pending.end(),[](const Event& e){return !e.deathType.empty();});
    const bool saveDeathTeams=saveMounts || std::any_of(pending.begin(),pending.end(),[](const Event& e){return e.deathTeam!=0;});
    Writer w;
    w.word(static_cast<DWORD>(counts.size()));
    for(const auto& [id, entries]:counts) {
        w.word(id); w.word(static_cast<DWORD>(entries.size()));
        for(const auto& [skill,p]:entries) { w.str(skill); w.word(p.count); w.word(p.next); }
    }
    w.word(static_cast<DWORD>(pending.size()));
    for(const Event& e:pending) {
        if(saveLandings) {
            w.word(e.landing.has_value());
            if(e.landing) {
                const auto& l=*e.landing;
                w.word(l.unit);w.word(l.house);w.word(l.started);w.word(l.cell.X);w.word(l.cell.Y);
                w.str(l.from);w.str(l.to);w.str(l.skill);w.word(l.nextDraw);
                w.word(e.consumedTarget);w.str(e.consumedKind);w.word(e.consumedOwner);
                w.word(e.consumedPosition.X);w.word(e.consumedPosition.Y);w.word(e.consumedPosition.Z);
                bool extraSnapshot=e.trigger!="onpassengerconsumed" && e.passengerPassives;
                w.word(extraSnapshot?static_cast<DWORD>(e.passengerPassives->size()):0);
                if(extraSnapshot) for(const auto& [skill,layers]:*e.passengerPassives) {w.str(skill);w.word(layers);}
            }
        }
        w.word(e.blastCenter.has_value());
        if(e.blastCenter) {w.word(e.blastCenter->X);w.word(e.blastCenter->Y);w.word(e.blastCenter->Z);}
        w.str(e.trigger); w.str(e.kind); w.str(e.relation);
        w.word(e.holder); w.word(e.attacker); w.word(e.victim);
        w.word(e.damage); w.word(e.hp); w.word(e.maxHp); w.word(e.state);
        w.word(e.hasImpact);w.word(e.impact.X);w.word(e.impact.Y);w.word(e.impact.Z);
        w.word(e.ammo);w.word(e.passengers);w.word(e.owner);w.word(e.origin.X);w.word(e.origin.Y);w.word(e.origin.Z);
        w.word(static_cast<DWORD>(e.snapshot.size()));for(const auto& id:e.snapshot) w.str(id);
        w.word(e.attackerOwner);w.word(e.victimOwner);
        w.real(e.distance);
        w.word(static_cast<DWORD>(e.stackSnapshot.size()));
        for(const auto& [skill,value]:e.stackSnapshot) {w.str(skill);w.word(value);}
        w.word(e.targetHp);w.word(e.targetMaxHp);
        w.optionalInt(e.targetCost);
        w.optionalInt(e.selfCost);
        if(saveDeathTypes && e.trigger=="ondeath") w.str(e.deathType);
        if(saveDeathTeams && e.trigger=="ondeath") w.word(e.deathTeam);
        if(e.trigger=="onpassengerconsumed") {
            w.word(e.passengerPassives?static_cast<DWORD>(e.passengerPassives->size()):0);
            if(e.passengerPassives)
                for(const auto& [id,stacks]:*e.passengerPassives) {w.str(id);w.word(stacks);}
        }
        w.str(e.senderKind);
        if(e.trigger=="onsignal") {
            w.str(e.signal);w.str(e.senderRelation);w.word(e.senderHp);w.word(e.senderMaxHp);w.optionalInt(e.senderCost);
            w.word(e.signalSenderPosition.X);w.word(e.signalSenderPosition.Y);w.word(e.signalSenderPosition.Z);
            w.real(e.signalValue);w.word(e.signalDepth);w.word(e.signalChain?e.signalChain->id:0);w.word(e.signalChain?e.signalChain->remaining:0);
        }
    }
    w.word(static_cast<DWORD>(Research::unlocked.size()));
    for(const auto& [house,ids]:Research::unlocked) {
        w.word(house); w.word(static_cast<DWORD>(ids.size()));
        for(const auto& id:ids) w.str(id);
    }
    w.word(static_cast<DWORD>(Research::savedOffers.size()));
    for(const auto& [house,ids]:Research::savedOffers) {
        w.word(house); w.word(static_cast<DWORD>(ids.size()));
        for(const auto& id:ids) w.str(id);
    }
    w.word(static_cast<DWORD>(Research::points.size()));
    for(auto [house,points]:Research::points) { w.word(house); w.word(points); }
    w.word(static_cast<DWORD>(Research::producers.size()));
    for(auto [id,p]:Research::producers) { w.word(id); w.word(p.owner); w.word(p.elapsed); }
    w.word(static_cast<DWORD>(applied.size()));
    for(const auto& [id,a]:applied) {
        w.word(id);
        for(int k=0;k<3;++k) {
            w.word(a.active[k]);
            w.real(a.base[k]);
            w.real(a.last[k]);
            w.real(a.percent[k]);
        }
    }
    w.word(static_cast<DWORD>(stateTimers.size()));
    for(const auto& [key,timer]:stateTimers) { w.word(key.first); w.str(key.second); w.word(timer.start); }
    w.word(static_cast<DWORD>(removedPassives.size()));
    for(const auto& [id,skills]:removedPassives) {
        w.word(id);w.word(static_cast<DWORD>(skills.size()));for(const auto& skill:skills) w.str(skill);
    }
    w.word(static_cast<DWORD>(passiveBullets.size()));for(auto id:passiveBullets) w.word(id);
    w.word(static_cast<DWORD>(previousState.size()));
    for(auto [id,v]:previousState){w.word(id);w.word(v.rank);w.word(v.deployed);}
    w.word(static_cast<DWORD>(reportedDeaths.size()));for(auto id:reportedDeaths) w.word(id);
    w.word(static_cast<DWORD>(periodic.size()));
    for(const auto& [key,timer]:periodic){w.word(key.first); w.str(key.second); w.word(timer.start);}
    w.word(static_cast<DWORD>(nearbySamples.size()));
    for(const auto& [key,sample]:nearbySamples) {
        w.word(key.first);w.str(key.second);w.word(sample.next);w.word(sample.owner);w.word(sample.matches);
    }
    w.word(static_cast<DWORD>(Research::unlockOrder.size()));
    for(const auto& [house,ids]:Research::unlockOrder) {
        w.word(house);w.word(static_cast<DWORD>(ids.size()));for(const auto& id:ids)w.str(id);
    }
    w.word(static_cast<DWORD>(attachedAnims.size()));
    for(const auto& [key,anim]:attachedAnims) { w.word(key.first); w.str(key.second); w.word(anim.id); }
    w.word(static_cast<DWORD>(activeAnimStates.size()));
    for(const auto& key:activeAnimStates) { w.word(key.first); w.str(key.second); }
    w.word(static_cast<DWORD>(Research::buildingRewards.size()));
    for(const auto& [id,state]:Research::buildingRewards) {w.word(id);w.word(state.owner);w.word(state.authorized);w.word(state.claimed);w.word(static_cast<DWORD>(state.source));}
    w.word(static_cast<DWORD>(VisualScale::base.size()));
    for(const auto& [id,size]:VisualScale::base) {w.word(id);w.word(static_cast<DWORD>(size));w.word(static_cast<DWORD>(size>>32));}
    w.word(static_cast<DWORD>(mirageUnits.size()));for(auto id:mirageUnits) w.word(id);
    w.word(static_cast<DWORD>(warheadGrants.size()));
    for(const auto& [unit,group]:warheadGrants) {
        w.word(unit);w.word(static_cast<DWORD>(group.skills.size()));
        for(const auto& [id,time]:group.skills) {w.str(id);w.word(time.start);w.word(time.duration);}
    }
    for(const auto* values:{&projectileBase,&VisualScale::projectiles}) {
        w.word(static_cast<DWORD>(values->size()));
        for(auto [id,size]:*values) {w.word(id);w.word(size);}
    }
    for(const auto* values:{&rangeBase,&fireRateBase}) {
        w.word(static_cast<DWORD>(values->size()));
        for(auto [id,percent]:*values) {w.word(id);w.real(percent);}
    }
    w.word(static_cast<DWORD>(immuneUnits.size()));
    for(auto id:immuneUnits) w.word(id);
    w.word(static_cast<DWORD>(Research::refreshedSlots.size()));
    for(auto [house,mask]:Research::refreshedSlots) {w.word(house);w.word(mask);}
    w.word(Research::globalIncome.initialized?1:0);
    w.word(Research::globalIncome.lastFrame);
    w.word(static_cast<DWORD>(UnitHealth::pools.size()));
    for(const auto& [id,p]:UnitHealth::pools) {
        w.word(id);w.word(p.base);w.word(p.maximum);w.word(p.current);w.word(p.native);
    }
    w.word(static_cast<DWORD>(gainSeen.size()));
    for(const auto& [unit,ids]:gainSeen) {
        w.word(unit);w.word(static_cast<DWORD>(ids.size()));
        for(const auto& id:ids) w.str(id);
    }
    w.word(0); // Reserved legacy shield-grant records, kept for save layout compatibility.
    w.word(static_cast<DWORD>(VisualScale::explosions.size()));
    for(auto [id,size]:VisualScale::explosions) {w.word(id);w.word(size);}
    w.word(static_cast<DWORD>(spawnGrants.size()));
    for(const auto& [id,grant]:spawnGrants) {
        w.word(id);w.word(grant.parent);w.word(static_cast<DWORD>(grant.ids.size()));
        for(const auto& skill:grant.ids) w.str(skill);
    }
    w.word(static_cast<DWORD>(triggerTotals.size()));
    for(const auto& [unit,entries]:triggerTotals) {
        w.word(unit);w.word(static_cast<DWORD>(entries.size()));
        for(const auto& [skill,total]:entries) {w.str(skill);w.word(total);}
    }
    w.word(static_cast<DWORD>(mindImmuneUnits.size()));
    for(auto id:mindImmuneUnits) w.word(id);
    w.word(static_cast<DWORD>(volleyWeapons.size()));
    for(const auto& [unit,choice]:volleyWeapons) {
        w.word(unit);w.word(choice.slot);w.str(choice.replacement?lower(choice.replacement->ID):string{});w.word(choice.frame);
    }
    w.word(static_cast<DWORD>(passiveStacks.size()));
    for(const auto& [unit,entries]:passiveStacks) {
        w.word(unit);w.word(static_cast<DWORD>(entries.size()));
        for(const auto& [skill,value]:entries) {w.str(skill);w.word(value);}
    }
    // Keep the sampled membership between pulses. Re-scanning here or on load
    // would change gains for units which just crossed the edge of an aura.
    w.word(discoverAuras);
    w.word(static_cast<DWORD>(auraSources.size()));
    for(const auto& [key,source]:auraSources) {
        w.word(key.first);w.str(key.second);w.word(source.owner);
        w.word(static_cast<DWORD>(source.grants.size()));for(const auto& id:source.grants) w.str(id);
        w.word(static_cast<DWORD>(source.targets.size()));for(auto id:source.targets) w.word(id);
    }
    w.word(static_cast<DWORD>(auraLosses.size()));
    for(const auto& [target,ids]:auraLosses) {
        w.word(target);w.word(static_cast<DWORD>(ids.size()));for(const auto& id:ids) w.str(id);
    }
    scriptTimers.ensureSeed(Game::Seed); // Also preserve the stream when saved before the first Lua draw.
    scriptTimers.write(w);
    w.word(static_cast<DWORD>(mapPassiveBindings.size()));
    for(const auto& [type,list]:mapPassiveBindings) {w.str(type);w.str(list);}
    w.word(static_cast<DWORD>(ForceMove::motions.size()));
    for(const auto& [id,m]:ForceMove::motions) {
        w.word(id);
        w.word(m.lastFrame);
        for(int v:{m.x,m.y,m.dx,m.dy,m.height,m.startHeight,m.duration,m.elapsed,int(m.blocked),int(m.tilt),int(m.easing),int(m.ignoreCollision),m.airborneHeight,int(m.nativeTracked)}) w.word(v);
        if(saveJumps) {
            w.word(m.landing.has_value());
            if(m.landing) {
                const auto& l=*m.landing;
                w.word(l.startZ);w.word(l.endZ);w.word(l.level);w.word(l.deck);w.word(l.bridge);w.word(l.crush);
                w.str(l.effect.anim);w.str(l.effect.sound);
                if(saveEject) {
                    w.word(l.openHeight);
                    if(l.openHeight) w.real(l.ejectSpeed);
                }
            }
        }
    }
    w.word(static_cast<DWORD>(ForceMove::settling.size()));
    for(const auto& [id,lean]:ForceMove::settling) {
        w.word(id);w.word(lean.dx);w.word(lean.dy);w.word(lean.start);
    }
    ActionSequence::write(w);
    w.word(static_cast<DWORD>(Research::seenOffers.size()));
    for(const auto& [house,ids]:Research::seenOffers) {
        w.word(house);w.word(static_cast<DWORD>(ids.size()));
        for(const auto& id:ids) w.str(id);
    }
    w.word(static_cast<DWORD>(hitSequences.size()));
    for(const auto& [id,seq]:hitSequences) {w.word(id);w.word(seq);}
    w.word(static_cast<DWORD>(observedHits.size()));
    for(const auto& [key,seq]:observedHits) {w.word(key.first);w.str(key.second);w.word(seq);}
    w.word(static_cast<DWORD>(LuaAnim::entries.size()));
    for(const auto& [id,e]:LuaAnim::entries) {
        w.word(id);w.word(e.owner);w.word(e.cast);w.str(e.skill);w.word(e.remaining);w.word(e.loop);w.word(e.zOffset);
        for(int v:{e.start.X,e.start.Y,e.start.Z,e.finish.X,e.finish.Y,e.finish.Z})w.word(v);
        w.word(e.duration);w.word(e.elapsed);w.word(e.lastFrame);
    }
    if(w.bytes.size()>64*1024*1024) return false;
    w.word(static_cast<DWORD>(passiveBulletOwners.size()));
    for(auto [id,owner]:passiveBulletOwners) {w.word(id);w.word(owner);}
    w.word(static_cast<DWORD>(cloakableUnits.size()));
    for(auto id:cloakableUnits) w.word(id);
    if(w.bytes.size()>64*1024*1024) return false;
    // Even a free-rewards-only house needs the new format to preserve round zero.
    const bool saveReplacements=saveDeathTypes || !Research::retired.empty();
    const bool saveTickets=saveReplacements || Research::lastTicket!=0;
    const bool saveResearchRounds=saveTickets || !bombSnapshots.empty() || !mapPassengersBySize.empty() || Research::sharedRaritySequence || Research::mixedRarityPool || Research::raritySequenceSeed.has_value() || !Research::completedRounds.empty() ||
        std::any_of(Research::buildingRewards.begin(),Research::buildingRewards.end(),[](const auto& entry){return entry.second.authorized>=0;}) ||
        std::any_of(Research::unlocked.begin(),Research::unlocked.end(),[](const auto& entry){return !entry.second.empty();});
    if(!linkedGrants.empty() || !groundPickups.empty() || saveResearchRounds) {
        w.word(static_cast<DWORD>(linkedGrants.size()));
        for(const auto& [unit,ids]:linkedGrants) {
            w.word(unit);w.word(static_cast<DWORD>(ids.size()));
            for(const auto& id:ids) w.str(id);
        }
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(!groundPickups.empty() || saveResearchRounds) {
        w.word(static_cast<DWORD>(groundPickups.size()));
        for(const auto& [cell,pickup]:groundPickups) {
            w.word(cell.first);w.word(cell.second);w.str(pickup.type);w.word(pickup.born);
        }
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(saveResearchRounds) {
        w.word(Research::raritySequenceSeed.value_or(Research::Detail::matchSeed()));
        w.word(static_cast<DWORD>(Research::completedRounds.size()));
        for(auto [house,round]:Research::completedRounds) {w.word(house);w.word(round);}
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(saveTickets || !mapPassengersBySize.empty() || !bombSnapshots.empty()) {
        w.word(static_cast<DWORD>(mapPassengersBySize.size()));
        for(const auto& [type,bySize]:mapPassengersBySize) {w.str(type);w.word(bySize);}
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(saveTickets || !bombSnapshots.empty()) {
        w.word(static_cast<DWORD>(bombSnapshots.size()));
        for(const auto& [id,s]:bombSnapshots) {
            w.word(id);w.real(s.firepower);w.word(s.scale);w.word(s.owner);
            w.word(0); // Reserved list in the bomb snapshot record.
        }
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(saveTickets) {
        w.word(Research::lastTicket);w.word(static_cast<DWORD>(Research::freeTickets.size()));
        for(const auto& [id,ticket]:Research::freeTickets) {
            w.word(id);w.word(ticket.house);
            w.word(ticket.rarity?static_cast<DWORD>(*ticket.rarity):Research::rarityCount);
            w.word(static_cast<DWORD>(ticket.projects.size()));
            for(const auto& project:ticket.projects) w.str(project);
            if(w.bytes.size()>64*1024*1024) return false;
        }
    }
    if(saveReplacements) {
        w.word(static_cast<DWORD>(Research::retired.size()));
        for(const auto& [house,ids]:Research::retired) {
            w.word(house);w.word(static_cast<DWORD>(ids.size()));
            for(const auto& id:ids) w.str(id);
        }
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(saveMounts) {
        w.word(static_cast<DWORD>(UnitMount::links.size()));
        for(const auto& [id,l]:UnitMount::links) {
            w.word(id);w.word(l.parentId);w.str(l.slot);
            w.word(l.offset.X);w.word(l.offset.Y);w.word(l.offset.Z);
            w.word(l.turret);w.word(l.transportFire);w.word(l.carrierId);w.word(l.orderId);
            w.word(static_cast<DWORD>(l.excluded.size()));
            for(const auto& value:l.excluded) w.str(value);
        }
    }
    if(saveSplits) {
        w.word(static_cast<DWORD>(splitTargets.size()));
        for(const auto& [firer,shot]:splitTargets) {
            w.word(firer);w.word(shot.primary);w.word(shot.slot);w.str(shot.weapon);
            w.word(static_cast<DWORD>(shot.branches.size()));
            for(auto id:shot.branches) w.word(id);
        }
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(saveJumps || !Taunt::controls.empty()) {
        w.word(static_cast<DWORD>(Taunt::controls.size()));
        for(const auto& [id,control]:Taunt::controls) {
            w.word(id);w.word(control.current);w.word(static_cast<DWORD>(control.bindings.size()));
            for(const auto& [key,binding]:control.bindings) {
                w.str(key.first);w.word(key.second);w.word(binding.target);
                w.word(binding.time.start);w.word(binding.time.duration);
            }
        }
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(saveEject) {
        w.word(static_cast<DWORD>(ForceMove::ejectOrders.size()));
        for(const auto& [id,q]:ForceMove::ejectOrders) {
            w.word(id);w.word(q.target.X);w.word(q.target.Y);w.word(q.nextFrame);
            w.word(q.spec.range);w.word(q.spec.openHeight);w.real(q.spec.horizontalSpeed);w.word(q.spec.interval);
            w.word(static_cast<DWORD>(q.passengers.size()));for(auto rider:q.passengers) w.word(rider);
        }
        w.word(static_cast<DWORD>(ForceMove::descending.size()));
        for(const auto& [id,cell]:ForceMove::descending) {w.word(id);w.word(cell.X);w.word(cell.Y);}
    }
    if(saveCounters) {
        w.word(static_cast<DWORD>(Research::counterValues.size()));
        for(const auto& [house,entries]:Research::counterValues) {
            w.word(house);w.word(static_cast<DWORD>(entries.size()));
            for(const auto& [key,value]:entries) {w.str(key);w.word(value);}
        }
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(saveStages) {
        w.word(static_cast<DWORD>(Research::stageProgress.size()));
        for(const auto& [key,effects]:Research::stageProgress) {
            const auto& [house,project,stage]=key;
            w.word(house);w.str(project);w.str(stage);w.word(static_cast<DWORD>(effects.size()));
            for(const auto& id:effects) w.str(id);
        }
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(saveBonds) {
        w.word(static_cast<DWORD>(Research::bondProgress.size()));
        for(const auto& [key,state]:Research::bondProgress) {
            const auto& [house,bond,tier]=key;
            w.word(house);w.str(bond);w.str(tier);w.word(state.active);
            w.word(static_cast<DWORD>(state.grants.size()));
            for(const auto& id:state.grants) w.str(id);
        }
        if(w.bytes.size()>64*1024*1024) return false;
    }
    if(saveRepeats) {
        w.word(static_cast<DWORD>(Research::repeatCounts.size()));
        for(const auto& [key,count]:Research::repeatCounts) {
            w.word(key.first);w.str(key.second);w.word(count);
        }
        if(w.bytes.size()>64*1024*1024) return false;
    }
    DWORD header[]={saveRepeats?repeatResearchSaveTag:saveBonds?researchBondSaveTag:saveStages?researchStageSaveTag:saveCounters?researchCounterSaveTag:saveEject?ejectSpeedSaveTag:saveJumps?jumpEffectSaveTag:!Taunt::controls.empty()?tauntSaveTag:saveSplits?splitTargetsSaveTag:saveLandings?landingSaveTag:saveMounts?mountSaveTag:saveDeathTeams?deathTeamSaveTag:saveDeathTypes?deathTypeSaveTag:saveReplacements?replacedResearchSaveTag:saveTickets?freeResearchSaveTag:!bombSnapshots.empty()?bombSnapshotSaveTag:!mapPassengersBySize.empty()?transportCapacitySaveTag:saveResearchRounds?researchRoundsSaveTag:
        !groundPickups.empty()?pickupSaveTag:linkedGrants.empty()?forceMoveAirSaveTag:linkedSaveTag,static_cast<DWORD>(w.bytes.size())};
    ULONG written=0;
    return SUCCEEDED(stream->Write(header,sizeof(header),&written)) && written==sizeof(header)
        && SUCCEEDED(stream->Write(w.bytes.data(),header[1],&written)) && written==header[1];
}
bool loadState(IStream* stream) {
    auto fail=[](const char* reason) {
        std::ofstream("Starfall.log",std::ios::app)<<"Starfall: save data rejected: "<<reason<<"\n";
        return false;
    };
    if(!stream) return fail("missing stream");
    DWORD header[2]{};
    ULONG got=0;
    if(FAILED(stream->Read(header,sizeof(header),&got)) || got!=sizeof(header)) return fail("truncated header");
    if((header[0]&0xFFFFFF)!=(saveTag&0xFFFFFF) || header[0]<saveTag || header[0]>repeatResearchSaveTag)
        return fail("unsupported format version");
    if(header[1]>64*1024*1024) return fail("payload exceeds size limit");
    vector<BYTE> data(header[1]);
    if(FAILED(stream->Read(data.data(),header[1],&got)) || got!=header[1]) return fail("truncated payload");
    Reader r{data};
    Counts restored;
    vector<Event> events;
    std::map<DWORD,std::shared_ptr<SignalChain>> signalChains;
    DWORD restoredNextSignalChain=1;
    for(DWORD n=r.word(); n && r.ok; --n) {
        DWORD id=r.word();
        for(DWORD m=r.word(); m && r.ok; --m) {
            string skill=r.str(); int count=static_cast<int>(r.word()); DWORD next=r.word();
            if(count<0 || count==INT_MAX || skill.empty()) r.ok=false;
            else restored[id][skill]={count,next};
        }
    }
    for(DWORD n=r.word(); n && r.ok; --n) {
        std::optional<LandingOrder> landing;
        std::shared_ptr<PassengerPassives> landingSnapshot;
        DWORD consumed=0;string kind;int owner=-1;CoordStruct position{};
        if(header[0]>=landingSaveTag && r.word()) {
            auto unit=r.word(),house=r.word(),started=r.word(),x=r.word(),y=r.word();
            LandingOrder order{unit,house,started,{short(x),short(y)},r.str(),r.str(),r.str(),static_cast<int>(r.word())};
            consumed=r.word();kind=r.str();owner=r.word();position={int(r.word()),int(r.word()),int(r.word())};
            if(auto count=r.word()) {
                landingSnapshot=std::make_shared<PassengerPassives>();
                for(;r.ok && count;--count) {
                    auto id=r.str();int layers=r.word();
                    if(id.empty() || layers<0 || !landingSnapshot->emplace(id,layers).second) r.ok=false;
                }
            }
            if(!unit || house>=128 || x>=512 || y>=512 || order.nextDraw<1 || order.skill.empty() || order.from.empty() || order.to.empty()) r.ok=false;
            landing=std::move(order);
        }
        std::optional<CoordStruct> center;
        if(header[0]>=forceMoveSaveTag && r.word())
            center=CoordStruct{static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
        Event e{r.str(),r.str(),r.str(),r.word(),r.word(),r.word(),
            static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
        e.blastCenter=center;
        if(header[0]>=0x35504653) e.state=static_cast<int>(r.word());
        if(header[0]>=0x37504653) {
            e.hasImpact=r.word()!=0;e.impact={static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
        }
        if(header[0]>=0x38504653) {
            e.ammo=r.word();e.passengers=r.word();e.owner=r.word();
            e.origin={static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
            for(DWORD n=r.word(); r.ok && n--;) e.snapshot.push_back(r.str());
        }
        if(header[0]>=0x3C504653) {
            e.attackerOwner=static_cast<int>(r.word());e.victimOwner=static_cast<int>(r.word());
            if(e.attackerOwner< -1 || e.attackerOwner>127 || e.victimOwner< -1 || e.victimOwner>127) r.ok=false;
        }
        if(header[0]>=0x51504653) {e.distance=r.real();if(e.distance<0 && e.distance!=-1) r.ok=false;}
        if(header[0]>=0x54504653) for(DWORD k=r.word();r.ok && k--;) {
            auto skill=r.str();int value=static_cast<int>(r.word());
            if(skill.empty() || value<0 || !e.stackSnapshot.emplace(skill,value).second) r.ok=false;
        }
        if(header[0]>=0x55504653) {
            e.targetHp=static_cast<int>(r.word());e.targetMaxHp=static_cast<int>(r.word());
            if(e.targetHp<0 || e.targetMaxHp<0 || e.targetHp>e.targetMaxHp) r.ok=false;
        }
        if(header[0]>=targetCostSaveTag) e.targetCost=r.optionalInt();
        if(header[0]>=selfCostSaveTag) e.selfCost=r.optionalInt();
        if(header[0]>=deathTypeSaveTag && e.trigger=="ondeath") e.deathType=r.str();
        if(header[0]>=deathTeamSaveTag && e.trigger=="ondeath") e.deathTeam=r.word();
        if(e.trigger=="onpassengerconsumed") {
            e.consumedTarget=e.victim;
            if(header[0]>=passengerPassivesSaveTag) {
                e.passengerPassives=std::make_shared<PassengerPassives>();
                for(DWORD k=r.word();r.ok && k--;) {
                    auto id=r.str();int stacks=static_cast<int>(r.word());
                    if(id.empty() || stacks<0 || !e.passengerPassives->emplace(id,stacks).second) r.ok=false;
                }
            }
        }
        if(header[0]>=signalSaveTag) {
            e.senderKind=r.str();
            if(e.trigger=="onsignal") {
                e.signal=r.str();e.senderRelation=r.str();e.senderHp=static_cast<int>(r.word());e.senderMaxHp=static_cast<int>(r.word());e.senderCost=r.optionalInt();
                e.signalSenderPosition={static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
                e.signalValue=r.real();e.signalDepth=r.word();DWORD id=r.word(),remaining=r.word();
                if(e.signal.empty() || e.senderKind.empty() || e.senderRelation.empty() || e.senderHp<0 || e.senderMaxHp<=0 || e.senderHp>e.senderMaxHp ||
                   !id || remaining>1024 || e.signalDepth<1 || e.signalDepth>16) r.ok=false;
                auto [it,created]=signalChains.try_emplace(id,std::make_shared<SignalChain>(SignalChain{id,remaining}));
                if(!created && it->second->remaining!=remaining) r.ok=false;
                e.signalChain=it->second;
                if(id>=restoredNextSignalChain) restoredNextSignalChain=id+1;
            }
        }
        if(landing) {
            e.landing=std::move(landing);e.consumedTarget=consumed;e.consumedKind=std::move(kind);
            e.consumedOwner=owner;e.consumedPosition=position;
            if(landingSnapshot) e.passengerPassives=std::move(landingSnapshot);
        }
        events.push_back(std::move(e));
    }
    Research::Unlocks upgrades;
    if(r.ok && r.pos<data.size()) {
        DWORD n=r.word();
        if(n>256) r.ok=false;
        while(r.ok && n--) {
            int house=static_cast<int>(r.word());
            if(house<0 || house>127 || upgrades.contains(house)) { r.ok=false; break; }
            DWORD size=header[0]>=0x34504653?r.word():1;
            auto& ids=upgrades[house];
            while(r.ok && size--) {
            string id;
            if(header[0]==saveTag) {
                DWORD index=r.word(); if(index<Research::choices) id=Research::legacyIds[index];
            } else id=r.str();
            if(id.empty() || !ids.insert(id).second) r.ok=false;
            }
        }
    }
    std::map<int,vector<string>> offers;
    if(header[0]>=0x33504653) {
        DWORD houses=r.word();
        if(houses>128) r.ok=false;
        while(r.ok && houses--) {
            DWORD house=r.word(), size=r.word();
            if(house>127 || size>Research::choices || offers.contains(house)) { r.ok=false; break; }
            auto& ids=offers[house];
            while(r.ok && size--) {
                auto id=r.str(); if(id.empty() || includes(ids,id)) r.ok=false;
                else ids.push_back(std::move(id));
            }
        }
    }
    decltype(Research::points) points;
    decltype(Research::producers) producers;
    if(header[0]>=0x34504653) {
        for(DWORD n=r.word();r.ok && n--;){
            int house=r.word(), amount=r.word();
            if(house<0 || house>127 || amount<0 || !points.emplace(house,amount).second) r.ok=false;
        }
        for(DWORD n=r.word();r.ok && n--;){
            DWORD id=r.word(); int owner=r.word(), elapsed=r.word();
            if(owner<0 || owner>127 || elapsed<0 || elapsed==INT_MAX || !producers.emplace(id,Research::Producer{owner,elapsed}).second) r.ok=false;
        }
    }
    decltype(applied) states;
    if(header[0]>=0x35504653) {
        for(DWORD n=r.word();r.ok && n--;) {
            DWORD id=r.word(); Applied a;
            for(int k=0;k<3;++k) {
                DWORD active=r.word(); if(active>1) r.ok=false;
                a.active[k]=active!=0;
                a.base[k]=r.real();
                a.last[k]=r.real();
                if(header[0]>=0x42504653) a.percent[k]=r.percent(header[0]>=0x48504653);
            }
            if(!states.emplace(id,a).second) r.ok=false;
        }
    }
    decltype(stateTimers) timers;
    if(header[0]>=0x36504653) {
        for(DWORD n=r.word();r.ok && n--;) {
            auto id=r.word(); auto skill=r.str(); auto start=r.word();
            if(skill.empty() || !timers.emplace(TimerKey{id,skill},StateTimer{start,false}).second) r.ok=false;
        }
    }
    decltype(removedPassives) removed; decltype(passiveBullets) bullets;
    if(header[0]>=0x37504653) {
        for(DWORD n=r.word(); r.ok && n--;) {
            DWORD id=r.word(),size=r.word(); auto& ids=removed[id];
            while(r.ok && size--) { auto key=r.str(); if(key.empty() || !ids.insert(key).second) r.ok=false; }
        }
        for(DWORD n=r.word();r.ok && n--;) if(!bullets.insert(r.word()).second) r.ok=false;
    }
    decltype(previousState) statesBefore;decltype(reportedDeaths) deaths;
    if(header[0]>=0x38504653) {
        for(DWORD n=r.word();r.ok && n--;) {DWORD id=r.word();int rank=r.word(); auto flag=r.word();
            if(rank<0 || rank>2 || flag>1 || !statesBefore.emplace(id,Transition{rank,flag!=0}).second) r.ok=false;}
        for(DWORD n=r.word();r.ok && n--;) if(!deaths.insert(r.word()).second) r.ok=false;
    }
    decltype(periodic) clocks;
    if(header[0]>=0x39504653) {
        for(DWORD n=r.word();r.ok && n--;) {
            DWORD id=r.word();auto skill=r.str();DWORD start=r.word();
            if(skill.empty() || !clocks.emplace(TimerKey{id,skill},PeriodicClock{start,false}).second) r.ok=false;
        }
    }
    decltype(nearbySamples) samples;
    if(header[0]>=0x3A504653) {
        for(DWORD n=r.word();r.ok && n--;) {
            auto id=r.word();auto skill=r.str();unsigned next=r.word();int house=r.word();DWORD match=r.word();
            if(skill.empty() || match>1 || house< -1 || house>127 || !samples.emplace(TimerKey{id,skill},NearbySample{next,house,match!=0,false}).second) r.ok=false;
        }
    }
    decltype(Research::unlockOrder) order;
    if(header[0]>=0x3B504653) {
        for(DWORD n=r.word();r.ok && n--;) {
            int house=static_cast<int>(r.word());DWORD size=r.word();
            if(house<0 || house>127 || order.contains(house) || size>10000) {r.ok=false;break;}
            auto& ids=order[house];
            while(r.ok && size--) {
                auto id=r.str();
                if(id.empty() || !upgrades[house].contains(id) || includes(ids,id)) r.ok=false;
                else ids.push_back(std::move(id));
            }
        }
    }
    decltype(attachedAnims) attachments;
    decltype(activeAnimStates) animStates;
    if(header[0]>=0x3D504653) {
        std::set<DWORD> used;
        for(DWORD n=r.word();r.ok && n--;) {
            TimerKey key{r.word(),r.str()}; DWORD id=r.word();
            if(key.second.empty() || !used.insert(id).second || !attachments.emplace(key,AttachedAnim{id}).second) r.ok=false;
        }
        for(DWORD n=r.word();r.ok && n--;) {
            TimerKey key{r.word(),r.str()};
            if(key.second.empty() || !animStates.insert(key).second) r.ok=false;
        }
    }
    decltype(Research::buildingRewards) rewards;
    if(header[0]>=0x3E504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();int owner=static_cast<int>(r.word()),authorized=static_cast<int>(r.word());DWORD claimed=r.word();
        DWORD source=header[0]>=buildingRewardSourceSaveTag?r.word():0;
        if(header[0]<buildingRewardSourceSaveTag) authorized=-1; // Old saves cannot distinguish how authorization was granted.
        if(owner<0 || owner>127 || authorized< -1 || authorized>127 || claimed>1 || source>2 ||
           (claimed && (authorized!=-1 || source!=0)) || ((authorized==-1)!=(source==0)) ||
           !rewards.emplace(id,Research::BuildingReward{owner,authorized,claimed!=0,static_cast<Research::BuildingRewardSource>(source)}).second) r.ok=false;
    }
    decltype(VisualScale::base) sizes;
    if(header[0]>=0x3F504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();uint64_t size=r.word();
        if(header[0]>=0x41504653) size|=static_cast<uint64_t>(r.word())<<32;
        if(!size || size>INT64_MAX || !sizes.emplace(id,static_cast<int64_t>(size)).second) r.ok=false;
    }
    decltype(mirageUnits) mirages;
    if(header[0]>=0x40504653) for(DWORD n=r.word();r.ok && n--;) if(!mirages.insert(r.word()).second) r.ok=false;
    decltype(warheadGrants) grants;
    if(header[0]>=0x43504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD unit=r.word(),count=r.word();
        if(!count || grants.contains(unit)) {r.ok=false;break;}
        auto& group=grants[unit];
        for(DWORD i=0;i<count && r.ok;++i) {
            auto id=r.str();unsigned start=r.word();int duration=static_cast<int>(r.word());
            if(id.empty() || (duration!=-1 && duration<=0) ||
               !group.skills.emplace(id,GrantTime{start,duration}).second) r.ok=false;
        }
    }
    std::map<DWORD,int> launchSizes,shotSizes;
    if(header[0]>=0x44504653) for(auto* values:{&launchSizes,&shotSizes}) {
        for(DWORD n=r.word();r.ok && n--;) {
            DWORD id=r.word();
            int size=static_cast<int>(r.word());
            if(size<=0 || !values->emplace(id,size).second) r.ok=false;
        }
    }
    decltype(rangeBase) weaponRanges,fireRates;
    if(header[0]>=0x45504653) for(auto* values:{&weaponRanges,&fireRates}) {
        if(values==&fireRates && header[0]<0x46504653) break;
        for(DWORD n=r.word();r.ok && n--;) {
            DWORD id=r.word();
            double value=r.percent(header[0]>=0x48504653);
            if(!values->emplace(id,value).second) r.ok=false;
        }
    }
    decltype(immuneUnits) immunes;
    if(header[0]>=0x47504653) for(DWORD n=r.word();r.ok && n--;) {
        if(!immunes.insert(r.word()).second) r.ok=false;
    }
    decltype(Research::refreshedSlots) refreshes;
    if(header[0]>=0x49504653) {
        DWORD houses=r.word();
        if(houses>128) r.ok=false;
        while(r.ok && houses--) {
            DWORD house=r.word(),mask=r.word();
            if(house>127 || !mask || mask>=(1u<<Research::choices) || !refreshes.emplace(house,mask).second) r.ok=false;
        }
    }
    DWORD incomeInitialized=1,incomeLast=static_cast<DWORD>(Unsorted::CurrentFrame);
    if(header[0]>=0x4A504653) {incomeInitialized=r.word();incomeLast=r.word();if(incomeInitialized>1) r.ok=false;}
    decltype(UnitHealth::pools) healthPools;
    if(header[0]>=0x4B504653) for(DWORD n=r.word();r.ok && n--;) {
        auto id=r.word();
        UnitHealth::Pool p{static_cast<int>(r.word()),static_cast<int>(r.word()),
            static_cast<int>(r.word()),static_cast<int>(r.word())};
        if(p.base<1 || p.maximum<1 || p.current<0 || p.current>p.maximum ||
           p.native!=p.project(p.current) || !healthPools.emplace(id,p).second) r.ok=false;
    }
    decltype(gainSeen) acquired;
    if(header[0]>=0x4C504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD unit=r.word(),count=r.word();
        if(!count || acquired.contains(unit)) {r.ok=false;break;}
        auto& ids=acquired[unit];
        while(r.ok && count--) {
            auto id=r.str();
            if(id.empty() || includes(ids,id)) r.ok=false;
            else ids.push_back(std::move(id));
        }
    }
    std::set<DWORD> legacyShieldUnits;
    if(header[0]>=0x4D504653) for(DWORD n=r.word();r.ok && n--;) {
        auto unit=r.word();
        auto type=r.str();r.word();int duration=static_cast<int>(r.word());
        if(type.empty() || type.size()>31 || (duration!=-1 && duration<=0) ||
           !legacyShieldUnits.insert(unit).second) r.ok=false;
    }
    decltype(VisualScale::explosions) explosions;
    if(header[0]>=0x4E504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();
        int size=static_cast<int>(r.word());
        if(size<=0 || !explosions.emplace(id,size).second) r.ok=false;
    }
    decltype(spawnGrants) restoredSpawns;
    if(header[0]>=0x4F504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();
        SpawnGrant grant{r.word(),{}};
        for(DWORD k=r.word();r.ok && k--;) {
            auto skill=r.str();
            if(skill.empty() || includes(grant.ids,skill)) r.ok=false;
            else grant.ids.push_back(std::move(skill));
        }
        if(grant.ids.empty() || !restoredSpawns.emplace(id,std::move(grant)).second) r.ok=false;
    }
    decltype(triggerTotals) restoredTotals;
    if(header[0]>=0x50504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD unit=r.word(),size=r.word();
        if(restoredTotals.contains(unit)) {r.ok=false;break;}
        auto& entries=restoredTotals[unit];
        while(r.ok && size--) {
            auto skill=r.str();auto total=r.word();
            if(skill.empty() || !total || total>INT_MAX || !entries.emplace(skill,total).second) r.ok=false;
        }
    }
    decltype(mindImmuneUnits) restoredMindImmunes;
    if(header[0]>=0x52504653) for(DWORD n=r.word();r.ok && n--;) {
        if(!restoredMindImmunes.insert(r.word()).second) r.ok=false;
    }
    decltype(volleyWeapons) restoredVolleys;
    if(header[0]>=0x53504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();int slot=static_cast<int>(r.word());auto name=r.str();unsigned frame=r.word();
        auto weapon=name.empty()?nullptr:WeaponTypeClass::Find(name.c_str());
        if(!name.empty() && !weapon) std::ofstream("Starfall.log",std::ios::app)<<"Starfall: missing saved volley weapon "<<name<<", using original\n";
        if(slot<0 || slot>1 || !restoredVolleys.emplace(id,VolleyWeapon{slot,weapon,frame}).second) r.ok=false;
    }
    decltype(passiveStacks) restoredStacks;
    if(header[0]>=0x54504653) for(DWORD n=r.word();r.ok && n--;) {
        DWORD unit=r.word(),size=r.word();
        if(!size || restoredStacks.contains(unit)) {r.ok=false;break;}
        auto& entries=restoredStacks[unit];
        while(r.ok && size--) {
            auto skill=r.str();int value=static_cast<int>(r.word());
            if(skill.empty() || value<=0 || !entries.emplace(skill,value).second) r.ok=false;
        }
    }
    decltype(auraSources) restoredAuras;
    decltype(auraLosses) restoredLosses;
    bool discoverAfterLoad=true;
    if(header[0]>=auraSaveTag && r.ok) {
        auto discovery=r.word();
        if(discovery>1) r.ok=false;
        discoverAfterLoad=discovery!=0;
        for(DWORD n=r.word();r.ok && n--;) {
            DWORD emitter=r.word();auto id=r.str();
            auto [it,added]=restoredAuras.try_emplace({emitter,id});
            auto& source=it->second;
            source.owner=static_cast<int>(r.word());
            if(!added || id.empty() || source.owner< -1 || source.owner>127) r.ok=false;
            for(DWORD k=r.word();r.ok && k--;) {
                auto skill=r.str();
                if(skill.empty() || includes(source.grants,skill)) r.ok=false;
                else source.grants.push_back(std::move(skill));
            }
            for(DWORD k=r.word();r.ok && k--;)
                if(source.grants.empty() || !source.targets.insert(r.word()).second) r.ok=false;
        }
        for(DWORD n=r.word();r.ok && n--;) {
            auto target=r.word(),size=r.word();
            if(!size || restoredLosses.contains(target)) {r.ok=false;break;}
            auto& ids=restoredLosses[target];
            while(r.ok && size--) {
                auto id=r.str();if(id.empty() || !ids.insert(id).second) r.ok=false;
            }
        }
    }
    LuaTimers::Store restoredTimers;
    if(header[0]>=luaTimerSaveTag && r.ok) restoredTimers.read(r);
    decltype(mapPassiveBindings) restoredBindings;
    if(header[0]>=mapPassiveSaveTag && r.ok) for(auto n=r.word();r.ok && n--;) {
        auto type=r.str(),list=r.str(2047);
        if(type.empty() || list.empty() || !restoredBindings.emplace(std::move(type),std::move(list)).second) r.ok=false;
    }
    decltype(ForceMove::motions) moves;
    if(header[0]>=forceMoveSaveTag && r.ok) for(auto n=r.word();r.ok && n--;) {
        DWORD id=r.word();ForceMove::Motion m;m.lastFrame=r.word();
        for(auto out:{&m.x,&m.y,&m.dx,&m.dy,&m.height,&m.startHeight,&m.duration,&m.elapsed})
            *out=static_cast<int>(r.word());
        auto blocked=r.word();m.blocked=blocked!=0;
        if(header[0]>=forceMoveTiltSaveTag) {
            auto tilt=r.word();if(tilt>1) r.ok=false;
            m.tilt=tilt!=0;
            auto easing=r.word();
            if(easing>static_cast<DWORD>(ForceMove::Easing::EaseInOut)) r.ok=false;
            m.easing=static_cast<ForceMove::Easing>(easing);
        }
        auto ignoreCollision=header[0]>=forceMoveCollisionSaveTag?r.word():0;
        if(ignoreCollision>1) r.ok=false;
        m.ignoreCollision=ignoreCollision!=0;
        // Old saves retain visual-only lift until their current motion ends.
        m.airborneHeight=header[0]>=forceMoveAirSaveTag?static_cast<int>(r.word()):0;
        auto nativeTracked=header[0]>=forceMoveAirSaveTag?r.word():0;
        if(nativeTracked>1) r.ok=false;
        m.nativeTracked=nativeTracked!=0;
        if(header[0]>=jumpSaveTag) {
            auto jump=r.word();if(jump>1) r.ok=false;
            if(jump) {
                ForceMove::Landing l{};
                l.startZ=static_cast<int>(r.word());l.endZ=static_cast<int>(r.word());l.level=static_cast<int>(r.word());
                l.deck=r.word();auto bridge=r.word(),crush=r.word();
                if(bridge>1 || crush>1 || !m.ignoreCollision || l.level<0 || l.level>255 ||
                   std::abs(int64_t(l.startZ))>ForceMove::mapExtent || std::abs(int64_t(l.endZ))>ForceMove::mapExtent) r.ok=false;
                if(header[0]>=jumpEffectSaveTag) {
                    l.effect.anim=r.str();l.effect.sound=r.str(2047);
                }
                if(header[0]>=ejectSaveTag) {
                    l.openHeight=static_cast<int>(r.word());
                    if(header[0]>=ejectSpeedSaveTag && l.openHeight) {
                        l.ejectSpeed=r.real();
                        if(!ForceMove::EjectSpec::validSpeed(l.ejectSpeed)) r.ok=false;
                    }
                    if(l.openHeight<0 || l.openHeight>128*256 || (l.openHeight && crush)) r.ok=false;
                }
                l.bridge=bridge!=0;l.crush=crush!=0;m.landing=std::move(l);
            }
        }
        constexpr int limit=ForceMove::mapExtent;
        if(m.x<0 || m.x>=limit || m.y<0 || m.y>=limit || m.dx< -limit || m.dx>limit ||
           m.dy< -limit || m.dy>limit || m.height<0 || m.height>limit || m.startHeight<0 || m.startHeight>4*limit ||
           m.airborneHeight<0 || m.airborneHeight>limit || m.duration<=0 || m.elapsed<0 || m.elapsed>=m.duration || blocked>1 || !moves.emplace(id,m).second) r.ok=false;
    }
    decltype(ForceMove::settling) settling;
    if(header[0]>=forceMoveTiltSaveTag && r.ok) for(auto n=r.word();r.ok && n--;) {
        auto id=r.word();
        ForceMove::SettlingTilt lean{static_cast<int>(r.word()),static_cast<int>(r.word()),r.word()};
        constexpr int limit=ForceMove::mapExtent;
        if((!lean.dx && !lean.dy) || lean.dx< -limit || lean.dx>limit || lean.dy< -limit || lean.dy>limit ||
           !settling.emplace(id,lean).second) r.ok=false;
    }
    ActionSequence::SavedState sequenceState;
    if(header[0]>=sequenceSaveTag && r.ok) sequenceState=ActionSequence::readSaved(r);
    decltype(Research::seenOffers) seen;
    if(header[0]>=seenOffersSaveTag && r.ok) for(auto n=r.word();n && r.ok;--n) {
        auto house=r.word(),count=r.word();
        if(house>=128 || count>100000 || seen.contains(house)) {r.ok=false;break;}
        auto& ids=seen[house];
        while(count-- && r.ok) {auto id=r.str();if(id.empty() || !ids.insert(id).second) r.ok=false;}
    }
    decltype(hitSequences) restoredHitSequences;
    decltype(observedHits) restoredObservedHits;
    if(header[0]>=unhitSaveTag && r.ok) {
        for(auto n=r.word();n && r.ok;--n) {auto id=r.word(),seq=r.word();if(!restoredHitSequences.emplace(id,seq).second) r.ok=false;}
        for(auto n=r.word();n && r.ok;--n) {auto id=r.word();auto skill=r.str();auto seq=r.word();if(skill.empty() || !restoredObservedHits.emplace(TimerKey{id,skill},seq).second) r.ok=false;}
    }
    decltype(LuaAnim::entries) restoredLuaAnimations;
    if(header[0]>=luaAnimSaveTag && r.ok) {
        auto count=r.word();if(count>LuaAnim::maxEntries)r.ok=false;
        for(DWORD i=0;i<count && r.ok;++i) {
            auto id=r.word(),owner=r.word(),cast=r.word();auto skill=r.str();auto remaining=r.word(),loop=r.word();int zOffset=static_cast<int>(r.word());
            CoordStruct start{static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
            CoordStruct finish{static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
            auto duration=r.word(),elapsed=r.word(),lastFrame=r.word();
            if(!id || (!owner && !cast) || skill.empty() || remaining>LuaAnim::maxDuration || loop>1 || zOffset< -64*256 || zOffset>64*256 || duration>LuaAnim::maxDuration || elapsed>duration ||
               !restoredLuaAnimations.emplace(id,LuaAnim::Entry{owner,cast,skill,remaining,loop!=0,zOffset,start,finish,duration,elapsed,lastFrame}).second)r.ok=false;
        }
    }
    decltype(passiveBulletOwners) restoredBulletOwners;
    if(header[0]>=bulletOwnerSaveTag && r.ok) for(auto n=r.word();n && r.ok;--n) {
        auto id=r.word();int owner=static_cast<int>(r.word());
        if(!id || owner<0 || owner>127 || !restoredBulletOwners.emplace(id,owner).second) r.ok=false;
    }
    decltype(cloakableUnits) restoredCloakables;
    if(header[0]>=cloakableSaveTag && r.ok) for(auto n=r.word();n && r.ok;--n) {
        auto id=r.word();
        if(!id || !restoredCloakables.insert(id).second) r.ok=false;
    }
    decltype(linkedGrants) restoredLinks;
    if(header[0]>=linkedSaveTag && r.ok) for(auto n=r.word();n && r.ok;--n) {
        auto unit=r.word(),count=r.word();
        if(!unit || !count || count>100000 || restoredLinks.contains(unit)) {r.ok=false;break;}
        auto& ids=restoredLinks[unit];
        while(count-- && r.ok) {auto id=r.str();if(id.empty() || !ids.insert(id).second) r.ok=false;}
    }
    decltype(groundPickups) restoredPickups;
    if(header[0]>=pickupSaveTag && r.ok) for(auto n=r.word();n && r.ok;--n) {
        auto x=r.word(),y=r.word();auto type=r.str();auto born=r.word();
        if(x>=512 || y>=512 || type.empty() || !restoredPickups.emplace(PickupCell{short(x),short(y)},GroundPickup{type,born}).second) r.ok=false;
    }
    decltype(Research::completedRounds) restoredResearchRounds;
    decltype(Research::raritySequenceSeed) restoredRaritySeed;
    if(header[0]>=researchRoundsSaveTag && r.ok) {
        restoredRaritySeed=r.word();
        auto count=r.word();
        if(count>128) r.ok=false;
        while(count-- && r.ok) {
            auto house=r.word(),round=r.word();
            auto owned=upgrades.find(house);
            if(house>=128 || !round || owned==upgrades.end() || (header[0]<replacedResearchSaveTag && round>owned->second.size()) ||
                !restoredResearchRounds.emplace(house,round).second) r.ok=false;
        }
    } else if(r.ok) {
        // Older saves did not distinguish ordinary purchases from free rewards.
        // Resume after the existing project count instead of restarting at round 1.
        for(const auto& [house,ids]:upgrades) if(!ids.empty())
            restoredResearchRounds.emplace(house,static_cast<unsigned>(ids.size()));
    }
    decltype(mapPassengersBySize) restoredCapacityModes;
    if(header[0]>=transportCapacitySaveTag && r.ok) for(auto n=r.word();n && r.ok;--n) {
        auto type=r.str();auto bySize=r.word();
        if(type.empty() || bySize>1 || !restoredCapacityModes.emplace(type,bySize!=0).second) r.ok=false;
    }
    decltype(bombSnapshots) restoredBombs;
    if(header[0]>=bombSnapshotSaveTag && r.ok) for(auto n=r.word();n && r.ok;--n) {
        auto id=r.word();BombSnapshot s;
        s.firepower=r.real();s.scale=static_cast<int>(r.word());s.owner=static_cast<int>(r.word());
        if(!id || s.firepower<0 || s.firepower>1e9 || s.scale<=0 || s.owner < -1 || s.owner>127) r.ok=false;
        for(auto count=r.word();count && r.ok;--count) r.str();
        if(!restoredBombs.emplace(id,std::move(s)).second) r.ok=false;
    }
    DWORD restoredLastTicket=0;
    decltype(Research::freeTickets) restoredTickets;
    if(header[0]>=freeResearchSaveTag && r.ok) {
        restoredLastTicket=r.word();
        auto count=r.word();
        if((!restoredLastTicket && header[0]<replacedResearchSaveTag) || count>restoredLastTicket) r.ok=false;
        while(r.ok && count--) {
            auto id=r.word(),house=r.word(),tier=r.word(),size=r.word();
            if(!id || id>restoredLastTicket || house>=128 || tier>Research::rarityCount || size>SHRT_MAX+1u) {r.ok=false;break;}
            Research::FreeResearchTicket ticket{static_cast<int>(house)};
            if(tier<Research::rarityCount) ticket.rarity=static_cast<Research::Rarity>(tier);
            std::set<string> seenIds;
            while(r.ok && size--) {
                auto key=r.str();
                if(key.empty() || !seenIds.insert(key).second) r.ok=false;
                else ticket.projects.push_back(std::move(key));
            }
            if(!restoredTickets.emplace(id,std::move(ticket)).second) r.ok=false;
        }
    }
    Research::Unlocks retired;
    if(header[0]>=replacedResearchSaveTag && r.ok) {
        auto count=r.word();
        if(count>128) r.ok=false;
        while(r.ok && count--) {
            int house=static_cast<int>(r.word());auto size=r.word();
            if(!upgrades.contains(house) || !size || size>10000 || retired.contains(house)) {r.ok=false;break;}
            auto& ids=retired[house];
            while(r.ok && size--) {
                auto id=r.str();
                if(id.empty() || upgrades.at(house).contains(id) || !ids.insert(id).second) r.ok=false;
            }
        }
        for(auto [house,round]:restoredResearchRounds)
            if(header[0]<repeatResearchSaveTag && round>upgrades.at(house).size()+(retired.contains(house)?retired.at(house).size():0)) r.ok=false;
    }
    decltype(UnitMount::links) mounts;
    std::set<std::pair<DWORD,string>> usedMountSlots;
    if(header[0]>=mountSaveTag) for(DWORD n=r.word();r.ok && n--;) {
        auto id=r.word();UnitMount::Link l;
        l.parentId=r.word();l.slot=r.str();
        l.offset={static_cast<int>(r.word()),static_cast<int>(r.word()),static_cast<int>(r.word())};
        auto turret=r.word(),fire=r.word();
        l.turret=turret!=0;l.transportFire=fire!=0;l.carrierId=r.word();l.orderId=r.word();
        for(DWORD k=r.word();r.ok && k--;) l.excluded.push_back(r.str());
        if(!l.parentId || (!l.slot.empty() && !usedMountSlots.emplace(l.parentId,l.slot).second) ||
           std::max({l.offset.X,l.offset.Y,l.offset.Z})>16384 || std::min({l.offset.X,l.offset.Y,l.offset.Z})< -16384) r.ok=false;
        if(!id || id==l.parentId || turret>1 || fire>1 || !mounts.emplace(id,std::move(l)).second) r.ok=false;
    }
    for(const auto& [id,l]:mounts) if(mounts.contains(l.parentId)) r.ok=false;
    decltype(splitTargets) restoredSplits;
    if(header[0]>=splitTargetsSaveTag) for(DWORD n=r.word();r.ok && n--;) {
        auto firer=r.word();
        SplitTargets shot{r.word(),static_cast<int>(r.word()),r.str(),{}};
        auto size=r.word();
        if(!firer || shot.slot<0 || shot.slot>=TechnoTypeClass::MaxWeapons || shot.weapon.empty() ||
           !size || size>(r.bytes.size()-r.pos)/sizeof(DWORD)) {r.ok=false;break;}
        std::set<DWORD> seen;
        while(size--) {
            auto id=r.word();
            if(id && (id==firer || id==shot.primary || !seen.insert(id).second)) {r.ok=false;break;}
            shot.branches.push_back(id);
        }
        if(seen.empty() || !restoredSplits.emplace(firer,std::move(shot)).second) r.ok=false;
    }
    decltype(Taunt::controls) restoredTaunts;
    if(header[0]>=tauntSaveTag) for(DWORD n=r.word();r.ok && n--;) {
        DWORD id=r.word();
        auto [it,fresh]=restoredTaunts.try_emplace(id);
        if(!id || !fresh) {r.ok=false;break;}
        auto& control=it->second;
        control.current=r.word();
        for(DWORD k=r.word();r.ok && k--;) {
            Taunt::Key key{r.str(),r.word()};
            Taunt::Binding binding{r.word(),{r.word(),static_cast<int>(r.word())}};
            if(key.first.empty() || !key.second || !binding.target || binding.target==id ||
               !binding.time.duration || binding.time.duration< -1 || !control.bindings.emplace(key,binding).second) r.ok=false;
        }
    }
    decltype(ForceMove::ejectOrders) ejectOrders;
    decltype(ForceMove::descending) descending;
    if(header[0]>=ejectSaveTag) {
        auto cell=[&] {auto x=r.word(),y=r.word();if(x>=512 || y>=512) r.ok=false;return CellStruct{short(x),short(y)};};
        for(auto n=r.word();r.ok && n--;) {
            auto id=r.word();ForceMove::EjectOrder q{};q.target=cell();q.nextFrame=r.word();
            q.spec.range=int(r.word());q.spec.openHeight=int(r.word());
            if(header[0]>=ejectSpeedSaveTag) q.spec.horizontalSpeed=r.real();
            else {
                // Older queues stored a fixed duration. Match it at the order's maximum range.
                auto frames=r.word();
                if(!frames || frames>3600) r.ok=false;
                else q.spec.horizontalSpeed=q.spec.range*15.0/frames;
            }
            q.spec.interval=int(r.word());
            auto count=r.word();if(!q.spec.valid() || !count || count>65535) r.ok=false;
            for(;r.ok && count--;) q.passengers.push_back(r.word());
            if(!ejectOrders.emplace(id,std::move(q)).second) r.ok=false;
        }
        for(auto n=r.word();r.ok && n--;) {
            auto id=r.word();if(!descending.emplace(id,cell()).second) r.ok=false;
        }
    }
    Research::CounterValues restoredCounters;
    if(header[0]>=researchCounterSaveTag) {
        for(auto n=r.word();r.ok && n--;) {
            auto house=r.word(), count=r.word();
            if(house>=128 || !count || restoredCounters.contains(house)) {r.ok=false;break;}
            auto& entries=restoredCounters[house];
            for(;r.ok && count--;) {
                auto key=r.str(255);
                auto dot=key.find('.');
                auto value=r.word();
                if(dot==string::npos || !dot || dot>127 || dot+1>=key.size() || key.size()-dot-1>127 ||
                    key.find('.',dot+1)!=string::npos || key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.")!=string::npos ||
                    !value || value>INT_MAX || !entries.emplace(key,value).second) r.ok=false;
            }
        }
    }
    Research::StageProgress restoredStages;
    if(header[0]>=researchStageSaveTag) {
        for(auto n=r.word();r.ok && n--;) {
            auto house=r.word();
            auto project=r.str(),stage=r.str();
            if(house>=128 || project.empty() || stage.empty()) {r.ok=false;break;}
            auto [it,added]=restoredStages.try_emplace({house,project,stage});
            if(!added) {r.ok=false;break;}
            for(auto count=r.word();r.ok && count--;) {
                auto id=r.str();
                if(id.empty() || !it->second.insert(id).second) r.ok=false;
            }
        }
    }
    Research::BondProgress restoredBonds;
    if(header[0]>=researchBondSaveTag) {
        for(auto n=r.word();r.ok && n--;) {
            auto house=r.word();auto bond=r.str(),tier=r.str();
            auto active=r.word(),count=r.word();
            if(house>=128 || bond.empty() || tier.empty() || active>1 || (!active && count)) {r.ok=false;break;}
            auto [it,fresh]=restoredBonds.try_emplace({house,bond,tier});
            if(!fresh) {r.ok=false;break;}
            it->second.active=active!=0;
            for(;r.ok && count--;) {
                auto id=r.str();
                if(id.empty() || !it->second.grants.insert(id).second) r.ok=false;
            }
        }
    }
    decltype(Research::repeatCounts) repeats;
    if(header[0]>=repeatResearchSaveTag) {
        std::map<int,uint64_t> extra;
        for(auto n=r.word();r.ok && n--;) {
            auto house=r.word();auto id=r.str();auto count=r.word();
            if(house>=128 || id.empty() || count<2 || count>INT_MAX ||
                ((!upgrades.contains(house) || !upgrades.at(house).contains(id)) &&
                 (!retired.contains(house) || !retired.at(house).contains(id))) ||
                !repeats.emplace(std::make_pair(static_cast<int>(house),id),count).second) r.ok=false;
            else extra[house]+=count-1;
        }
        for(auto [house,round]:restoredResearchRounds)
            if(round>extra[house]+upgrades.at(house).size()+(retired.contains(house)?retired.at(house).size():0)) r.ok=false;
    }
    if(!r.ok || r.pos!=data.size()) return fail("invalid or trailing payload fields");
    // Parsing above must not mutate live state, including sequence and signal IDs.
    reset();
    // Older saves allocated a blank reward for every eligible building.
    std::erase_if(rewards,[](const auto& entry){return !entry.second.claimed && entry.second.authorized<0;});
    Research::counterValues.swap(restoredCounters);
    Research::stageProgress.swap(restoredStages);
    Research::bondProgress.swap(restoredBonds);
    Research::repeatCounts.swap(repeats);
    recheckLoadedStages=!upgrades.empty();
    ForceMove::ejectOrders.swap(ejectOrders);ForceMove::descending.swap(descending);
    splitTargets.swap(restoredSplits);
    Taunt::controls.swap(restoredTaunts);Taunt::rebind=!Taunt::controls.empty();
    UnitMount::links.swap(mounts);Mount::rebind=!UnitMount::links.empty();
    Research::retired.swap(retired);
    Research::lastTicket=restoredLastTicket;Research::freeTickets.swap(restoredTickets);
    bombSnapshots.swap(restoredBombs);
    rebindBombKeys=!bombSnapshots.empty();
    mapPassengersBySize.swap(restoredCapacityModes);
    Research::completedRounds.swap(restoredResearchRounds);
    Research::raritySequenceSeed=restoredRaritySeed;
    passiveBulletOwners.swap(restoredBulletOwners);
    cloakableUnits.swap(restoredCloakables);
    groundPickups.swap(restoredPickups);
    linkedGrants.swap(restoredLinks);
    nextSignalChain=restoredNextSignalChain;
    sequenceState.commit();
    if(ready) ArtSp::TipsLabel::readRules();
    LuaAnim::entries.swap(restoredLuaAnimations);
    if(r.ok && r.pos==data.size()) {hitSequences.swap(restoredHitSequences);observedHits.swap(restoredObservedHits);Research::seenOffers.swap(seen);}
    if(r.ok && r.pos==data.size()) {
        ForceMove::settling.swap(settling);
        ForceMove::motions.swap(moves);
        mapPassiveBindings.swap(restoredBindings);
        scriptTimers=std::move(restoredTimers);
        auraSources.swap(restoredAuras);auraLosses.swap(restoredLosses);discoverAuras=discoverAfterLoad;
        if(!auraSources.empty()) {
            for(auto unit:TechnoClass::Array) if(unit) {
                auto it=auraSources.lower_bound({unit->UniqueID,{}});
                while(it!=auraSources.end() && it->first.first==unit->UniqueID) {
                    it->second.unit=unit;
                    ++it;
                }
            }
            for(const auto& entry:auraSources) for(auto target:entry.second.targets)
                for(const auto& id:entry.second.grants) ++auraGrants[target][id];
        }
    }
    if(r.ok && r.pos==data.size()) {triggerTotals.swap(restoredTotals);mindImmuneUnits.swap(restoredMindImmunes);volleyWeapons.swap(restoredVolleys);passiveStacks.swap(restoredStacks);}
    if(r.ok && r.pos==data.size()) { spawnGrants.swap(restoredSpawns);VisualScale::explosions.swap(explosions);Research::clearOfferCache();gainSeen.swap(acquired);UnitHealth::pools.swap(healthPools);Research::globalIncome.initialized=incomeInitialized!=0;Research::globalIncome.lastFrame=incomeLast; Research::refreshedSlots.swap(refreshes); immuneUnits.swap(immunes);fireRateBase.swap(fireRates);rangeBase.swap(weaponRanges);projectileBase.swap(launchSizes);VisualScale::projectiles.swap(shotSizes);warheadGrants.swap(grants); rebindGrants=!warheadGrants.empty(); mirageUnits.swap(mirages); VisualScale::base.swap(sizes); Research::buildingRewards.swap(rewards); attachedAnims.swap(attachments); activeAnimStates.swap(animStates); Research::unlockOrder.swap(order); nearbySamples.swap(samples); periodic.swap(clocks); previousState.swap(statesBefore);reportedDeaths.swap(deaths);removedPassives.swap(removed);passiveBullets.swap(bullets);stateTimers.swap(timers); applied.swap(states); Research::points.swap(points); Research::producers.swap(producers); Research::savedOffers.swap(offers); counts.swap(restored); pending.swap(events); Research::unlocked.swap(upgrades); }
    if(r.ok && r.pos==data.size()) {
        VisualScale::attachedAnims.clear();
        for(const auto& [key,anim]:attachedAnims) VisualScale::attachedAnims[anim.id]=key.first;
    }
    return true;
}
}
// gamemd 1.001: EAX is the open IStorage immediately before CreateStream("CONTENTS").
DEFINE_HOOK(0x67D074, SF_SaveStorage, 6) {
    auto storage=R->EAX<IStorage*>();
    IStream* stream=nullptr;
    if(FAILED(storage->CreateStream(stateStreamName,STGM_CREATE|STGM_WRITE|STGM_SHARE_EXCLUSIVE,0,0,&stream)))
        return 0x67D08F;
    const bool ok=saveState(stream) && SUCCEEDED(stream->Commit(STGC_DEFAULT));
    stream->Release();
    return ok && saveUnitOrders(storage) && saveFlightState(storage) && ActSp::save(storage) ? 0 : 0x67D08F;
}

// Object load and scenario resets have finished; the source IStorage is still open.
DEFINE_HOOK(0x67E67B, SF_LoadStorage, 5) {
    auto storage=R->Stack<IStorage*>(0x18);
    IStream* stream=nullptr;
    const HRESULT opened=storage?storage->OpenStream(stateStreamName,nullptr,STGM_READ|STGM_SHARE_EXCLUSIVE,0,&stream):E_POINTER;
    bool valid=true;
    if(SUCCEEDED(opened)) {
        valid=loadState(stream);
        stream->Release();
        // Per-building multipliers are derived from the restored passives and timers.
        for(auto house:HouseClass::Array) if(house) house->RecheckPower=true;
    } else if(opened==STG_E_FILENOTFOUND) {
        // Saves made before Starfall was installed legitimately have no stream.
        reset();
        Research::globalIncome.initialized=true;
        Research::globalIncome.lastFrame=static_cast<unsigned>(Unsorted::CurrentFrame);
    } else {
        std::ofstream("Starfall.log",std::ios::app)<<"Starfall: cannot open saved state, HRESULT "<<std::hex<<opened<<"\n";
        valid=false;
    }
    if(!valid || !ActSp::load(storage) || !loadFlightState(storage) || !loadUnitOrders(storage)) {
        // Native objects have already been loaded. Continuing or returning to the
        // old scenario here would leave engine and extension state inconsistent.
        MessageBoxW(nullptr,L"流火或特殊行为子模块的存档数据损坏或版本不受支持，无法继续载入。\n游戏将关闭；请使用匹配的版本或其他存档。",
            L"Starfall：读档失败",MB_OK|MB_ICONERROR);
        ExitProcess(EXIT_FAILURE);
    }
    return 0;
}
