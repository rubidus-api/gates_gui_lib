# 13장 - 이미지와 네이티브 대화 상자

헤더: `gates/image.h`, `gates/draw.h`, `gates/window.h`.

## 이미지

트리는 이미지를 id로 보관한다. `gates_image_add_rgba(tree, w, h, pixels, stride, &id)`는 곧은 알파 RGBA 픽셀을 복사해 넣고, `gates_image_load_file`과 `gates_image_load_memory`는 플랫폼이 설치한 디코더로 PNG, JPEG, BMP, GIF, ICO, TIFF를 푼다(Windows 창에는 모두 있고, 창 없는 트리는 UNSUPPORTED로 답한다). `gates_image_remove`는 이미지를 풀어 준다. 그 이미지를 보여 주던 노드는 아무것도 보이지 않으며, id는 다시 쓰이지 않는다.

이미지의 기본 크기는 픽셀 크기를 논리 단위로 본 것이다. 그래서 32 x 16 그림은 32 x 16 단위를 차지하고 글자처럼 창과 함께 커진다. 150 %에서는 48 x 24 픽셀에 부드럽게(쌍선형) 그려진다. 큰 배율에서도 선명해야 하는 그림에는 더 큰 픽셀을 준다.

`gates_image_create(tree, parent, id, &node)`는 이미지를 보여 준다. `gates_image_node_set_size`는 다른 크기를 주며, 그때 그림은 가로세로 비율을 지키고 가운데에 놓인다. 접근성 이름(`gates_node_set_access_name`)이 있는 이미지는 화면 낭독기에 Image이고, 이름이 없으면 장식이라 읽히지 않는다.

## 아이콘

아이콘은 16 x 16 단위로 그려진다(`GATES_ICON_SIZE`). `gates_button_set_icon`은 버튼 레이블 왼쪽에 아이콘을 둔다. 아이콘만 있고 글자가 없는 버튼은 아이콘 버튼이며 접근성 이름이 있어야 한다. `gates_command_set_icon`은 명령에 아이콘을 준다. 메뉴는 여백 칸에 그리고(체크된 명령은 대신 표시를 보인다), 도구 막대는 레이블 왼쪽에 그리거나 `gates_toolbar_set_icons_only`로 아이콘만 그린다. 그때 레이블은 툴팁과 접근성 이름에 남는다.

직접 그릴 때는 `gates_tree_image`가 `gates_draw_image(dl, rect, image)`에 쓸 이미지를 돌려준다. 그리기 목록은 그것을 빌려 쓰므로 목록을 그릴 때까지 이미지를 두어야 한다.

<!-- example: manual/examples/ex_13_images.c -->
```c
/* manual example (host): an image from pixels, an icon on a button and on a command.
 * expect: logo 32x16, drawn 32x16 at 192 dpi as 64x32 pixels; Save button 20 units wider with its icon */
#include <gates/gates.h>

#include <stdio.h>

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t), logo, save;
    /* 32 x 16 pixels, a left-to-right fade from blue to transparent. */
    static gates_u8 px[16][32][4];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++) {
            px[y][x][0] = 0; px[y][x][1] = 90; px[y][x][2] = 200; px[y][x][3] = (gates_u8)(255 - x * 8);
        }
    }
    /* A 16 x 16 icon: a grey square. */
    static gates_u8 ic[16][16][4];
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            ic[y][x][0] = ic[y][x][1] = ic[y][x][2] = 90; ic[y][x][3] = 255;
        }
    }
    gates_image_id_t pic = 0, icon = 0;
    if (!gates_is_ok(gates_image_add_rgba(t, 32, 16, &px[0][0][0], 0, &pic)) ||
        !gates_is_ok(gates_image_add_rgba(t, 16, 16, &ic[0][0][0], 0, &icon)) ||
        !gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) ||
        !gates_is_ok(gates_image_create(t, root, pic, &logo)) ||
        !gates_is_ok(gates_node_set_access_name(t, logo, GATES_STR("Logo"))) ||   /* named: an Image */
        !gates_is_ok(gates_layout_set_child_align(t, logo, GATES_ALIGN_START_V)) ||
        !gates_is_ok(gates_button_create(t, root, GATES_STR("Save"), nullptr, nullptr, &save)) ||
        !gates_is_ok(gates_layout_set_child_align(t, save, GATES_ALIGN_START_V)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 200, 100 }, gates_text_backend_builtin()))) {
        return 1;
    }
    gates_i32 plain = gates_node_preferred_size(t, save).w;
    if (!gates_is_ok(gates_button_set_icon(t, save, icon)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 200, 100 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* Render at 192 dpi: the logo covers 64 x 32 device pixels. */
    gates_draw_list_t dl;
    static gates_u32 pixels[200 * 2 * 100 * 2];
    gates_pixels_t target = { .ptr = pixels, .w = 400, .h = 200, .stride_bytes = 1600 };
    if (!gates_is_ok(gates_draw_list_init(&dl, (gates_allocator_t){0}, 0)) ||
        !gates_is_ok(gates_paint_tree(t, &dl, gates_theme_light(), gates_text_backend_builtin())) ||
        !gates_is_ok(gates_render_soft_scaled(&dl, target, gates_text_backend_builtin(), 192))) {
        return 1;
    }
    gates_rect_t r = gates_node_layout_rect(t, logo), dev = gates_rect_px(r, 192);
    printf("logo %dx%d, drawn %dx%d at 192 dpi as %dx%d pixels; Save button %d units wider with its icon\n",
           gates_image_size(t, pic).w, gates_image_size(t, pic).h, r.w, r.h, dev.w, dev.h,
           gates_node_preferred_size(t, save).w - plain);
    gates_draw_list_deinit(&dl);
    gates_tree_destroy(t);
    return 0;
}
```

## 네이티브 대화 상자

파일, 폴더, 색을 고르는 일과 예/아니오를 묻는 일은 플랫폼에 맡기는 것이 가장 좋다. 사람들은 그 대화 상자를 알고, 그것은 플랫폼의 키보드 지원, 화면 낭독기 지원, 최근 위치를 함께 가져온다. Windows에서는 다음과 같다.

- `gates_window_open_file`, `gates_window_save_file`, `gates_window_choose_folder`는 `gates_file_dialog_t`(제목, `"Pictures|*.png;*.jpg|All files|*.*"` 같은 필터, 시작 폴더, 저장할 때의 추천 이름)를 받아 고른 경로를 UTF-8로 버퍼에 적는다. 저장 대화 상자는 덮어쓰기 전에 묻는다.
- `gates_window_choose_color`는 한 색에서 시작해 고른 색을 답한다.
- `gates_window_message`는 확인, 확인/취소, 예/아니오, 예/아니오/취소 버튼과 정보, 경고, 오류, 질문 아이콘으로 메시지를 보이고 답을 돌려준다(Escape와 닫기 상자는 취소로 답한다).

이것은 플랫폼의 모달 대화 상자이다. 호출은 사람이 답할 때 돌아오고, 창의 메뉴는 먼저 닫힌다. 취소는 오류가 아니다. 경로가 비어서 돌아오거나 `chosen`이 거짓이다. 명령이나 이벤트 처리기에서 부르고, 그리는 도중에는 부르지 않는다.

