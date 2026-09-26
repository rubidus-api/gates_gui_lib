# 7장 - 작업 스레드와 타이머

헤더: `gates/post.h`, `gates/timer.h`.

## 작업 스레드는 트리를 만지지 않는다

트리는 UI 스레드의 것이다. 작업 스레드(파일 읽기, 장치와 이야기하기, 계산)는 결과를 메시지로 보내서
넘긴다. 프로그램은 작업 스레드를 시작하기 전에 UI 스레드에서 응용의 송신자(sender,
`gates_app_sender`)를 받고 대상을 만든다. 대상은 트리와 결과를 받을 노드다(`gates_target`). 작업
스레드는 종류와 짐(payload)을 담은 메시지를 보낸다. UI 스레드는 안전한 시점에 그 노드의 메시지
처리기(`gates_node_set_message_handler`)에 전달하고 짐을 놓아 준다.

<!-- example: manual/examples/ex_08_worker.c -->
```c
/* manual example (windows): a worker thread feeds a log through the sender.
 * The worker never touches the tree; it posts lines, and the UI thread
 * delivers them to the log's message handler. */
#include <gates/gates.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum { MSG_LINE = 1 };

typedef struct job_t {
    gates_sender_t *sender;      /* retained for the worker */
    gates_target_t target;       /* built on the UI thread */
} job_t;

static void release_line(void *payload, void *ctx) {
    (void)ctx;
    free(payload); /* may run on any thread: only frees */
}

static DWORD WINAPI worker(void *arg) {
    job_t *job = arg;
    for (int i = 1; i <= 1000; i++) {
        char *line = malloc(40);
        if (line == nullptr) break;
        int n = snprintf(line, 40, "line %d from the worker", i);
        gates_message_t msg = { .target = job->target, .kind = MSG_LINE, .payload = line,
                                .bytes = (gates_usize_t)n, .release = release_line };
        gates_err_t err = gates_sender_post(job->sender, &msg);
        if (err == GATES_POST_FULL) { free(line); Sleep(5); i--; continue; } /* the caller keeps it */
        if (err == GATES_POST_CLOSED) { free(line); break; }                 /* the app is closing */
        if (!gates_is_ok(err)) { free(line); break; }
    }
    gates_sender_release(job->sender);
    return 0;
}

static void on_line(gates_tree_t *tree, gates_node_t node, gates_u32 kind, void *payload,
                    gates_usize_t bytes, void *user) {
    (void)user;
    if (kind == MSG_LINE) (void)gates_log_append(tree, node, (gates_str_t){ .ptr = payload, .size = bytes });
}

int main(void) {
    gates_app_t *app = nullptr;
    if (!gates_is_ok(gates_app_create(&(gates_app_desc_t){0}, &app))) return 1;
    gates_window_t *win = nullptr;
    gates_window_desc_t desc = { .title = GATES_STR("Worker"), .size = { 420, 300 } };
    if (!gates_is_ok(gates_window_create(app, &desc, nullptr, &win))) {
        gates_app_destroy(app);
        return 1;
    }
    gates_tree_t *t = gates_window_tree(win);
    gates_node_t root = gates_tree_root(t), log;
    job_t job = {0};
    gates_err_t err = gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN);
    if (gates_is_ok(err)) err = gates_log_create(t, root, &(gates_log_desc_t){ .max_lines = 500 }, &log);
    if (gates_is_ok(err)) err = gates_layout_set_child_grow(t, log, 1);
    if (gates_is_ok(err)) err = gates_node_set_access_name(t, log, GATES_STR("Worker output"));
    if (gates_is_ok(err)) err = gates_node_set_message_handler(t, log, on_line, nullptr);
    if (gates_is_ok(err)) err = gates_app_sender(app, &job.sender);
    HANDLE thread = nullptr;
    if (gates_is_ok(err)) {
        job.target = gates_target(t, log);
        thread = CreateThread(nullptr, 0, worker, &job, 0, nullptr);
        if (thread == nullptr) gates_sender_release(job.sender);
    }
    if (gates_is_ok(err)) err = gates_app_run(app);
    gates_window_destroy(win);
    gates_app_destroy(app); /* closes the sender: the worker sees CLOSED and stops */
    if (thread != nullptr) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    return gates_is_ok(err) ? 0 : 1;
}
```

