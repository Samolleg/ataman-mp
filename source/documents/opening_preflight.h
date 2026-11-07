#pragma once
#include "mpr_document.h"
#include "endurance_document.h"
#include "mob.h"
#include "opening_trace.h"
struct SOpeningPreflight {
    CMprDocument mpr;CMobDocument mob;CValidationReport mprReport,mobReport;
    bool mprValid{false},mobValid{false},projectionValid{false};QString projectionError;
};
inline SOpeningPreflight openingPreflight(const QFileInfo& mpr,const QFileInfo& mob,bool terrainOnly) {
    SOpeningPreflight r;
    {opening::Scope s("cpu.validate_mpr");r.mprValid=r.mpr.load(mpr,&r.mprReport);}
    {opening::Scope s("cpu.validate_mob");r.mobValid=terrainOnly||r.mob.load(mob,&r.mobReport);}
    {opening::Scope s("cpu.validate_editable_projection");r.projectionValid=terrainOnly||CMob::validateEditableProjection(r.mob.originalBytes(),mob,&r.projectionError);}
    return r;
}
