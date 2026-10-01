/* gates_gui_lib - text encodings at the edge (0.10.0).
 *
 * Inside gates every string is UTF-8: the API, the text buffer, the editor.
 * Text that comes from or goes to the outside - a file, the console, another
 * program, an older system - may be in another encoding. These functions turn
 * it into UTF-8 on the way in and back on the way out, so a program keeps one
 * encoding inside and meets each environment in its own.
 *
 * Encodings: UTF-8, UTF-16 and UTF-32 (little or big endian), done here on
 * every platform; and code pages by their Windows numbers (949 = CP949, the
 * Korean Windows ANSI code page, all 11172 Hangul syllables; 51949 = EUC-KR
 * as such, the 2350 of KS X 1001 - the rest are unmappable there; 932, 936,
 * 950, 1252, ...; 0 = the system's), done by a code-page
 * converter the platform installs (the Win32 backend does when the app is
 * created). The UTF code pages 65001, 1200/1201 and 12000/12001 need no
 * converter. Without one, other code pages are UNSUPPORTED.
 *
 * Malformed input (bytes that are not text in that encoding, a lone UTF-16
 * surrogate, a UTF-32 value past U+10FFFF, a cut-off last character) becomes
 * U+FFFD by default; with GATES_ENCODING_STRICT the call refuses it with
 * PROVEN_ERR_INVALID_ENCODING and *bad_at says where (a byte offset in the
 * input). Going out, a character the target cannot hold becomes the target's
 * replacement ('?' in code pages, U+FFFD in the UTF forms never happens);
 * strict refuses it the same way. Results are allocated from `alloc`
 * ({0} = the heap) and freed by the caller with the same allocator; on any
 * error nothing is allocated. Platform-free; safe from any thread once the
 * converter is installed. */
#ifndef GATES_ENCODING_H
#define GATES_ENCODING_H

#include <gates/types.h>

typedef enum gates_encoding_kind_t {
    GATES_ENCODING_UTF8 = 0,
    GATES_ENCODING_UTF16LE,
    GATES_ENCODING_UTF16BE,
    GATES_ENCODING_UTF32LE,
    GATES_ENCODING_UTF32BE,
    GATES_ENCODING_CODEPAGE,     /* `codepage` says which */
} gates_encoding_kind_t;

typedef struct gates_encoding_t {
    gates_encoding_kind_t kind;
    gates_u32 codepage;          /* CODEPAGE: a Windows code page number; 0 = the system's */
    bool bom;                    /* from_utf8: start with the byte order mark (UTF forms only) */
} gates_encoding_t;

#define GATES_CODEPAGE_SYSTEM   0u
#define GATES_CODEPAGE_CP949    949u     /* Korean Windows (EUC-KR and the rest of Hangul) */
#define GATES_CODEPAGE_EUC_KR   51949u
#define GATES_CODEPAGE_UTF8     65001u
#define GATES_CODEPAGE_UTF16LE  1200u
#define GATES_CODEPAGE_UTF16BE  1201u
#define GATES_CODEPAGE_UTF32LE  12000u
#define GATES_CODEPAGE_UTF32BE  12001u

/* Flags. */
#define GATES_ENCODING_STRICT   0x1u     /* refuse malformed or unmappable text */

/* Input in `enc` to UTF-8. A byte order mark of that encoding at the start is
 * not text and is skipped. INVALID_ARG for a null pointer with a size, an
 * unknown kind; UNSUPPORTED for a code page without a converter;
 * INVALID_ENCODING (strict). *bad_at (may be null) is set on INVALID_ENCODING. */
[[nodiscard]] gates_err_t gates_encoding_to_utf8(gates_encoding_t enc, const void *in, gates_usize_t size,
                                                 gates_u32 flags, gates_allocator_t alloc, gates_u8 **out,
                                                 gates_usize_t *out_size, gates_usize_t *bad_at);
/* UTF-8 to `enc` (a byte order mark first when enc.bom). Malformed UTF-8 in
 * `text` is treated as above (*bad_at is then an offset into `text`). */
[[nodiscard]] gates_err_t gates_encoding_from_utf8(gates_encoding_t enc, gates_str_t text, gates_u32 flags,
                                                   gates_allocator_t alloc, gates_u8 **out, gates_usize_t *out_size,
                                                   gates_usize_t *bad_at);

/* A guess for bytes of unknown origin: a byte order mark decides (its length
 * is returned); else a text of whole 16-bit units with zeros in at least
 * three of four odd bytes and none in the even ones is UTF-16LE (the other
 * way round, BE); else valid UTF-8 (all ASCII included) is UTF-8; else the
 * system code page. Returns the BOM length (0 without one). */
gates_usize_t gates_encoding_detect(const void *in, gates_usize_t size, gates_encoding_t *out);

/* true when `text` is valid UTF-8 (no overlong forms, surrogates or values
 * past U+10FFFF); *bad_at (may be null) receives the first bad offset. */
bool gates_utf8_valid(gates_str_t text, gates_usize_t *bad_at);

/* -- the code-page converter (for platform backends and tests) ---------------------
 *
 * Code pages need tables the platform has. A converter turns bytes of a code
 * page into UTF-8 and back with the same rules as above (replace, or strict
 * with *bad_at); it allocates its result from `alloc`. system_codepage says
 * what code page 0 means (Windows: GetACP, 949 on Korean Windows);
 * console_codepage what the console uses (0 when there is none). One
 * converter for the process; set it before converting from other threads. */
typedef struct gates_codepage_converter_t {
    void *ctx;
    gates_err_t (*to_utf8)(void *ctx, gates_u32 codepage, const gates_u8 *in, gates_usize_t size, bool strict,
                           gates_allocator_t alloc, gates_u8 **out, gates_usize_t *out_size, gates_usize_t *bad_at);
    gates_err_t (*from_utf8)(void *ctx, gates_u32 codepage, gates_str_t text, bool strict, gates_allocator_t alloc,
                             gates_u8 **out, gates_usize_t *out_size, gates_usize_t *bad_at);
    gates_u32 (*system_codepage)(void *ctx);
    gates_u32 (*console_codepage)(void *ctx);
} gates_codepage_converter_t;

/* Copied; null removes it. */
void gates_encoding_set_codepage_converter(const gates_codepage_converter_t *converter);
bool gates_encoding_has_codepage_converter(void);
/* The converter's answers (0 without a converter). */
gates_u32 gates_encoding_system_codepage(void);
gates_u32 gates_encoding_console_codepage(void);

/* The Win32 converter (MultiByteToWideChar / WideCharToMultiByte). Only in
 * builds that include src/platform/win32; gates_app_create installs it when
 * no converter is set. */
const gates_codepage_converter_t *gates_codepage_converter_win32(void);

#endif /* GATES_ENCODING_H */
