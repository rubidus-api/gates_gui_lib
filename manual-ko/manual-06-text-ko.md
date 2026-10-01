# 6장 - 글자 입력

헤더: `gates/widget.h`(텍스트 상자), `gates/text_edit.h`, `gates/clipboard.h`, `gates/input.h`,
`gates/ui.h`.

## 텍스트 상자

텍스트 상자는 UTF-8 글 한 줄을 편집한다. 사람은 입력하고, Shift 와 포인터로 고르고(Shift+클릭은
넓히고, 두 번 누르면 단어, 세 번 누르면 전부), 글자 단위와
처음·끝(Home, End)으로 옮기고, 복사·잘라내기·붙여넣기(Ctrl+C, Ctrl+X, Ctrl+V)와 되돌리기·다시
하기(Ctrl+Z, Ctrl+Y)를 한다. 편집이 성공할 때마다 확정된 글과 함께 TEXT_CHANGED 가 대기열에 들어간다.
프로그램이 부르는 `gates_textbox_set_text` 는 조용하고 되돌리기 이력을 지운다. 모든 텍스트 상자
밑에는 편집 코어(`gates/text_edit.h`)가 있다. 글, 캐럿, 고른 범위, 열린 조합을 UTF-8 바이트 위치로
다룬다.

| 설정 | 효과 |
|---|---|
| `gates_textbox_set_read_only` | 포커스, 고르기, 복사는 된다. 편집, 붙여넣기, 잘라내기, 되돌리기, 조합은 거절한다 |
| `gates_textbox_set_password` | 글자마다 `*` 하나, 복사·잘라내기 없음, 이벤트에 글 없음, 되돌리기 없음, IME 없음 |
| `gates_textbox_set_max_bytes` | UTF-8 바이트로 잰 최대 길이(아래를 보라) |
| `gates_textbox_set_undo_limits` | 되돌리기 이력의 한도. 기본 64개, 16384 바이트 |
| `gates_textbox_set_invalid` | 틀림 모양(폼이 대신 해 준다) |

## 한도는 질문이다

입력이 최대 길이를 넘으려 하면 gates 는 아무것도 넣지 않고 묻는다. LIMIT_EXCEEDED 이벤트에 거절된
입력과, 그 가운데 몇 바이트가 들어갈 수 있는지가 담긴다. 프로그램은 사람에게 들어가는 만큼 넣을지,
버릴지 묻고, `gates_textbox_accept_fit` 이나 `gates_textbox_discard_rejected` 로 답한다. 붙여 넣은
계좌 번호를 소리 없이 반으로 자르는 실패를 이렇게 피한다.

<!-- example: manual/examples/ex_07_text.c -->
```c
/* manual example (host): a text box with a limit - the overflow is a question.
 * expect: offered 7 bytes, 5 fit; kept "Seoul" */
#include <gates/gates.h>

#include <stdio.h>

static gates_u32 offered, fit;

static void on_box(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    (void)tree;
    (void)user;
    if (ev->kind == GATES_EVENT_LIMIT_EXCEEDED) {
        offered = (gates_u32)ev->text.size;
        fit = ev->fit_bytes;
    }
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t city;
    if (!gates_is_ok(gates_textbox_create(t, gates_tree_root(t), GATES_STR(""), 20, &city)) ||
        !gates_is_ok(gates_textbox_set_max_bytes(t, city, 5)) ||
        !gates_is_ok(gates_widget_set_handler(t, city, on_box, nullptr))) {
        return 1;
    }
    gates_tree_set_focus(t, city);
    /* Typed or pasted input (an IME delivers its result the same way). */
    (void)gates_input_commit(t, GATES_STR("Seoul!!"));
    (void)gates_tree_dispatch_events(t, 0);
    /* Nothing was inserted: the box holds the input as an offer. Here the
     * answer is "keep what fits"; gates_textbox_discard_rejected drops it. */
    if (!gates_is_ok(gates_textbox_accept_fit(t, city))) return 1;
    gates_str_t text = gates_textbox_text(t, city);
    printf("offered %u bytes, %u fit; kept \"%.*s\"\n", offered, fit, (int)text.size, (const char *)text.ptr);
    gates_tree_destroy(t);
    return 0;
}
```

