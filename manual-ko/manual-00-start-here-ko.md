# 0장 - 여기서부터

## gates 는 무엇을 위한 것인가

gates 는 사람이 무언가를 보고 바꾸게 하는 것이 일인 프로그램을 위한 GUI 라이브러리다. 설정 창,
기록 살펴보기, 로그 보기, 빌드 도구가 그런 프로그램이다. C23 으로 쓰였고, 화면을 설명하는 노드
트리를 유지하며, 이런 프로그램이 저마다 서툴게 다시 만들던 일을 맡는다. 키보드 포커스, 설치된
입력기로 하는 글자 입력, 폼, 큰 데이터 위의 목록, 버튼과 단축키가 함께 쓰는 명령, 대화상자,
화면에 결과를 넘기는 스레드, 테마, 배율, 접근성이 그 일이다.

브라우저 엔진도, 게임 UI 도, 픽셀까지 똑같이 그리는 도구도 아니다. HTML 과 CSS 를 떠올리면 된다.
프로그램은 무엇이 무엇이며 서로 어떤 관계인지를 말하고, 마지막 모습은 백엔드와 테마가 정한다.
gates 에서 기능이 끝났다는 것은 그 기능이 섬기는 상호작용이 동작한다는 뜻이다. 닿을 수 있고,
포인터와 키보드와 입력기로 쓸 수 있고, 화면 읽기 프로그램이 읽을 수 있어야 한다. 그림과 같아
보이는 것은 기준이 아니다.

0.13.0 의 백엔드는 하나, 소프트웨어 그리기를 쓰는 Win32 다. 창을 뺀 나머지 전부인 코어는 플랫폼에
묶이지 않아서 C23 컴파일러가 있는 곳이면 어디서나 돌고, 시험도 그렇게 돈다.

## 패키지

릴리스는 `gates-0.13.0/` 폴더 하나다.

| 경로 | 무엇 |
|---|---|
| `include/gates/` | 공개 헤더. `gates/gates.h` 가 전부를 포함한다 |
| `include/proven/` | gates 가 기반으로 삼는 proven 라이브러리의 헤더 |
| `lib/win64/libgates.a` | Windows 용 gates(코어 + Win32 백엔드), mingw-w64 |
| `lib/host/libgates_core.a` | 플랫폼 없는 코어. 시험과 창 없는 도구용 |
| `lib/*/libproven.a` | 기반 라이브러리 proven(할당기, 문자열, 결과) |
| `manual/`, `manual-ko/` | 이 매뉴얼의 영문판과 한국어판 |
| `examples/consumer/` | 패키지만으로 빌드하는 프로그램과 그 Makefile |

gates 와 proven 은 따로 된 라이브러리다. 둘 다 링크하되 gates 를 먼저 둔다. 이미 proven(0.1.x)을
쓰는 프로그램은 패키지의 것 대신 자기 사본을 링크해도 된다.

## 첫 프로그램

이름표 하나와 버튼 하나가 있는 창이다. 버튼을 누르면 이름표가 바뀐다.

<!-- example: manual/examples/ex_00_hello.c -->
```c
/* manual example (windows): the first program - a window, a label, a button.
 * Build: see chapter 0 (links -lgates -lproven and the Windows libraries). */
#include <gates/gates.h>

#include <stdio.h>

typedef struct hello_t {
    gates_app_t *app;
    gates_tree_t *tree;
    gates_node_t count_label;
    int clicks;
} hello_t;

/* The button tells us it was pressed; we change the label. Nothing is read
 * back from widgets while painting. */
static void on_button(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    hello_t *h = user;
    if (ev->kind != GATES_EVENT_ACTIVATED) return;
    h->clicks++;
    char text[48];
    int n = snprintf(text, sizeof text, "Pressed %d time%s", h->clicks, h->clicks == 1 ? "" : "s");
    (void)gates_widget_set_text(tree, h->count_label, (gates_str_t){ .ptr = (const gates_u8 *)text, .size = (gates_usize_t)n });
}

static void on_key(gates_window_t *win, const gates_key_event_t *ev, void *user) {
    (void)win;
    hello_t *h = user;
    if (ev->down && ev->key == GATES_KEY_ESCAPE) gates_app_quit(h->app);
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) return 1;
    hello_t h = { .app = app };
    gates_window_desc_t desc = { .title = GATES_STR("Hello, gates"), .size = { 320, 160 } };
    gates_window_callbacks_t cb = { .on_key = on_key, .user_data = &h };
    gates_window_t *win = nullptr;
    if (!gates_is_ok(gates_window_create(app, &desc, &cb, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    h.tree = gates_window_tree(win);
    gates_node_t root = gates_tree_root(h.tree), button;
    gates_err_t err = gates_layout_set(h.tree, root, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_layout_set_padding(h.tree, root, 12);
    if (gates_is_ok(err)) err = gates_label_create(h.tree, root, GATES_STR("Not pressed yet"), &h.count_label);
    if (gates_is_ok(err)) err = gates_button_create(h.tree, root, GATES_STR("Press me"), nullptr, nullptr, &button);
    if (gates_is_ok(err)) err = gates_widget_set_handler(h.tree, button, on_button, &h);
    if (gates_is_ok(err)) err = gates_app_run(app); /* returns when the last window closes */
    gates_window_destroy(win);
    gates_app_destroy(app);
    return gates_is_ok(err) ? 0 : 1;
}
```

