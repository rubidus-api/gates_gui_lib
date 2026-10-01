/* manual example (host): text from outside in another encoding, UTF-8 inside, back out.
 * expect: UTF-16LE with a 2-byte mark; 9 characters in 13 UTF-8 bytes; out as UTF-32BE in 36 bytes; strict refuses byte 2; code page 949 here: unsupported */
#include <gates/gates.h>
#include <proven/heap.h>

#include <stdio.h>

static const char *name(gates_encoding_kind_t k) {
    static const char *const names[] = { "UTF-8", "UTF-16LE", "UTF-16BE", "UTF-32LE", "UTF-32BE", "a code page" };
    return names[k];
}

int main(void) {
    /* A file's bytes: a UTF-16LE mark, then "Hi, " and two Hangul syllables U+D55C U+AE00, then "!". */
    static const unsigned char file[] = { 0xFF, 0xFE, 'H', 0, 'i', 0, ',', 0, ' ', 0, 0x5C, 0xD5, 0x00, 0xAE,
                                          '!', 0, '\r', 0, '\n', 0 };
    gates_encoding_t enc;
    gates_usize_t mark = gates_encoding_detect(file, sizeof file, &enc);
    printf("%s with a %u-byte mark; ", name(enc.kind), (unsigned)mark);

    /* In: one encoding inside the program from here on (the mark is skipped). */
    gates_u8 *text = nullptr;
    gates_usize_t size = 0;
    if (!gates_is_ok(gates_encoding_to_utf8(enc, file, sizeof file, 0, (gates_allocator_t){0}, &text, &size, nullptr))) {
        return 1;
    }
    gates_str_t s = { .ptr = text, .size = size };
    unsigned chars = 0;
    for (gates_u32 at = 0; at < size; chars++) at += gates_text_decode(s, at, &(gates_u32){0});
    printf("%u characters in %u UTF-8 bytes; ", chars, (unsigned)size);

    /* Out: whatever the other side wants. */
    gates_u8 *wide = nullptr;
    gates_usize_t wide_size = 0;
    if (!gates_is_ok(gates_encoding_from_utf8((gates_encoding_t){ .kind = GATES_ENCODING_UTF32BE }, s, 0,
                                              (gates_allocator_t){0}, &wide, &wide_size, nullptr))) {
        return 1;
    }
    printf("out as UTF-32BE in %u bytes; ", (unsigned)wide_size);

    /* Broken input: replaced with U+FFFD by default, or refused with where it broke. */
    gates_usize_t bad_at = 0;
    gates_u8 *junk = nullptr;
    gates_err_t err = gates_encoding_to_utf8((gates_encoding_t){ .kind = GATES_ENCODING_UTF8 }, "ok\xFF", 3,
                                             GATES_ENCODING_STRICT, (gates_allocator_t){0}, &junk, &size, &bad_at);
    printf("strict refuses byte %u; ", err == PROVEN_ERR_INVALID_ENCODING ? (unsigned)bad_at : 99u);

    /* Code pages (949 = Korean Windows, EUC-KR and more) need a converter: on Windows the app
     * installs the platform's; gates_codepage_converter_cp949() is a built-in one for 949 and
     * 51949. This program installs none, so the call says so. */
    err = gates_encoding_to_utf8((gates_encoding_t){ .kind = GATES_ENCODING_CODEPAGE, .codepage = GATES_CODEPAGE_CP949 },
                                 "\xC7\xD1", 2, 0, (gates_allocator_t){0}, &junk, &size, nullptr);
    printf("code page 949 here: %s\n", err == PROVEN_ERR_UNSUPPORTED ? "unsupported" : "converted");

    gates_allocator_t heap = proven_heap_allocator();
    heap.free_fn(heap.ctx, text);
    heap.free_fn(heap.ctx, wide);
    return 0;
}
