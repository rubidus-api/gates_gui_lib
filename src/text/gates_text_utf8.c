/* gates_gui_lib - shared UTF-8 decoding and cell-width rules.
 * The single source of truth used by every text backend and the edit core,
 * so cell counts cannot drift between them. Platform-free. */
#include <gates/text.h>

gates_u32 gates_text_decode(gates_str_t text, gates_u32 at, gates_u32 *out_cp) {
    gates_u32 cp = 0xFFFD;
    if (text.ptr == nullptr || at >= text.size) {
        if (out_cp != nullptr) {
            *out_cp = cp;
        }
        return 1;
    }
    const gates_u8 *p = (const gates_u8 *)text.ptr + at;
    gates_u32 avail = (gates_u32)text.size - at;
    gates_u8 b = p[0];
    gates_u32 need;
    gates_u32 min_cp; /* the smallest value a sequence of this length may carry */

    if (b < 0x80) {
        cp = b;
        need = 0;
        min_cp = 0;
    } else if ((b & 0xE0) == 0xC0) {
        need = 1;
        cp = b & 0x1Fu;
        min_cp = 0x80;
    } else if ((b & 0xF0) == 0xE0) {
        need = 2;
        cp = b & 0x0Fu;
        min_cp = 0x800;
    } else if ((b & 0xF8) == 0xF0) {
        need = 3;
        cp = b & 0x07u;
        min_cp = 0x10000;
    } else {
        if (out_cp != nullptr) {
            *out_cp = 0xFFFD;
        }
        return 1;
    }

    if (need + 1 > avail) {
        if (out_cp != nullptr) {
            *out_cp = 0xFFFD;
        }
        return 1;
    }
    for (gates_u32 i = 1; i <= need; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            if (out_cp != nullptr) {
                *out_cp = 0xFFFD;
            }
            return 1;
        }
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    /* Overlong forms, surrogates and values past U+10FFFF are malformed: one
     * byte, U+FFFD (the shared rule every backend and the edit core rely on). */
    if (cp < min_cp || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        if (out_cp != nullptr) {
            *out_cp = 0xFFFD;
        }
        return 1;
    }
    if (out_cp != nullptr) {
        *out_cp = cp;
    }
    return need + 1;
}

/* East-Asian wide/fullwidth subset sufficient for v0.x (Hangul, CJK, kana,
 * fullwidth forms). Conservative: anything unlisted is narrow. */
gates_u32 gates_text_cell_width(gates_u32 cp) {
    bool wide = (cp >= 0x1100 && cp <= 0x115F)   /* Hangul Jamo (leading) */
             || (cp >= 0x2E80 && cp <= 0x303E)   /* CJK radicals, punctuation */
             || (cp >= 0x3041 && cp <= 0x33FF)   /* kana, compat, Hangul compat jamo */
             || (cp >= 0x3400 && cp <= 0x4DBF)   /* CJK ext A */
             || (cp >= 0x4E00 && cp <= 0x9FFF)   /* CJK unified */
             || (cp >= 0xA960 && cp <= 0xA97F)   /* Hangul Jamo ext A */
             || (cp >= 0xAC00 && cp <= 0xD7A3)   /* Hangul syllables */
             || (cp >= 0xF900 && cp <= 0xFAFF)   /* CJK compat ideographs */
             || (cp >= 0xFE30 && cp <= 0xFE4F)   /* CJK compat forms */
             || (cp >= 0xFF00 && cp <= 0xFF60)   /* fullwidth forms */
             || (cp >= 0xFFE0 && cp <= 0xFFE6)   /* fullwidth signs */
             || (cp >= 0x20000 && cp <= 0x3FFFD);/* CJK ext B+ */
    return wide ? 2u : 1u;
}

gates_u32 gates_text_cells(gates_str_t text) {
    gates_u32 cells = 0;
    gates_u32 i = 0;
    while (i < text.size) {
        gates_u32 cp;
        i += gates_text_decode(text, i, &cp);
        cells += gates_text_cell_width(cp);
    }
    return cells;
}

gates_u32 gates_text_next_offset(gates_str_t text, gates_u32 at) {
    if (at >= text.size) {
        return (gates_u32)text.size;
    }
    gates_u32 next = at + gates_text_decode(text, at, nullptr);
    return next > (gates_u32)text.size ? (gates_u32)text.size : next;
}

gates_u32 gates_text_prev_offset(gates_str_t text, gates_u32 at) {
    if (at == 0 || text.ptr == nullptr) {
        return 0;
    }
    if (at > (gates_u32)text.size) {
        at = (gates_u32)text.size;
    }
    /* Walk back over continuation bytes (at most 3), then validate forward so
     * malformed input still moves exactly one byte. */
    gates_u32 start = at;
    const gates_u8 *p = (const gates_u8 *)text.ptr;
    gates_u32 steps = 0;
    while (start > 0 && steps < 4) {
        start--;
        steps++;
        if ((p[start] & 0xC0) != 0x80) {
            break;
        }
    }
    gates_u32 consumed = gates_text_decode(text, start, nullptr);
    if (start + consumed != at) {
        return at - 1; /* malformed: byte-wise fallback */
    }
    return start;
}

bool gates_text_utf8_valid(gates_str_t text, gates_usize_t *bad_at) {
    for (gates_usize_t pos = 0; pos < text.size;) {
        gates_u32 cp = 0;
        gates_u32 used = gates_text_decode(text, (gates_u32)pos, &cp);
        if (cp == 0xFFFDu && used == 1) { /* a real U+FFFD takes three bytes */
            if (bad_at != nullptr) *bad_at = pos;
            return false;
        }
        pos += used;
    }
    return true;
}
