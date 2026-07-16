import {loadFile, writeFile} from "qjs:std"

const cutils_h = loadFile("src/internal/cutils.h")
const dtoa_c = loadFile("src/numeric/dtoa.c")
const dtoa_h = loadFile("src/numeric/dtoa.h")
const libregexp_c = loadFile("src/regexp/regexp.c")
const libregexp_h = loadFile("src/regexp/regexp.h")
const libregexp_opcode_h = loadFile("src/regexp/regexp_opcode.h")
const libunicode_c = loadFile("src/unicode/unicode.c")
const libunicode_h = loadFile("src/unicode/unicode.h")
const libunicode_table_h = loadFile("src/unicode/unicode_tables.h")
const list_h = loadFile("src/internal/list.h")
const atom_defs_h = loadFile("src/internal/atom_defs.h")
const turbojs_engine_c = loadFile("src/api/legacy_api.c")
const atomics_h = loadFile("src/internal/atomics_compat.h")
const turbojs_h = loadFile("src/api/turbojs.h")
const turbojs_libc_c = loadFile("runtime/libc/turbojs-libc.c")
const turbojs_libc_h = loadFile("src/api/turbojs-libc.h")
const bytecode_opcodes_h = loadFile("src/internal/bytecode_opcodes.h")
const gen_builtin_array_fromasync_h = loadFile("src/generated/builtins/array_fromasync.h")
const gen_builtin_iterator_zip_h = loadFile("src/generated/builtins/iterator_zip.h")
const gen_builtin_iterator_zip_keyed_h = loadFile("src/generated/builtins/iterator_zip_keyed.h")

let source = "#if defined(TURBOJS_BUILD_LIBC) && defined(__linux__) && !defined(_GNU_SOURCE)\n"
           + "#define _GNU_SOURCE\n"
           + "#endif\n"
           + atomics_h
           + cutils_h
           + dtoa_h
           + list_h
           + libunicode_h // exports lre_is_id_start, used by src/regexp/regexp.h
           + libregexp_h
           + libunicode_table_h
           + turbojs_h
           + turbojs_engine_c
           + dtoa_c
           + libregexp_c
           + libunicode_c
           + "#ifdef TURBOJS_BUILD_LIBC\n"
           + turbojs_libc_h
           + turbojs_libc_c
           + "#endif // TURBOJS_BUILD_LIBC\n"
source = source.replace(/#include "atom_defs.h"/g, atom_defs_h)
source = source.replace(/#include "bytecode_opcodes.h"/g, bytecode_opcodes_h)
source = source.replace(/#include "src/regexp/regexp_opcode.h"/g, libregexp_opcode_h)
source = source.replace(/#include "src/generated/builtins/array_fromasync.h"/g,
                        gen_builtin_array_fromasync_h)
source = source.replace(/#include "src/generated/builtins/iterator_zip.h"/g,
                        gen_builtin_iterator_zip_h)
source = source.replace(/#include "src/generated/builtins/iterator_zip_keyed.h"/g,
                        gen_builtin_iterator_zip_keyed_h)
source = source.replace(/#include "[^"]+"/g, "")
writeFile(execArgv[2] ?? "turbojs-amalgam.c", source)
