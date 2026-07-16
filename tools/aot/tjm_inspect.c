#include <stdio.h>
#include <stdlib.h>
#include "jit.h"

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    unsigned char *data;
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    data = (unsigned char *)malloc((size_t)length);
    if (!data) { fclose(file); return NULL; }
    if (length && fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data); fclose(file); return NULL;
    }
    fclose(file); *size = (size_t)length; return data;
}

int main(int argc, char **argv)
{
    unsigned char *data;
    size_t size, i;
    TurboJSAOTModuleInfo info;
    TurboJSAOTModule module;
    TurboJSIRDiagnostic diagnostic;
    if (argc != 2) {
        fprintf(stderr, "usage: turbojs-aot-inspect <module.tjm>\n");
        return 2;
    }
    data = read_file(argv[1], &size);
    if (!data) { fprintf(stderr, "unable to read: %s\n", argv[1]); return 1; }
    if (TurboJS_AOTInspectModule(data, size, &info, &diagnostic) != TURBOJS_IR_OK) {
        fprintf(stderr, "invalid module: %s\n", diagnostic.message ? diagnostic.message : "unknown error");
        free(data); return 1;
    }
    if (TurboJS_AOTDeserializeModule(data, size, &module, &diagnostic) != TURBOJS_IR_OK) {
        fprintf(stderr, "unable to load module: %s\n", diagnostic.message ? diagnostic.message : "unknown error");
        free(data); return 1;
    }
    printf("TurboJS AOT module\n");
    printf("  version:   %u\n", (unsigned)info.version);
    printf("  functions: %zu\n", info.function_count);
    printf("  bytes:     %zu\n", info.image_size);
    printf("  checksum:  %08x\n", (unsigned)info.checksum);
    for (i = 0; i < module.function_count; ++i)
        printf("  export[%zu]: %s (%zu IR instructions)\n", i,
               module.functions[i].name, module.functions[i].ir.instruction_count);
    TurboJS_AOTModuleDestroy(&module);
    free(data);
    return 0;
}
