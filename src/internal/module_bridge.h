#ifndef TURBOJS_INTERNAL_MODULE_BRIDGE_H
#define TURBOJS_INTERNAL_MODULE_BRIDGE_H

/*
 * Narrow bridge between the independently compiled module API facade and the
 * unity-owned module implementation. This is private, ABI-unstable, and is
 * intentionally limited to opaque public TurboJS types.
 */

#include <turbojs.h>

void turbojs_internal_set_module_loader_func(
    JSRuntime *rt,
    JSModuleNormalizeFunc *module_normalize,
    JSModuleLoaderFunc *module_loader,
    void *opaque);

void turbojs_internal_set_module_loader_func2(
    JSRuntime *rt,
    JSModuleNormalizeFunc *module_normalize,
    JSModuleLoaderFunc2 *module_loader,
    JSModuleCheckSupportedImportAttributes *module_check_attrs,
    void *opaque);

void turbojs_internal_set_module_normalize_func2(
    JSRuntime *rt,
    JSModuleNormalizeFunc2 *module_normalize);

int turbojs_internal_set_module_private_value(
    JSContext *ctx,
    JSModuleDef *module,
    JSValue value);

JSValue turbojs_internal_get_module_private_value(
    JSContext *ctx,
    JSModuleDef *module);

#endif
