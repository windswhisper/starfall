void readPercentLimits();
struct PassiveGrant {
    vector<string> ids;
    int duration=-1;
    bool stackOnReapply=false;
};
struct WarheadGrantRule : PassiveGrant {
    vector<string> houses;
    vector<string> types{"*"};
};
struct WarheadHouses {bool owner,enemies,neutral;};
std::map<WarheadTypeClass*,WarheadHouses> warheadHouses;
bool warheadAllows(WarheadTypeClass* wh,HouseClass* source,HouseClass* target) {
    const auto& flags=warheadHouses.at(wh);
    if(!target || (target->IsNeutral() && !flags.neutral)) return false;
    if(!source) return true;
    if(source==target) return flags.owner;
    return source->IsAlliedWith(target)?wh->AffectsAllies:flags.enemies;
}
struct GrantPreset : PassiveGrant {
    vector<TechnoTypeClass*> targetTypes;
};
std::map<int,GrantPreset> grantPresets;
std::map<WarheadTypeClass*,WarheadGrantRule> grantRules;
bool readGrant(PassiveGrant& grant,const string& section,const char* listKey,const string& list,const char* durationKey) {
    grant.ids=split(list);
    if(grant.ids.empty() || std::any_of(grant.ids.begin(),grant.ids.end(),[](const string& id){return !skills.contains(id);}))
        return invalidField(section,string(listKey)+"="+list);
    auto duration=read(section,durationKey,"-1");
    if(duration!="-1" && (grant.duration=number(duration))<=0)
        return invalidField(section,string(durationKey)+"="+duration);
    return true;
}
void readGrantPresets() {
    const string section="Starfall.PassiveGrants";
    for(const auto& key:sectionKeys(section)) {
        int index=number(key);
        if(index<0 || key!=std::to_string(index)) {invalidField(section,key);continue;}
        auto id=read(section,key.c_str());
        if(id.empty()) {invalidField(section,key+" (missing preset)");continue;}
        GrantPreset preset;
        if(!readGrant(preset,id,"Passives",read(id,"Passives"),"Duration")) continue;
        auto types=split(read(id,"TargetTypes"));
        for(const auto& name:types) preset.targetTypes.push_back(TechnoTypeClass::Find(name.c_str()));
        if(std::find(preset.targetTypes.begin(),preset.targetTypes.end(),nullptr)!=preset.targetTypes.end()) {
            invalidField(id,"TargetTypes");continue;
        }
        grantPresets.emplace(index,std::move(preset));
    }
}
void readGrantRules() {
    for(auto wh:WarheadTypeClass::Array) {
        auto section=ActionSequence::mapFields.find(lower(wh->ID));
        auto flag=[&](const char* key,bool fallback) {
            if(section!=ActionSequence::mapFields.end()) {
                auto it=section->second.find(lower(key));
                if(it!=section->second.end()) {
                    auto value=lower(it->second);
                    if(includes({"yes","true","1"},value)) return true;
                    if(includes({"no","false","0"},value)) return false;
                }
            }
            auto ini=CCINIClass::INI_Rules;
            return ini?ini->ReadBool(wh->ID,key,fallback):fallback;
        };
        warheadHouses[wh]={flag("AffectsOwner",wh->AffectsAllies),flag("AffectsEnemies",true),flag("AffectsNeutral",true)};
        auto list=read(wh->ID,"Starfall.GrantPassives");
        if(list.empty()) continue;
        WarheadGrantRule rule;
        if(!readGrant(rule,wh->ID,"Starfall.GrantPassives",list,"Starfall.GrantPassives.Duration")) continue;
        auto reapply=read(wh->ID,"Starfall.GrantPassives.StackOnReapply","no");
        if(reapply!="yes" && reapply!="no") {invalidField(wh->ID,"Starfall.GrantPassives.StackOnReapply="+reapply);continue;}
        rule.stackOnReapply=reapply=="yes";
        rule.houses=split(read(wh->ID,"Starfall.GrantPassives.Houses","All"));
        if(rule.houses.empty() || std::any_of(rule.houses.begin(),rule.houses.end(),[](const string& h){
            return !includes({"all","self","allies","enemies","neutral"},h);
        })) {invalidField(wh->ID,"Starfall.GrantPassives.Houses");continue;}
        rule.types=split(read(wh->ID,"Starfall.GrantPassives.TargetTypes","*"));
        if(rule.types.empty() || std::any_of(rule.types.begin(),rule.types.end(),[](const string& type) {
            return type!="*" && !Research::isTargetCategory(type) && !TechnoTypeClass::Find(type.c_str());
        })) {invalidField(wh->ID,"Starfall.GrantPassives.TargetTypes");continue;}
        grantRules.emplace(wh,std::move(rule));
    }
}
bool readActive(const string& id,ActiveSkills::Spec& spec) {
    using K=ActiveSkills::Kind;
    auto kind=read(id,"Active.Target","None"),mode=read(id,"Active.CastMode","Single");
    if(!includes({"none","unit","point","area","direction"},kind)) return invalidField(id,"Active.Target");
    if(!includes({"single","all"},mode)) return invalidField(id,"Active.CastMode");
    auto interrupt=read(id,"Active.InterruptOrders","yes");
    if(!includes({"yes","no"},interrupt)) return invalidField(id,"Active.InterruptOrders");
    spec.interruptOrders=interrupt=="yes";
    spec.id=id;spec.all=mode=="all";
    spec.name=displayText(readRaw(id,"Active.Name",id.c_str()));spec.icon=readRaw(id,"Active.Icon");
    spec.indicatorTexture=readRaw(id,"Active.Indicator.Texture");
    spec.arrowTexture=readRaw(id,"Active.Indicator.Arrow.Texture");
    auto width=read(id,"Active.Indicator.Arrow.Width","0.75");
    auto parsedWidth=std::from_chars(width.data(),width.data()+width.size(),spec.arrowWidth);
    if(parsedWidth.ec!=std::errc{} || parsedWidth.ptr!=width.data()+width.size() ||
       !std::isfinite(spec.arrowWidth) || spec.arrowWidth<0.1 || spec.arrowWidth>8)
        return invalidField(id,"Active.Indicator.Arrow.Width");
    spec.arrowHead=number(read(id,"Active.Indicator.Arrow.Head","25"));
    spec.arrowTail=number(read(id,"Active.Indicator.Arrow.Tail","37"));
    spec.indicatorOpacity=number(read(id,"Active.Indicator.Opacity","90"));
    if(spec.arrowHead<1 || spec.arrowHead>98 || spec.arrowTail<1 || spec.arrowTail>98 || spec.arrowHead+spec.arrowTail>=100)
        return invalidField(id,"Active.Indicator.Arrow.Head/Tail");
    if(spec.indicatorOpacity<0 || spec.indicatorOpacity>100) return invalidField(id,"Active.Indicator.Opacity");
    spec.kind=kind=="unit"?K::Unit:kind=="area"?K::Area:kind=="direction"?K::Direction:kind=="point"?K::Point:K::None;
    spec.range=number(read(id,"Active.Range","0"));spec.radius=number(read(id,"Active.Radius","0"));
    spec.distance=number(read(id,"Active.Distance","0"));
    if(spec.range<0 || spec.range>128 || ((spec.kind==K::None || spec.kind==K::Direction) && spec.range)) return invalidField(id,"Active.Range");
    if(spec.radius<0 || spec.radius>128 || (spec.kind==K::Area)!=(spec.radius>0)) return invalidField(id,"Active.Radius");
    if(spec.distance<0 || spec.distance>128 || (spec.kind==K::Direction)!=(spec.distance>0)) return invalidField(id,"Active.Distance");
    auto shape=read(id,"Active.Shape");
    if(!shape.empty() && (spec.kind!=K::Area || shape!="circle")) return invalidField(id,"Active.Shape");
    spec.houses=split(read(id,"Active.Houses"));
    if((!spec.houses.empty() && spec.kind!=K::Unit) || std::any_of(spec.houses.begin(),spec.houses.end(),
        [](const auto& h){return !includes({"all","self","allies","enemies","neutral"},h);})) return invalidField(id,"Active.Houses");
    return true;
}
void discoverScripts(std::set<string>& ids) {
    ScriptRuntime::clearPool();
    namespace fs=std::filesystem;
    std::error_code ec;
    vector<fs::path> paths;
    for(fs::directory_iterator it(".",ec),end;!ec && it!=end;it.increment(ec)) {
        if(it->is_regular_file(ec) && !_wcsicmp(it->path().extension().c_str(),L".lua")) paths.push_back(it->path());
    }
    std::sort(paths.begin(),paths.end());
    const auto iniIds=ids;
    for(const auto& path:paths) {
        ScriptRuntime::Module m;
        auto filename=path.filename().u8string();
        m.file.assign(filename.begin(),filename.end());
        std::ifstream stream(path,std::ios::binary|std::ios::ate);
        auto size=stream.tellg();
        if(size<0 || size>1024*1024) {configLog("Lua: cannot read "+m.file+" (1 MiB limit)");continue;}
        string source(static_cast<size_t>(size),'\0'),error;
        stream.seekg(0);
        if(!stream.read(source.data(),size)) {configLog("Lua: read failed "+m.file);continue;}
        if(source.starts_with("\xEF\xBB\xBF")) source.erase(0,3);
        ScriptRuntime::Call call{&m,&source,nullptr,nullptr,nullptr};
        if(!ScriptRuntime::run(call,error)) {configLog("Lua "+m.file+": "+error);continue;}
        auto file=lower(m.file),id=lower(trim(m.id)),trigger=lower(m.trigger);
        // An explicitly registered INI definition takes precedence over legacy Lua metadata.
        if(!id.empty() && !iniIds.contains(id)) {
            if(ids.contains(id)) {configLog("Lua "+m.file+": duplicate skill ID: "+id);continue;}
            if(id.size()>127 || trigger.empty()) {configLog("Lua "+m.file+": invalid skill ID or missing trigger: "+id);continue;}
            bool active=trigger=="active";
            ids.insert(id);
            auto& f=scriptDefinitions[id];
            f={{"trigger",trigger},{"action","runscript"},{"action.script",file},
               {"action.function",lower(m.callback.empty()?(active?"onCast":m.trigger):m.callback)},{"cooldown",std::to_string(m.cooldown)}};
            if(active) {
                if(!m.name.empty()) f["active.name"]=m.name;
                f["active.icon"]=m.icon;
                if(!m.castMode.empty()) f["active.castmode"]=m.castMode;
                if(!m.targetKind.empty()) f["active.target"]=m.targetKind;
                f["active.shape"]=m.shape;f["active.houses"]=m.houses;
                f["active.range"]=std::to_string(m.castRange);f["active.radius"]=std::to_string(m.radius);
                f["active.distance"]=std::to_string(m.distance);
            }
            if(m.interval) f["interval"]=std::to_string(m.interval);
            if(!m.signal.empty()) f["signal"]=lower(m.signal);
            for(const auto& unit:split(m.units)) scriptBindings[unit].push_back(id);
        }
        scriptModules.emplace(file,std::move(m));
    }
}
void rejectLinkedCycles() {
    vector<string> path;
    std::set<string> checked,invalid;
    std::map<string,size_t> visiting;
    std::function<void(const string&)> visit=[&](const string& id) {
        if(auto it=visiting.find(id);it!=visiting.end()) {
            string chain;
            for(size_t i=it->second;i<path.size();++i) {
                invalid.insert(path[i]);chain+=path[i]+" -> ";
            }
            invalidField(id,"Modifier.LinkedPassives cycle: "+chain+id);
            return;
        }
        if(!checked.insert(id).second) return;
        visiting.emplace(id,path.size());path.push_back(id);
        for(const auto& child:skills.at(id).linkedPassives) if(skills.contains(child)) visit(child);
        path.pop_back();visiting.erase(id);
    };
    for(const auto& [id,skill]:skills) if(!skill.linkedPassives.empty()) visit(id);
    for(const auto& id:invalid) skills.erase(id);
}
void readWeaponScripts() {
    for(auto weapon:WeaponTypeClass::Array) {
        const auto overlay=ActionSequence::mapFields.find(lower(weapon->ID));
        auto setting=[&](const char* key,const char* fallback="") {
            if(overlay!=ActionSequence::mapFields.end()) {
                auto it=overlay->second.find(lower(key));
                if(it!=overlay->second.end()) return it->second;
            }
            return readRaw(weapon->ID,key,fallback);
        };
        auto file=setting("Starfall.FireScript");
        if(file.empty()) continue;
        if(weapon->Suicide || weapon->Spawner || weapon->LimboLaunch || weapon->DiskLaser) {
            invalidField(weapon->ID,"Starfall.FireScript requires a standard firing weapon");
            continue;
        }
        Skill script{};
        script.id="@weapon:"+lower(weapon->ID);
        script.trigger="onweaponfire";
        ScriptFields fields{{"action","runscript"},{"action.script",file},{"action.function",setting("Starfall.FireFunction","onFire")}};
        if(readAction(script,weapon->ID,&fields)) {
            auto it=weaponScripts.insert_or_assign(script.id, std::move(script)).first;
            weaponFireScripts[weapon]=&it->second;
        }
    }
}
void loadPassiveDefinitions() {
    installRangeGetter<UnitClass>();installRangeGetter<InfantryClass>();
    ForceMove::installTargeting();
    installRangeGetter<AircraftClass>();installRangeGetter<BuildingClass>();
    readPercentLimits();
    std::set<string> ids;
    if (CCINIClass* ini = CCINIClass::INI_Rules) {
        for (int i=0; i<ini->GetKeyCount(registry); ++i) {
            char id[128]{};
            ini->ReadString(registry,ini->GetKeyName(registry,i),"",id,sizeof(id));
            ids.insert(lower(trim(id)));
        }
    }
    char keys[32768]{};
    GetPrivateProfileStringA(registry,nullptr,"",keys,sizeof(keys),configPath);
    for (char* k=keys; *k; k+=strlen(k)+1) ids.insert(read(registry,k));
    rulesSkillNames.clear();
    if(auto ini=CCINIClass::INI_Rules)
        for(auto node=ini->Sections.First();node && node->IsValid();node=node->Next())
            if(node->Name && ids.contains(lower(node->Name))) rulesSkillNames.emplace(lower(node->Name),node->Name);
    const string pickupRegistry="Starfall.PickupTypes";
    for(const auto& key:sectionKeys(pickupRegistry)) {
        auto id=read(pickupRegistry,key.c_str());
        if(!id.empty()) pickupTypes.try_emplace(id);
    }
    if(!pickupTypes.empty()) if(auto ini=CCINIClass::INI_Rules) {
        // Reward lists may reference arbitrary mixed-case rules sections.
        for(auto node=ini->Sections.First();node && node->IsValid();node=node->Next())
            if(node->Name) rulesSkillNames.try_emplace(lower(node->Name),node->Name);
    }
    UnitOrders::readDefinitions();
    discoverScripts(ids);
    ActiveSkills::specs.clear();
    for (auto& id : ids) {
        if (id.empty() || id.size()>127) continue;
        auto def=scriptDefinitions.find(id);
        parsingScript=def==scriptDefinitions.end()?nullptr:&*def;
        struct EndDefinition { ~EndDefinition(){parsingScript=nullptr;} } endDefinition;
        Skill s{};
        s.id=id; s.trigger=read(id,"Trigger");
        s.signal=read(id,"Signal");
        s.senderHouses=split(read(id,"Filter.SenderHouse","All"));
        s.senderKinds=split(read(id,"Filter.SenderKind","All"));
        s.houses=split(read(id,"Filter.TargetHouse","All"));
        s.kinds=split(read(id,"Filter.TargetKind","All"));
        for(const auto& value:s.houses) if(!includes({"all","self","allies","enemies","neutral","none"},value))
            configLog("["+id+"] Filter.TargetHouse="+value+" - 未知的所属方筛选值，不会匹配");
        for(const auto& value:s.kinds) if(!includes({"all","infantry","vehicle","aircraft","building","other","none"},value))
            configLog("["+id+"] Filter.TargetKind="+value+" - 未知的单位类别，不会匹配");
        const bool active=s.trigger=="active";
        s.count=number(read(id,"Condition.Count","1"));
        s.cooldown=number(read(id,"Cooldown",active && includes({"jumpto","ejectpassengers"},read(id,"Action"))?"120":"0"));
        s.chance=number(read(id,"Chance","100%"),true);
        s.minDamage=number(read(id,"Condition.MinDamage","0"));
        s.maxTriggers=number(read(id,"MaxTriggers","0"));
        auto interval=read(id,"Interval");
        if(s.trigger=="oninterval" || !interval.empty()) {
            s.interval=number(interval);
            if(s.trigger!="oninterval" || s.interval<=0) {invalidField(id,"Interval="+interval);continue;}
        }
        auto fields=sectionKeys(id);
        if(s.trigger=="oncast") {invalidField(id,"Trigger="+s.trigger+" (use Active)");continue;}
        if(active) s.trigger="oncast";
        for(const auto& key:fields) if(key.starts_with("script.param.") && key.size()>13)
            s.scriptParams.emplace(key.substr(13),readRaw(id,key.c_str()));
        auto stackMax=read(id,"Stack.Max");
        if(!stackMax.empty() && (s.stackMax=number(stackMax))<=0) {invalidField(id,"Stack.Max="+stackMax);continue;}
        if(!readAnimations(s,fields)) continue;
        if(s.trigger.empty() && (s.anim || s.tint || s.gainAnim) && std::all_of(fields.begin(),fields.end(),[](const string& key){return key=="anim" || key=="anim.always" || key=="gainanim" || key=="tint.color" || key=="tint.strength" || key=="stack.max";})) {
            skills.emplace(id,s); anyPassiveVisuals=true; continue;
        }
        if(!parseState(s,fields)) continue;
        if(s.conditionFormula.expression.usesLiveState() && s.trigger!="whilestate") {invalidField(id,"Live state formula variables require WhileState");continue;}
        if(s.conditionFormula.expression.usesSignal() && s.trigger!="onsignal") {invalidField(id,"Sender and Event.SignalValue require OnSignal");continue;}
        needMotion|=(s.requiredMask&16)!=0;
        needUnitPower|=(s.requiredMask&2)!=0;
        if(s.trigger=="whilestate") {
            skills.emplace(id,s); anyStateSkills=true;
            anyPassiveVisuals|=s.anim || s.tint.has_value();
            ProjectileShield::configured|=s.shield.radius>0;
            continue;
        }
        if(s.count<=0 || s.cooldown<0 || s.chance<0 || s.chance>100 || s.minDamage<0 || s.maxTriggers<0) {
            for(auto [key,invalid]:{std::pair{"Condition.Count",s.count<=0},{"Chance",s.chance<0 || s.chance>100},
                 {"Cooldown",s.cooldown<0},{"MaxTriggers",s.maxTriggers<0},{"Condition.MinDamage",s.minDamage<0}})
                if(invalid) invalidField(id,string(key)+"="+read(id,key));
            continue;
        }
        if(!includes({"onkill","ondealdamage","ontakedamage","onbeforedealdamage","onbeforetakedamage","ondeath","onfire","onbeforefire","ondeploy","onpromote","onpromoteveteran","onpromoteelite","oninterval","onpassivegained","onbuilt","onbuildingupgrade","onpassengerconsumed","onsignal","oncast"},s.trigger)) {
            invalidField(id,"Trigger="+s.trigger);continue;
        }
        if(s.trigger=="onsignal" && (s.signal.empty() || s.signal.size()>127)) {invalidField(id,"Signal");continue;}
        if(s.trigger!="onsignal" && !s.signal.empty()) {invalidField(id,"Signal requires OnSignal");continue;}
        if(s.trigger!="onsignal" && (fields.contains("filter.senderhouse") || fields.contains("filter.senderkind"))) {invalidField(id,"Filter.Sender* requires OnSignal");continue;}
        if(std::any_of(s.senderHouses.begin(),s.senderHouses.end(),[](const auto& x){return !includes({"all","self","allies","enemies","neutral","none"},x);}) ||
           std::any_of(s.senderKinds.begin(),s.senderKinds.end(),[](const auto& x){return !includes({"all","infantry","vehicle","aircraft","building","other","none"},x);})) {invalidField(id,"Filter.Sender*");continue;}
        bool hasAction=fields.contains("action") || fields.contains("actions") || fields.contains("randomactions") || fields.contains("randomactions.weights") || fields.contains("randomactions.count") ||
            std::any_of(fields.begin(),fields.end(),[](const string& key){return key.starts_with("action.");});
        if(s.trigger=="onbeforefire" && (fields.contains("actions") || s.count!=1 || s.chance!=100 || s.cooldown || s.maxTriggers || s.minDamage || s.triggerAnim)) {
            invalidField(id,"OnBeforeFire requires one deterministic ReplaceWeapon action");continue;
        }
        if((hasAction || !s.triggerAnim) && !readActions(s,fields)) continue;
        if(s.trigger=="onbeforefire" && (s.actions.size()!=1 || s.actions[0].action!="replaceweapon")) {
            invalidField(id,"OnBeforeFire requires ReplaceWeapon");continue;
        }
        if(beforeHealthLoss(s.trigger) && (s.actions.empty() || std::any_of(s.actions.begin(),s.actions.end(),[](const auto& action) {
            return action.action!="setdamage" && action.action!="blockdamage" && action.action!="runscript";
        }))) {
            invalidField(id,s.trigger+" requires SetDamage, BlockDamage or RunScript");continue;
        }
        if(active) {
            auto unsupported=std::find_if(fields.begin(),fields.end(),[](const auto& k){
                return k!="tint.color" && k!="tint.strength" && k!="gainanim" && !activeField(k) && !k.starts_with("script.param.") && !includes({"trigger","cooldown","action","action.order","action.script","action.function","action.jump.height","action.jump.duration","action.jump.takeoffanim","action.jump.takeoffsound","action.jump.landanim","action.jump.landsound","action.eject.openheight","action.eject.horizontalspeed","action.eject.interval"},k);
            });
            if(unsupported!=fields.end()) {invalidField(id,*unsupported+" is unavailable for Trigger=Active");continue;}
            if(s.actions.size()!=1 || !includes({"runscript","jumpto","ejectpassengers","startunitorder","cancelunitorder"},s.actions.front().action)) {invalidField(id,"Trigger=Active requires RunScript, JumpTo, EjectPassengers, StartUnitOrder or CancelUnitOrder");continue;}
            ActiveSkills::Spec spec;
            if(!readActive(id,spec)) continue;
            const auto& action=s.actions.front();
            if(!action.order.empty() && !UnitOrders::registered(action.order)) {invalidField(id,"unknown Action.Order="+action.order);continue;}
            if(action.action=="startunitorder" && spec.kind!=ActiveSkills::Kind::Area) {invalidField(id,"StartUnitOrder requires Active.Target=Area");continue;}
            if(action.action=="startunitorder")
                spec.orderCursor=UnitOrders::isAutoEnter(action.order)?MouseCursorType::Enter:MouseCursorType::Attack;
            if(action.action=="cancelunitorder" && spec.kind!=ActiveSkills::Kind::None) {invalidField(id,"CancelUnitOrder requires Active.Target=None");continue;}
            if(s.actions.front().action=="jumpto") {
                if(spec.kind!=ActiveSkills::Kind::Point) {invalidField(id,"JumpTo requires Active.Target=Point");continue;}
                if(read(id,"Active.Range").empty()) spec.range=6;
                if(spec.range<=0) {invalidField(id,"JumpTo requires Active.Range in 1..128");continue;}
                spec.jump=s.actions.front().jump;spec.jump->range=spec.range;
            }
            if(s.actions.front().action=="ejectpassengers") {
                if(spec.kind!=ActiveSkills::Kind::Point) {invalidField(id,"EjectPassengers requires Active.Target=Point");continue;}
                if(read(id,"Active.Range").empty()) spec.range=8;
                if(spec.range<=0) {invalidField(id,"EjectPassengers requires Active.Range in 1..128");continue;}
                spec.eject=s.actions.front().eject;spec.eject->range=spec.range;
            }
            if(ActiveSkills::specs.size()>=65536) {invalidField(id,"active skill limit reached");continue;}
            ActiveSkills::specs.push_back(std::move(spec));
        }
        skills.emplace(id,s);
        anyPassiveVisuals|=s.anim || s.tint.has_value();
        anyIntervals|=s.trigger=="oninterval";
        observePromotions|=s.trigger=="onpromote" || s.trigger=="onpromoteveteran" || s.trigger=="onpromoteelite";
        observeDeployments|=s.trigger=="ondeploy";
    }
    // Resolve forward references after all skills are parsed, including dependencies of rejected grants.
    for(auto it=pickupTypes.begin();it!=pickupTypes.end();) {
        auto& type=it->second;
        const string& id=it->first;
        type.reward.id=id;type.reward.trigger="onpickup";
        auto keys=sectionKeys(id);
        auto life=read(id,"Lifetime","-1");
        auto end=life.data()+life.size();
        auto parsed=std::from_chars(life.data(),end,type.lifetime);
        bool valid=parsed.ec==std::errc{} && parsed.ptr==end && (type.lifetime==-1 || type.lifetime>0);
        if(!valid) invalidField(id,"Lifetime requires -1 or a positive frame count");
        type.image=read(id,"Image");
        for(const auto& key:keys) {
            if(key.starts_with("script.param.")) type.reward.scriptParams[key.substr(13)]=readRaw(id,key.c_str());
            else if(!actionField(key) && !includes({"image","lifetime","actions","randomactions","randomactions.count","randomactions.weights"},key))
                valid=invalidField(id,key);
        }
        if(valid && readActions(type.reward,keys)) {
            type.reward.id="@pickup:"+id;
            for(auto& action:type.reward.actions) if(action.action=="runscript") action.skillId=type.reward.id;
            ++it;
        }
        else it=pickupTypes.erase(it);
    }
    auto validActionReferences=[](const Skill& s) {
        for(const auto& a:s.actions) {
            for(const auto& id:a.removePickupTypes)
                if(!pickupTypes.contains(id)) return invalidField(s.id,"Action.PickupTypes="+id);
            if(a.action=="spawnpickup" && !pickupTypes.contains(a.pickupType)) return invalidField(s.id,"Action.PickupType="+a.pickupType);
            if((a.action=="grantpassive" || a.action=="changepassivestack" || a.action=="resetstatetimer") && !skills.contains(a.passive))
                return invalidField(s.id,"Action.Passive="+a.passive);
            if(a.action=="stealpassive") for(const auto& id:a.passiveList)
                if(!skills.contains(id)) return invalidField(s.id,"Action.Passive="+id);
        }
        return true;
    };
    rejectLinkedCycles();
    auto invalidSkill=[&](const auto& entry) {
        if(!validActionReferences(entry.second)) return true;
        for(const auto& id:entry.second.linkedPassives) if(!skills.contains(id)) {
            invalidField(entry.first,"Modifier.LinkedPassives="+id);return true;
        }
        for(const auto& id:entry.second.aura.passives) {
            auto child=skills.find(id);
            if(child==skills.end() || child->second.aura.range) {
                invalidField(entry.first,"Modifier.Aura.Passives="+id+" (requires a registered non-aura passive)");return true;
            }
        }
        for(const auto& id:entry.second.spawnPassives) if(!skills.contains(id)) {
            invalidField(entry.first,"Modifier.SpawnPassives="+id);return true;
        }
        for(const auto& id:entry.second.transportPassives) {
            auto child=skills.find(id);
            if(child==skills.end() || !child->second.transportPassives.empty()) {
                invalidField(entry.first,"Modifier.TransportPassives="+id+" (requires a registered non-transport-support passive)");return true;
            }
        }
        return false;
    };
    size_t removed;
    do {
        removed=std::erase_if(skills,invalidSkill);
        removed+=std::erase_if(pickupTypes,[&](const auto& entry){return !validActionReferences(entry.second.reward);});
    } while(removed);
    anyLinkedPassives=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return !entry.second.linkedPassives.empty();});
    anyAuras=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return entry.second.aura.range!=0;});
    anySpawnPassives=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return !entry.second.spawnPassives.empty();});
    anyTransportPassives=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return !entry.second.transportPassives.empty();});
    anyMultiShot=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return entry.second.hasMultiShot();});
    anyGainSkills=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return entry.second.trigger=="onpassivegained" || entry.second.gainAnim;});
    anyBeforeDealDamage=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return entry.second.trigger=="onbeforedealdamage";});
    anyBeforeTakeDamage=std::any_of(skills.begin(),skills.end(),[](const auto& entry){return entry.second.trigger=="onbeforetakedamage";});
    readWeaponScripts();
    FireOrigin::readRules();
    ArtSp::Tracers::readRules();
    ArtSp::TipsLabel::readRules();
    ArtSp::ContinuousBeam::readRules();
    readGrantRules();
    readGrantPresets();
}
void readSkills() {
    GetFullPathNameA("Starfall.ini",MAX_PATH,configPath,nullptr);
    Research::readConfiguration(configPath,loadPassiveDefinitions);
    ActionSequence::load();
    ready=true;
}