이것이 안전한 까닭은 다음 규칙이다.

- 보내기는 막히지도 할당하지도 않는다. 대기열이 차면 `GATES_POST_FULL` 이 온다. 작업 스레드는 짐을
  그대로 갖고 나중에 다시 하거나, 버리거나, 다음 것에 합친다. 대기열이 닫혔으면
  `GATES_POST_CLOSED` 가 온다. 응용이 끝나는 중이니 멈추고 송신자를 놓는다.
- 보내기가 성공하면 짐은 gates 의 것이 되고 정확히 한 번 놓인다. 전달한 뒤, 대상 노드가 없어졌을 때,
  더 새 스냅숏이 대신할 때(`replaceable`), 또는 끝날 때다. 보내기가 실패하면 짐은 여전히 작업
  스레드의 것이다.
- 놓아 주는 함수는 어느 스레드에서든 돌 수 있다. 그 함수는 풀기만 한다.
- 대기열에는 한도가 있고(기본 메시지 1024 개, 짐 1 MiB, `gates_app_desc_t`), 한 차례에 많아야
  64 개만 전달하므로 메시지가 쏟아져도 입력과 그리기가 굶지 않는다.
- 송신자는 마지막 참조가 놓일 때까지, 응용이 사라진 뒤에도, 모든 스레드에서 유효하다.

## 타이머

타이머는 노드에 속하며, 정한 간격 뒤에 UI 스레드에서 콜백을 한 번 또는 되풀이해 부른다. 취소는
즉시다. 취소한 타이머는 다시 울리지 않고, 노드나 트리를 없애면 취소된다. 늦은 되풀이 타이머는 한 번
울리고 지금부터 다시 잡힌다. 몰아서 울리지 않는다. 한가한 창은 플랫폼 타이머를 하나도 돌리지
않는다.

창의 트리에는 시계가 있다. 맨 트리(시험)는 프로그램에게서 시계를 받고, 시간이 언제 흐를지는
프로그램이 정한다.

<!-- example: manual/examples/ex_08_timers.c -->
```c
/* manual example (host): a repeating timer, driven by a test clock.
 * expect: 3 ticks, then cancelled */
#include <gates/gates.h>

#include <stdio.h>

static gates_u64 now_ms;
static gates_u64 clock_now(void *ctx) { (void)ctx; return now_ms; }

static void on_tick(gates_tree_t *tree, gates_node_t node, gates_timer_id_t id, void *user) {
    (void)node;
    int *ticks = user;
    if (++*ticks == 3) (void)gates_timer_cancel(tree, id); /* a callback may cancel itself */
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    /* A window's tree has a clock; a bare tree gets one from the program. */
    gates_tree_set_clock(t, clock_now, nullptr, nullptr);
    int ticks = 0;
    gates_timer_id_t id;
    if (!gates_is_ok(gates_timer_start(t, gates_tree_root(t), 100, true, on_tick, &ticks, &id))) return 1;
    for (int step = 0; step < 10; step++) {
        now_ms += 100;
        (void)gates_tree_run_timers(t); /* the window does this when its timer fires */
    }
    printf("%d ticks, then %s\n", ticks, gates_timer_active(t, id) ? "still running" : "cancelled");
    gates_tree_destroy(t);
    return 0;
}
```

움직임을 보이기 전에 `gates_window_reduced_motion` 을 묻는다. 사람이 애니메이션 효과를 꺼 두었으면
끝 상태를 바로 보인다.