패키지 폴더에서 mingw-w64(Linux, 또는 Windows 의 MSYS2)로 빌드한다.

```sh
x86_64-w64-mingw32-gcc -std=c23 -O2 -Iinclude hello.c -Llib/win64 -lgates -lproven \
    -lgdi32 -luser32 -limm32 -ldwmapi -ladvapi32 -luiautomationcore -lole32 -loleaut32 \
    -luuid -mwindows -o hello.exe
```

gates 프로그램의 모양은 이 안의 세 가지가 전부다. 프로그램은 창의 트리에 노드를 만들어 넣는다
(`gates_label_create`, `gates_button_create`). 사람이 한 일은 이벤트로 알고
(`gates_widget_set_handler`), 창이 그리는 동안 위젯을 읽어서 알아내지 않는다. 그리고
`gates_app_run` 이 마지막 창이 닫힐 때까지 루프를 맡는다.

## 창 없이

코어에는 창이 필요 없다. 트리를 만들고, 내장 글자 백엔드로 배치하고, 무엇이 어디 있는지 묻는다.
시험과 명령줄 도구가 gates 를 이렇게 쓴다.

<!-- example: manual/examples/ex_00_headless.c -->
```c
/* manual example (host): gates without a window - build, lay out, inspect.
 * expect: Press me: button at 0,16 320x26 */
#include <gates/gates.h>

#include <stdio.h>

int main(void) {
    gates_tree_t *tree = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &tree))) return 1;
    gates_node_t root = gates_tree_root(tree), label, button;
    gates_err_t err = gates_layout_set(tree, root, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_label_create(tree, root, GATES_STR("Not pressed yet"), &label);
    if (gates_is_ok(err)) err = gates_button_create(tree, root, GATES_STR("Press me"), nullptr, nullptr, &button);
    /* Layout needs text metrics: the builtin backend works everywhere. */
    if (gates_is_ok(err)) err = gates_layout_run(tree, (gates_size_t){ 320, 160 }, gates_text_backend_builtin());
    gates_access_info_t info;
    if (gates_is_ok(err)) err = gates_access_info(tree, button, 0, &info);
    if (gates_is_ok(err)) {
        printf("%.*s: %s at %d,%d %dx%d\n", (int)info.name.size, (const char *)info.name.ptr,
               info.role == GATES_ROLE_BUTTON ? "button" : "?", info.bounds.x, info.bounds.y, info.bounds.w,
               info.bounds.h);
    }
    gates_tree_destroy(tree);
    return gates_is_ok(err) ? 0 : 1;
}
```

```sh
cc -std=c23 -Iinclude headless.c -Llib/host -lgates_core -lproven -lm -o headless
```

출력은 `Press me: button at 0,16 320x26` 이다. 세로 배치(column)가 버튼을 이름표 아래에 두고
너비 전체로 늘렸다.

## 헤더

`gates/gates.h` 가 전부를 포함한다. 이 매뉴얼에서 만나는 차례로 적으면 다음과 같다.

| 헤더 | 담긴 것 | 장 |
|---|---|---|
| `gates/types.h` | 정수, 문자열(`gates_str_t`), 오류, 할당기 | 1 |
| `gates/version.h` | 판과 `gates_version()` | 10 |
| `gates/tree.h` | 노드 트리, 핸들, 포커스, 숨긴 노드 | 1 |
| `gates/event.h` | 타입 있는 이벤트와 그 전달 | 1 |
| `gates/widget.h` | 이름표, 버튼, 체크박스, 텍스트 상자, 라디오 그룹, 선택 상자, 진행 막대 | 2 |
| `gates/layout.h` | 가로, 세로, 겹침, 나눔, 스크롤, 폼 배치 | 2 |
| `gates/geometry.h` | 점, 크기, 사각형, 논리 단위 | 2, 8 |
| `gates/form.h` | 이름표·도움말·오류 줄이 있는 폼 | 3 |
| `gates/command.h` | 버튼·단축키·메뉴가 함께 쓰는 명령 | 4 |
| `gates/overlay.h` | 모달 대화상자와 문맥 메뉴 | 4 |
| `gates/view.h` | 내 데이터 위의 목록, 표, 트리, 로그 | 5 |
| `gates/text_edit.h` | 모든 텍스트 상자 밑의 편집 코어 | 6 |
| `gates/clipboard.h` | 클립보드 경계 | 6 |
| `gates/input.h` | 포인터와 키 이벤트 | 6 |
| `gates/ui.h` | 입력 전달, 맞힘 판정(hit test), 트리 그리기 | 6 |
| `gates/post.h` | 작업 스레드에서 보내기 | 7 |
| `gates/timer.h` | UI 스레드의 타이머 | 7 |
| `gates/theme.h` | 테마: 색 토큰, 밝게, 어둡게, 고대비 | 8 |
| `gates/draw.h` | 그리기 명령 목록 | 8 |
| `gates/render.h` | 소프트웨어 그리기 | 8 |
| `gates/text.h` | 글꼴, 글자 치수, 글자 백엔드 | 8 |
| `gates/access.h` | 접근성: 역할, 이름, 동작, 감사 | 9 |
| `gates/app.h` | 응용과 그 루프 | 10 |
| `gates/window.h` | 창, 테마, 확대, 알림 | 10 |
