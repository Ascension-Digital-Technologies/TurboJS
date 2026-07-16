#ifndef QJS_INTERNAL_SUBSYSTEM_H
#define QJS_INTERNAL_SUBSYSTEM_H

/* Stable numeric identifiers used by diagnostics and architecture tooling. */
typedef enum QJSInternalSubsystem {
    QJS_SUBSYSTEM_CORE = 1,
    QJS_SUBSYSTEM_RUNTIME,
    QJS_SUBSYSTEM_OBJECTS,
    QJS_SUBSYSTEM_GC,
    QJS_SUBSYSTEM_VM,
    QJS_SUBSYSTEM_COMPILER,
    QJS_SUBSYSTEM_MODULES,
    QJS_SUBSYSTEM_SERIALIZATION,
    QJS_SUBSYSTEM_BUILTINS
} QJSInternalSubsystem;

#define QJS_INTERNAL_API static
#define QJS_INTERNAL_INLINE static inline

#endif