## 설치된 입력기

gates 는 입력기를 구현하지 않는다. Windows 에서는 설치된 IME(한국어, 일본어, 중국어 ...)가 IMM32 를
통해 모든 텍스트 상자에서 동작한다. 조합 중인 음절은 캐럿 자리에 바로 보이고, 후보 창은 캐럿 아래에
열리며, 끝난 글은 편집 하나로 들어온다. 읽기 전용이나 비밀번호 상자는 포커스를 가진 동안 IME 를
끈다. 글을 스스로 넣는 프로그램(시험, 자동화)을 위해 `gates_input_commit` 은 IME 처럼 글을 넣고,
`gates_input_preedit` 은 조합을 보인다.

## 0.11.0 의 유니코드 한계

- 글은 어디서나 UTF-8 이다. 잘못된 입력은 거절하며, 몰래 고치지 않는다. 다른 인코딩의 글은 가장자리에서
  바꾼다(아래).
- 글자마다 글꼴에서 가져온 제 폭(advance)이 있다. UI 글꼴에서는 비례폭, 고정폭 글꼴에서는
  고정폭이다(8장). 글줄의 폭은 글자 폭의 합과 정확히 같으므로 캐럿, 고른 범위, 누른 자리는 늘
  글자 사이에 떨어진다.
- 커닝, 합자, 셰이핑은 없다. 셰이핑이 필요한 문자(아랍 문자, 인도계 문자, 타이 문자)는 글자가
  하나씩 따로 보이고, 오른쪽에서 왼쪽으로 쓰는 글은 다시 배열하지 않는다.
- 편집은 문자소 묶음(grapheme cluster)이 아니라 코드 포인트 단위로 움직인다. 바탕 글자와 결합
  부호는 두 걸음이다.
- Win32 백엔드는 시스템의 진짜 글꼴로 그리며, 그 글꼴에 없는 글자(Segoe UI 의 한글)는 시스템의
  대체 글꼴에서 가져온다. 내장 백엔드(시험, 창 없음)는 고정폭이고 ASCII 를 그리며, 나머지는 맞는
  너비의 상자로 그린다.
- 텍스트 상자는 한 줄이다. 여러 줄은 편집기의 몫이다(아래).

## 클립보드

창이 플랫폼 클립보드를 대 준다(`gates/clipboard.h`). 글은 UTF-8 로 넘나들고 가장자리에서 바뀐다.
클립보드 제공자가 없는 트리(창 없음)에서는 복사, 잘라내기, 붙여넣기가 아무 일도 하지 않는다.

## 바깥의 다른 인코딩

프로그램 안에서는 API, 텍스트 상자, 편집기, 텍스트 버퍼 모두가 UTF-8 이다. 바깥은 늘 그렇지 않다. 파일은 바이트 순서
표시가 붙은 UTF-16 일 수 있고, 한국어 Windows 의 콘솔은 949 코드 페이지를 쓰며, 옛 프로그램은 EUC-KR 로 쓴다.
`gates/encoding.h` 가 가장자리에서 바꿔 주므로, 프로그램은 안에서 인코딩 하나만 두고 환경마다 그 환경의 인코딩으로
만난다(0.10.0).

- `gates_encoding_to_utf8(enc, bytes, size, flags, alloc, &out, &out_size, &bad_at)` 는 글을 들여온다. 그 인코딩의
  바이트 순서 표시는 건너뛴다. `gates_encoding_from_utf8` 은 다시 내보낸다(`enc.bom` 이면 표시를 앞에 붙인다).
  결과는 `alloc`({0} = 힙)에서 받고, 세지 않는 0 바이트로 끝나며(그대로 C 문자열이나 와이드 문자열), 부른 쪽이 푼다.
