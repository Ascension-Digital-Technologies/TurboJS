/*
 * Public module configuration facade.
 *
 * This is the first independently compiled engine subsystem unit. It keeps
 * the public ABI in a small, stable file while the stateful implementation
 * remains behind a narrow private bridge during staged modularization.
 */

#include <turbojs.h>
#include "internal/module_bridge.h"

void JS_SetModuleLoaderFunc(JSRuntime *rt,
                            JSModuleNormalizeFunc *module_normalize,
                            JSModuleLoaderFunc *module_loader,
                            void *opaque)
{
    turbojs_internal_set_module_loader_func(rt, module_normalize,
                                        module_loader, opaque);
}

void JS_SetModuleLoaderFunc2(
    JSRuntime *rt,
    JSModuleNormalizeFunc *module_normalize,
    JSModuleLoaderFunc2 *module_loader,
    JSModuleCheckSupportedImportAttributes *module_check_attrs,
    void *opaque)
{
    turbojs_internal_set_module_loader_func2(rt, module_normalize, module_loader,
                                         module_check_attrs, opaque);
}

void JS_SetModuleNormalizeFunc2(JSRuntime *rt,
                                JSModuleNormalizeFunc2 *module_normalize)
{
    turbojs_internal_set_module_normalize_func2(rt, module_normalize);
}

int JS_SetModulePrivateValue(JSContext *ctx, JSModuleDef *module, JSValue value)
{
    return turbojs_internal_set_module_private_value(ctx, module, value);
}

JSValue JS_GetModulePrivateValue(JSContext *ctx, JSModuleDef *module)
{
    return turbojs_internal_get_module_private_value(ctx, module);
}
