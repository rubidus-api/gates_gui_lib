# 10장 - 배포와 문제 해결

헤더: `gates/app.h`, `gates/window.h`, `gates/version.h`.

## 링크

| 대상 | 링크 줄 |
|---|---|
| Windows 프로그램 | `-lgates -lproven -lgdi32 -luser32 -limm32 -ldwmapi -ladvapi32 -luiautomationcore -lole32 -loleaut32 -luuid`, 콘솔 없는 프로그램이면 `-mwindows` 도 |
| 창 없음(어떤 시스템이든) | `-lgates_core -lproven -lm` |

C23 컴파일러(`-std=c23`. GCC 14 이상, Clang 18 이상, 또는 GCC 14 이상의 mingw-w64)와
`-I<패키지>/include` 로 컴파일한다. 패키지의 `examples/consumer/` 는 패키지만으로 빌드하는 완전한
프로그램과 Makefile 이다. 원본 트리에서 `make install PREFIX=/some/dir` 은 헤더와 라이브러리를
`PREFIX/include` 와 `PREFIX/lib` 에 복사한다.

결과는 실행 파일 하나다. 라이브러리는 정적이고, gates 는 대상 기계에 Windows 10 이나 11 말고는 아무것도
요구하지 않는다(쓰는 UI Automation, 입력기, DPI 함수는 Windows 의 일부이고, 더 새 것은 실행 중에
찾는다).

## 판과 호환

`gates/version.h` 는 `GATES_VERSION_STRING` 과 `GATES_VERSION_NUMBER` 를 주고, `gates_version()` 은
링크한 라이브러리가 무엇인지 말한다. 0.1.x 릴리스끼리는 소스가 호환된다. 0.1.0 으로 빌드한 프로그램은
0.1.1 로도 빌드된다. 바이너리 호환은 약속하지 않는다. 공개 구조체의 크기가 바뀔 수 있으므로 판이 바뀔
때마다 프로그램을 다시 빌드하고, 시작할 때 확인한다.

```c
if (gates_version() != GATES_VERSION_NUMBER) { /* headers and library differ */ }
```

## 응용과 창

`gates_app_create` 가 응용을 만든다(보통 프로그램에 하나, UI 스레드에서. 그 스레드를 UI Automation
을 위해 COM 단일 스레드 아파트로 초기화한다). `gates_window_create` 는 자기 트리를 가진 창을 연다.
`gates_app_run` 은 마지막 창이 닫히거나 `gates_app_quit` 이 불릴 때까지 루프를 돈다. 창을 없애고 나서
응용을 없앤다. `gates_app_desc_t` 의 할당기가 gates 가 할당하는 모든 것에 쓰인다(기본 할당기는
스레드에 안전하다).

## 문제 해결

| 증상 | 까닭 |
|---|---|
| 처리기가 불리지 않는다 | 처리기를 달지 않았거나 프로그램이 바꾼 것이다(설정 함수는 조용하다. `gates_widget_notify` 를 쓴다) |
| 방금 쓴 노드에 `PROVEN_ERR_INVALID_ARG` 가 온다 | 핸들이 낡았다. 노드가 없어졌다(그 칸이 다시 쓰였을 수도 있다) |
| 동작에서 `PROVEN_ERR_INVALID_STATE` 가 온다 | 컨트롤이 꺼졌거나, 숨었거나, 모달 대화상자 뒤에 있다 |
| 입력한 글이 나타나지 않는다 | 텍스트 상자가 읽기 전용이거나 입력이 최대 길이를 넘었다(LIMIT_EXCEEDED 가 답을 기다린다) |
| IME 가 열리지 않는다 | 상자가 읽기 전용이거나 비밀번호 상자다 |
| 화면이 아주 작거나 크다 | 크기를 논리 단위가 아니라 픽셀로 주었다(8장) |
| 작업 스레드의 결과가 끊긴다 | 작업 스레드가 `GATES_POST_FULL` 을 무시했거나 `GATES_POST_CLOSED` 뒤에도 보냈다 |
| 내레이터가 이름 없이 "편집"이라고 읽는다 | 필드 옆의 이름표가 묶이지 않았다(9장) |
| 링크할 때 `proven_*` 기호가 겹친다 | 프로그램이 proven 사본 둘을 링크한다. 하나만 남긴다 |