- UTF-8, UTF-16, UTF-32 는 두 바이트 순서 모두 gates 가 어디서나 바꾼다. 코드 페이지는 Windows 번호로 고르며
  (`GATES_CODEPAGE_CP949` 949, `GATES_CODEPAGE_EUC_KR` 51949, 932, 936, 1252 …, 0 = 시스템의 것) 플랫폼의 표가 있어야
  한다. Win32 앱은 만들어질 때 변환기를 넣고, 변환기가 없으면 UNSUPPORTED 다. `gates_encoding_system_codepage` 와
  `gates_encoding_console_codepage` 는 시스템과 콘솔이 쓰는 코드 페이지를 알려 준다.
- 깨진 입력은 U+FFFD 가 된다. `GATES_ENCODING_STRICT` 를 주면 대신 거절하고 `bad_at` 이 몇 번째 바이트인지 알려 준다.
  코드 페이지에 없는 글자는 그 코드 페이지의 `?` 가 되며, strict 면 거절된다.
- `gates_encoding_detect` 는 어디서 왔는지 모르는 바이트를 짐작한다. 바이트 순서 표시가 먼저 정하고, 다음은 0 바이트로
  보는 UTF-16, 그다음 올바른 UTF-8, 아니면 시스템 코드 페이지다. `gates_utf8_valid` 는 글을 편집기에 넣기 전에 확인한다
  (편집기는 UTF-8 이 아닌 글을 거절한다).
- 물어볼 OS 가 없는 대상(마이크로컨트롤러, RTOS 의 GUI)에는 `gates_codepage_converter_cp949()` 가 있다. Windows 11 에서
  측정한 표로 949(CP949)와 51949(EUC-KR)를 Windows 와 똑같이 바꾸는 gates 자체 변환기다(0.11.0).
  `gates_encoding_set_codepage_converter` 로 넣으며, 이것을 부르는 프로그램만 그 표(약 120 KB)를 싣는다.
- 메모리를 아껴야 하는 대상에는 할당 없이 쓴다. `gates_encoding_to_utf8_buf` / `_from_utf8_buf` 는 부른 쪽의
  버퍼에 쓰고(버퍼 없이 부르면 크기만 알려 준다), `gates_encoding_stream_t` 는 조각으로 바꾼다. 블록으로 읽는
  파일, 직렬선으로 오는 바이트처럼 조각 끝에서 잘린 글자는 다음 조각으로 넘긴다. 둘 다 UTF 형식을 다룬다.
- Windows 에서는 `main` 의 인자가 ANSI 코드 페이지로 오고, UTF-8 콘솔이 아니면 UTF-8 `printf` 가 깨져 보인다.
  `gates_args_utf8_win32` 는 명령줄을 UTF-8 인자로 주고(Windows 규칙으로 나누는 `gates_args_split` 은 어디서나
  쓸 수 있다), `gates_console_write_win32` 는 콘솔에 UTF-8 을 쓰며 출력이 다른 곳으로 돌려졌으면 바이트를 그대로 쓴다.

<!-- example: manual/examples/ex_06_encodings.c -->
```c
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
```

## 여러 줄: 편집기

`gates_editor_create`(gates/editor.h)는 텍스트 버퍼 위에 여러 줄 편집기를 만든다. 보이는 줄만 그리므로
긴 파일도 창만큼만 든다. 화살표, Home/End, PageUp/PageDown 과 그 Ctrl 조합이 캐럿을 옮기고(Shift 는
선택, Shift+클릭은 넓히고, 두 번 누르면 단어, 세 번 누르면 줄 끝까지 포함한 줄), Enter 는 글의 원래 줄 끝을 따르고, Tab 은 설명에서 탭 입력을 청하지 않는 한 포커스를 옮기며,
Ctrl+A/C/X/V/Z/Y 는 어디서나처럼 동작한다. 사람이 고친 것은 되돌릴 수 있고(이어 치거나 지운 것은 한
걸음), `gates_editor_modified` 는 글이 설정하거나 저장한 것과 다른지 알려 준다. 프로그램은
`gates_editor_buffer` 로 글을 읽고 `gates_editor_set_text` 나 `gates_editor_replace` 로 바꾼다.
TEXT_CHANGED 는 글을 담지 않으므로 긴 파일이 이벤트에 복사되는 일은 없다.

