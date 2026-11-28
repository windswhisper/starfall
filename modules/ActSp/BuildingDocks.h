#pragma once
#include <BuildingClass.h>
#include <FootClass.h>
#include <algorithm>

namespace BuildingDocks {
inline bool reserve(BuildingClass* host,FootClass* child,int berths) {
    const int slot=host->FindLinkIndex(child);
    if(slot>=0) return slot<berths;
    for(int i=0;i<std::min(host->RadioLinks.Capacity,berths);++i)
        if(!host->GetNthLink(i)) return child->SendCommand(RadioCommand::RequestLink,host)==RadioCommand::AnswerPositive;
    return false;
}
inline void release(BuildingClass* host,FootClass* child) {
    if(!host->ContainsLink(child)) return;
    if(child->IsTether) child->SendCommand(RadioCommand::RequestUntether,host);
    child->SendCommand(RadioCommand::NotifyUnlink,host);
}
}
