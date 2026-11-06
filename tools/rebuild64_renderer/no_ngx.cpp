// Ataman host policy: this target has no NVIDIA SDK, runtime, or NGX worker.
#include "dlss_backend.h"
#include <cstdio>
namespace owned {
struct DlssBackend::InitStorage {};
DlssBackend::DlssBackend() { std::snprintf(capability_.reason,sizeof(capability_.reason),"DLSS excluded from Ataman build"); }
DlssBackend::~DlssBackend() = default;
bool DlssBackend::initialize(ID3D11Device*,Log) noexcept { return false; }
bool DlssBackend::optimal(unsigned,unsigned,DlssQuality,unsigned&,unsigned&) noexcept { return false; }
bool DlssBackend::create_feature(ID3D11DeviceContext*,const DlssShape&) noexcept { return false; }
void DlssBackend::release_feature() noexcept {}
void DlssBackend::device_lost(HRESULT) noexcept {}
void DlssBackend::shutdown() noexcept {}
bool DlssBackend::evaluate(ID3D11DeviceContext*,const DlssFrame&) noexcept { return false; }
}