<!-- example: manual/examples/ex_06_editor.c -->
```c
/* manual example (host): a multi-line editor - typing, lines, undo and the modified mark.
 * expect: 3 lines, caret on line 2; after undo: 2 lines, modified: no */
#include <gates/gates.h>

#include <stdio.h>

static void press(gates_tree_t *t, gates_key_t key, bool ctrl) {
    gates_key_event_t ev = { .key = key, .ctrl = ctrl, .down = true };
    (void)gates_input_key(t, &ev);
    ev.down = false;
    (void)gates_input_key(t, &ev);
}

static void type(gates_tree_t *t, const char *s) {
    for (; *s != 0; s++) (void)gates_input_char(t, (gates_u32)(unsigned char)*s);
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t notes;
    gates_editor_desc_t desc = { .rows = 8, .cols = 40 };
    if (!gates_is_ok(gates_editor_create(t, gates_tree_root(t), &desc, &notes)) ||
        !gates_is_ok(gates_editor_set_text(t, notes, GATES_STR("Shopping\nMilk"))) ||
        !gates_is_ok(gates_node_set_access_name(t, notes, GATES_STR("Notes"))) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 360, 200 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* A person goes to the end, starts a new line and types. */
    gates_tree_set_focus(t, notes);
    press(t, GATES_KEY_END, true);
    press(t, GATES_KEY_ENTER, false);
    type(t, "Bread");
    const gates_text_buffer_t *text = gates_editor_buffer(t, notes);
    gates_u32 caret = 0;
    gates_editor_selection(t, notes, nullptr, &caret);
    printf("%u lines, caret on line %u; ", gates_text_buffer_line_count(text), gates_text_buffer_line_of(text, caret));

    /* Ctrl+Z takes the typing back, then the new line: the text is as it was set. */
    press(t, GATES_KEY_Z, true);
    press(t, GATES_KEY_Z, true);
    printf("after undo: %u lines, modified: %s\n", gates_text_buffer_line_count(text),
           gates_editor_modified(t, notes) ? "yes" : "no");
    gates_tree_destroy(t);
    return 0;
}
```

설명에서 줄 바꿈(행은 들어가는 마지막 공백 뒤에서 나뉘고, 그러면 Up 과 Down 은 행 단위로 움직이며,
Home 과 End 는 줄보다 먼저 행의 처음과 끝으로 가고, 행 끝의 캐럿은 그 자리에 그려진다),
줄 번호 여백, 자동 들여쓰기를 켤 수 있다. Tab 이 탭을 칠 때 Tab 과 Shift+Tab 은 선택한 줄을 들여쓰고
내어쓰며, Ctrl+Tab 이 포커스를 다음으로 옮기므로 키보드가 갇히지 않는다. 강조는 프로그램의 몫이다.
`gates_editor_set_styles` 가 스타일 바이트를 색에 잇고, 스타일러(`gates_editor_set_styler`)는 그리기
직전에 곧 보일 줄 가운데 스타일이 낡은 것을 칠하라는 요청을 받는다(고치면 그 줄이 다시 낡는다). 그래서
스타일 일은 글의 길이가 아니라 보이는 곳을 따른다. `gates_editor_find` 는 다음 일치를 선택하고, 표시
(`gates_editor_mark_add`)는 주변 글이 바뀌어도 자리를 지킨다.
입력기는 캐럿에서 조합하고 조합 중인 글은 밑줄로 그려지며, 결과는 한 번의 편집으로 들어온다. 화면
낭독기는 편집기의 글을 글자, 단어, 줄 단위로 읽고(줄은 보이는 대로, 곧 감긴 행이다
(`gates_access_text_line`); 문단은 글의 줄이다), 범위는 행마다 사각형 하나로 받는다
(`gates_access_text_rects`).

