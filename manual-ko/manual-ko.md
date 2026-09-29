# gates 매뉴얼 (v0.3.0)

`gates` 의 매뉴얼이다. gates 는 도구형 Windows 프로그램(설정 창, 기록 살펴보기, 로그 보기,
빌드 도구)을 위한 C23 의 작은 유지형(retained) GUI 라이브러리다. 각 장은 `manual-ko/` 아래 파일
하나이며 영문판([`manual/`](../manual/manual.md))을 그대로 옮긴 것이다. 코드와 API 계약을 빌드가
검증하는 정본은 영문판이고, 두 판이 어긋나면 영문판을 따른다.

## 차례

- [0장 - 여기서부터](manual-00-start-here-ko.md): gates 가 무엇인가, 패키지, 첫 프로그램
- [1장 - 개념](manual-01-concepts-ko.md): 노드 트리, 핸들, 소유, 오류, 이벤트
- [2장 - 컨트롤과 배치](manual-02-controls-layout-ko.md): 컨트롤 표, 배치(layout)
- [3장 - 폼](manual-03-forms-ko.md): 이름표 달린 필드, 초안, 검사
- [4장 - 명령, 포커스, 대화상자, 메뉴](manual-04-commands-focus-ko.md)
- [5장 - 내 데이터 위의 뷰](manual-05-views-ko.md): 목록, 표, 트리, 로그
- [6장 - 글자 입력](manual-06-text-ko.md): 텍스트 상자, 설치된 IME, 한도, 유니코드
- [7장 - 작업 스레드와 타이머](manual-07-workers-timers-ko.md)
- [8장 - 테마, 단위, 배율](manual-08-themes-units-ko.md)
- [9장 - 접근성](manual-09-accessibility-ko.md): 모델, 규칙, UI Automation
- [10장 - 배포와 문제 해결](manual-10-deployment-ko.md)
- [11장 - 응용 프로그램 틀](manual-11-application-frame-ko.md): 니모닉, 메뉴 막대, 키맵, 도구 막대, 상태 줄, 툴팁, 탭, 상태 저장

## 읽는 법

0장과 1장을 먼저 읽는다. 그 뒤로는 지금 다루는 컨트롤이나 일의 장을 읽으면 된다. 여기 실린
프로그램은 모두 `manual/examples/` 의 파일이고, 빌드가 패키지를 상대로 컴파일한다. "host" 표시가
있는 프로그램은 실행까지 하며, 본문이 말하는 출력을 내야 한다(`make manual-check`). 본문과
프로그램이 어긋나면 프로그램이 맞고 본문이 결함이다.

## 판

- 라이브러리와 매뉴얼 판: 0.3.0. 각 장은 필요한 헤더를 적는다. 여기 나오는 것은 모두 0.3.0
  기능 범위("Windows Tool UI 1": Win32, 최상위 화면마다 창 하나, 소프트웨어 그리기)에 든다.
- 지은이: rubidus. 라이선스: MIT - Copyright (c) 2026 rubidus-api. 라이브러리와 이 매뉴얼은 같은
  라이선스를 따른다(패키지의 `LICENSE`).
