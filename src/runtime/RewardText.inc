namespace RewardText {
struct Label {
    CoordStruct position;
    unsigned frame;
    bool research;
    std::wstring text;
};
std::deque<Label> labels;
constexpr unsigned lifetime=45;

void expire(unsigned frame) {
    while(!labels.empty() && frame-labels.front().frame>=lifetime) labels.pop_front();
}
void add(HouseClass* house,TechnoClass* unit,const Event& event,bool research,int amount) {
    if(house!=HouseClass::CurrentPlayer) return;
    if(unit && unit->Transporter) unit=unit->Transporter;
    CoordStruct position;
    if(unit && unit->IsOnMap && !unit->InLimbo) position=unit->GetCenterCoords();
    else if(event.sequenceCast && event.hasImpact) position=event.impact;
    else if(!unit && event.holder && !event.sequenceCast) position=event.origin;
    else return;
    auto cell=MapClass::Instance.TryGetCellAt(position);
    if(!cell || cell->IsFogged() || cell->IsShrouded()) return;
    if(labels.size()>=256) labels.pop_front();
    labels.push_back({position,static_cast<unsigned>(Unsorted::CurrentFrame),research,
        research?L"+ "+std::to_wstring(amount)+L" RP":L"+$"+std::to_wstring(amount)});
}
void draw() {
    auto surface=DSurface::Temp;
    if(labels.empty() || !surface || !TacticalClass::Instance) return;
    auto bounds=surface->GetRect();
    bounds.Width=std::min(bounds.Width,DSurface::ViewBounds.Width);
    bounds.Height=std::min(bounds.Height,DSurface::ViewBounds.Height);
    for(const auto& label:labels) {
        const auto age=static_cast<unsigned>(Unsorted::CurrentFrame)-label.frame;
        if(age>=lifetime) continue;
        auto [position,visible]=TacticalClass::Instance->CoordsToClient(label.position);
        if(!visible) continue;
        auto cell=MapClass::Instance.TryGetCellAt(label.position);
        if(!cell || cell->IsFogged() || cell->IsShrouded()) continue;
        position.Y-=16+static_cast<int>(age)+(label.research?12:0);
        const auto ink=label.research?Drawing::RGB_To_Int(105,220,255):Drawing::RGB_To_Int(0,255,0);
        surface->DrawText(label.text.c_str(),&bounds,&position,ink,0,TextPrintType::Center|TextPrintType::FullShadow);
        TacticalClass::Instance->RegisterDirtyArea({position.X-72,position.Y-2,144,18},false);
    }
}
}