<!-- example: manual/examples/ex_06_highlight.c -->
```c
/* manual example (host): highlighting through a styler, and find.
 * expect: styled up to line 11 of 200; "TODO" found on line 150 */
#include <gates/gates.h>

#include <stdio.h>
#include <string.h>

enum { PLAIN, NUMBER, KEYWORD };

/* The program's highlighter: numbers and the word "TODO". It only styles the
 * range it is given - the editor asks for what is about to show. */
static gates_u32 styled_to;
static void highlight(void *user, gates_text_buffer_t *text, gates_u32 from, gates_u32 to) {
    (void)user;
    gates_text_buffer_set_style(text, from, to, PLAIN);
    for (gates_u32 i = from; i < to; i++) {
        gates_u8 c = gates_text_buffer_byte(text, i);
        if (c >= '0' && c <= '9') gates_text_buffer_set_style(text, i, i + 1, NUMBER);
    }
    gates_u32 at = from;
    while (gates_text_buffer_find(text, at, GATES_STR("TODO"), 0, &at) && at + 4 <= to) {
        gates_text_buffer_set_style(text, at, at + 4, KEYWORD);
        at += 4;
    }
    styled_to = to;
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    static char text[8000];
    int n = 0;
    for (int i = 1; i <= 200; i++) n += snprintf(text + n, sizeof text - (size_t)n, i == 150 ? "TODO %d\n" : "step %d\n", i);
    gates_node_t ed;
    gates_editor_desc_t desc = { .rows = 10, .line_numbers = true, .wrap = true };
    const gates_editor_style_t styles[] = { {0}, { .token = GATES_COLOR_FOCUS_RING }, { .token = GATES_COLOR_ERROR } };
    if (!gates_is_ok(gates_editor_create(t, gates_tree_root(t), &desc, &ed)) ||
        !gates_is_ok(gates_editor_set_text(t, ed, (gates_str_t){ (const gates_u8 *)text, (gates_usize_t)n })) ||
        !gates_is_ok(gates_editor_set_styles(t, ed, styles, 3)) ||
        !gates_is_ok(gates_editor_set_styler(t, ed, highlight, nullptr)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 320, 200 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* Painting styles only the lines shown (and the next one). */
    gates_draw_list_t dl;
    if (!gates_is_ok(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0)) ||
        !gates_is_ok(gates_paint_tree(t, &dl, gates_theme_light(), gates_text_backend_builtin()))) {
        return 1;
    }
    const gates_text_buffer_t *buf = gates_editor_buffer(t, ed);
    printf("styled up to line %u of %u; ", gates_text_buffer_line_of(buf, styled_to), gates_text_buffer_line_count(buf) - 1);

    /* Find selects the match and scrolls to it. */
    if (gates_editor_find(t, ed, GATES_STR("todo"), GATES_FIND_IGNORE_CASE, true)) {
        gates_u32 caret = 0;
        gates_editor_selection(t, ed, nullptr, &caret);
        printf("\"TODO\" found on line %u\n", gates_text_buffer_line_of(buf, caret) + 1);
    }
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
    return 0;
}
```

## 큰 글

`gates/text_buffer.h` 는 도구가 고치는 길이의 UTF-8 글(1 GiB 까지)을 담는다. 줄 시작 색인이 딸린 간격
버퍼, 바이트마다 스타일 바이트, 편집을 따라 움직이는 표시, 찾기가 있다. 읽기는 간격을 옮기지 않고 범위를
많아야 두 조각으로 돌려주므로, 프로그램은 다른 곳을 고치면서 보이는 줄을 읽을 수 있다. 여러 줄 편집기가
이 위에 서고, 프로그램이 큰 글을 불러오고 찾고 고칠 때 따로 쓸 수도 있다.
