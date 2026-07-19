#include <stdio.h>
#include <string.h>
#include "optimization.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    return 1; \
} } while (0)

int main(void)
{
    const TurboJSPipelineIdentity *identity;
    CHECK(TurboJS_PipelineComponentCount() ==
          TURBOJS_PIPELINE_COMPONENT_COUNT);
    CHECK(strcmp(TurboJS_OptimizationTierCodename(
                     TURBOJS_TIER_INTERPRETER), "Pulse") == 0);
    CHECK(strcmp(TurboJS_OptimizationTierCodename(
                     TURBOJS_TIER_BASELINE), "Spool") == 0);
    CHECK(strcmp(TurboJS_OptimizationTierCodename(
                     TURBOJS_TIER_OPTIMIZING), "Redline") == 0);
    CHECK(strcmp(TurboJS_OptimizationTierCodename(
                     TURBOJS_TIER_AOT), "Forge") == 0);

    identity = TurboJS_PipelineIdentity(TURBOJS_PIPELINE_CLUTCH_CALL_ABI);
    CHECK(identity != NULL);
    CHECK(strcmp(identity->codename, "Clutch") == 0);
    CHECK(strstr(identity->description, "generation-checked") != NULL);

    identity = TurboJS_PipelineIdentity(TURBOJS_PIPELINE_BEACON_IDENTITY_REGISTRY);
    CHECK(identity != NULL);
    CHECK(strcmp(identity->codename, "Beacon") == 0);
    CHECK(strstr(identity->description, "live bytecode") != NULL);

    identity = TurboJS_PipelineIdentity(TURBOJS_PIPELINE_SLIPSTREAM_OSR);
    CHECK(identity != NULL);
    CHECK(strcmp(identity->codename, "Slipstream") == 0);
    CHECK(strstr(identity->description, "frames") != NULL);

    puts("TurboJS execution pipeline identity passed");
    return 0;
}
