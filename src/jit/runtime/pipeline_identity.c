#include "optimization.h"

static const TurboJSPipelineIdentity turbojs_pipeline_identities[] = {
    { TURBOJS_PIPELINE_ROTOR_FRONTEND, "Rotor", "bytecode frontend",
      "Parses JavaScript and emits validated TurboJS bytecode." },
    { TURBOJS_PIPELINE_PULSE_INTERPRETER, "Pulse", "interpreter",
      "Executes cold or unsupported bytecode with canonical semantics." },
    { TURBOJS_PIPELINE_SPOOL_BASELINE, "Spool", "baseline JIT",
      "Quickly lowers broad bytecode coverage to native code." },
    { TURBOJS_PIPELINE_REDLINE_OPTIMIZER, "Redline", "optimizing JIT",
      "Builds feedback-specialized SSA and high-performance native code." },
    { TURBOJS_PIPELINE_FORGE_AOT, "Forge", "ahead-of-time compiler",
      "Produces portable or native modules outside the hot execution path." },
    { TURBOJS_PIPELINE_TELEMETRY_FEEDBACK, "Telemetry", "feedback system",
      "Collects value, shape, and call-target behavior for specialization." },
    { TURBOJS_PIPELINE_RELAY_INLINE_CACHE, "Relay", "inline caches",
      "Provides patchable property, element, and call-site fast paths." },
    { TURBOJS_PIPELINE_CLUTCH_CALL_ABI, "Clutch", "compiled call ABI",
      "Publishes generation-checked native entries between Relay, Spool, and Vault." },
    { TURBOJS_PIPELINE_BEACON_IDENTITY_REGISTRY, "Beacon", "live function identity registry",
      "Resolves Telemetry identities to live bytecode objects without retaining stale pointers." },
    { TURBOJS_PIPELINE_SLIPSTREAM_OSR, "Slipstream", "OSR and tier transitions",
      "Transfers live frames between Pulse, Spool, and Redline." },
    { TURBOJS_PIPELINE_REWIND_DEOPT, "Rewind", "deoptimization",
      "Reconstructs lower-tier frames after speculative guard failure." },
    { TURBOJS_PIPELINE_GEARBOX_BACKEND, "Gearbox", "machine backend",
      "Performs lowering, register allocation, and architecture emission." },
    { TURBOJS_PIPELINE_VAULT_CODE_CACHE, "Vault", "native code cache",
      "Owns executable memory, code aging, lookup, and invalidation." },
};

const char *TurboJS_PipelineComponentCodename(TurboJSPipelineComponent component)
{
    const TurboJSPipelineIdentity *identity = TurboJS_PipelineIdentity(component);
    return identity ? identity->codename : "Unknown";
}

const char *TurboJS_PipelineComponentRole(TurboJSPipelineComponent component)
{
    const TurboJSPipelineIdentity *identity = TurboJS_PipelineIdentity(component);
    return identity ? identity->role : "unknown";
}

const TurboJSPipelineIdentity *TurboJS_PipelineIdentity(
    TurboJSPipelineComponent component)
{
    size_t index = (size_t)component;
    if (index >= sizeof(turbojs_pipeline_identities) /
                 sizeof(turbojs_pipeline_identities[0]))
        return NULL;
    if (turbojs_pipeline_identities[index].component != component)
        return NULL;
    return &turbojs_pipeline_identities[index];
}

size_t TurboJS_PipelineComponentCount(void)
{
    return sizeof(turbojs_pipeline_identities) /
           sizeof(turbojs_pipeline_identities[0]);
}

const char *TurboJS_OptimizationTierCodename(TurboJSOptimizationTier tier)
{
    switch (tier) {
    case TURBOJS_TIER_INTERPRETER:
        return TurboJS_PipelineComponentCodename(TURBOJS_PIPELINE_PULSE_INTERPRETER);
    case TURBOJS_TIER_BASELINE:
        return TurboJS_PipelineComponentCodename(TURBOJS_PIPELINE_SPOOL_BASELINE);
    case TURBOJS_TIER_OPTIMIZING:
        return TurboJS_PipelineComponentCodename(TURBOJS_PIPELINE_REDLINE_OPTIMIZER);
    case TURBOJS_TIER_AOT:
        return TurboJS_PipelineComponentCodename(TURBOJS_PIPELINE_FORGE_AOT);
    default:
        return "Unknown";
    }
}

