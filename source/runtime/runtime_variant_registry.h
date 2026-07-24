#ifndef EI_RUNTIME_VARIANT_REGISTRY_H
#define EI_RUNTIME_VARIANT_REGISTRY_H

#include <cstddef>

namespace ei {
namespace runtime {

enum class ERuntimeProfile
{
    Jmv,
    Base
};

struct SRuntimeVariant
{
    const char* id;
    ERuntimeProfile profile;
    const char* sha256;
    const char* layoutId;
};

static const char kGameLayoutV1[] = "ei-game-layout-v1";

// Canonical built-in runtime allowlist. Both the Qt profile evaluator and the
// injected bridge consume this table so a supported executable cannot be
// accepted by one side and rejected by the other. Entries remain exact-hash
// and layout-specific; user-managed experimental variants are a future UI
// workflow and must not be added here without the normal byte/layout audit.
static const SRuntimeVariant kRuntimeVariants[] = {
    {
        "jmv-starter-engine-v1",
        ERuntimeProfile::Jmv,
        "BE0FC330035E03983CCE590038CC7F881F1F5D1BCF082717F31776DD4DEE94E2",
        kGameLayoutV1
    },
    {
        "base-ei-gog-v1",
        ERuntimeProfile::Base,
        "B871D70D3876B2A5634F0920C92118BC49058B50C7D8CBE68376349F51C2E570",
        kGameLayoutV1
    },
    {
        "base-ei-vk-v1",
        ERuntimeProfile::Base,
        "8ED3F9A500D84D822BD311B74B1B49D5CD16DD12D3502C27D7667957311036C1",
        kGameLayoutV1
    }
};

inline std::size_t runtimeVariantCount()
{
    return sizeof(kRuntimeVariants) / sizeof(kRuntimeVariants[0]);
}

} // namespace runtime
} // namespace ei

#endif
