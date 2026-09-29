# Chapter 12 - Numbers, groups and layouts

Headers: `gates/inputs.h`, `gates/event.h`.

## One handler for many controls

A calculator has twenty buttons; a settings page has a dozen fields that all mean "something
changed". `gates_node_set_bubble_handler(tree, node, fn, user)` puts one handler on a container
- a panel, a form, the root - and it hears the events of every descendant that has no handler
of its own, with `ev->source` naming the control. Only the nearest such container hears an
event, and an overlay is a root of its own: a dialog's buttons never reach the window's
handler, only a handler inside the dialog.

## Once per turn: deferred calls

Some work should follow a burst of changes, not each of them: a total over many fields, a
filter over a list. `gates_tree_defer(tree, key, fn, user)` asks for `fn` to run once at the
next safe point - after the events queued now are delivered - however often it is asked
before then; the key names the job (`gates_tree_cancel_defer` drops it). A job asked for while
deferred jobs run waits for the next turn, so a job cannot keep the program busy.

## Spin box and slider

Both edit an integer in a range (`gates_range_t`: `min`, `max`, `step`, `page`, `value`). A
decimal quantity uses a `scale`: with scale 100 the value 125 is shown as `1.25`, so money and
measurements never meet floating-point rounding. A person's change reports
GATES_EVENT_VALUE_CHANGED with `ev->value`; `gates_range_set_value` changes the value silently
and `gates_range_set` changes the limits.

`gates_spin_create` makes a text box with up/down arrows. Up and Down step, PgUp and PgDn page,
and the arrows step with a click; Left, Right, Home and End stay with the text. While typed text
is not a number in range the box is marked invalid; Enter or leaving the box commits it, and
text that is still not a number in range goes back to the value. `gates_spin_box` returns the
text box, for a label's target or a width.

`gates_slider_create` makes a track with a thumb, horizontal or vertical (up is more). Arrows
step, PgUp and PgDn page, Home and End go to the ends, dragging the thumb moves in whole steps,
and a press on the track pages toward the pointer. `gates_slider_set_ticks` draws a mark every
n steps.

`gates_range_format` and `gates_range_parse` convert values and text the way the spin box does
(`,` is read as `.`).

<!-- example: manual/examples/ex_12_numbers.c -->
```c
/* manual example (host): a spin box and a slider, one bubble handler, one deferred total.
 * expect: width 2.50, volume 30; the panel heard 2 reports, the total was computed 1 time: 2.50 x 30 = 75.00 */
#include <gates/gates.h>

#include <stdio.h>

typedef struct app_t {
    gates_node_t width, volume;
    int heard, totals;
    char total[128];
} app_t;

/* Runs once after all the changes of a turn, however many there were. */
static void recompute(gates_tree_t *tree, gates_u32 key, void *user) {
    (void)key;
    app_t *a = user;
    a->totals++;
    char w[32];
    gates_i64 wv = gates_range_value(tree, a->width), vv = gates_range_value(tree, a->volume);
    (void)gates_range_format(wv, 100, w, sizeof w);
    char t[32];
    (void)gates_range_format(wv * vv, 100, t, sizeof t);
    snprintf(a->total, sizeof a->total, "%s x %lld = %s", w, (long long)vv, t);
}

/* One handler on the panel hears every control in it (ev->source says which). */
static void on_change(gates_tree_t *tree, const gates_event_t *ev, void *user) {
    app_t *a = user;
    if (ev->kind != GATES_EVENT_VALUE_CHANGED) return;
    a->heard++;
    (void)gates_tree_defer(tree, 1, recompute, a);
}

static bool key(gates_tree_t *t, gates_key_t k) {
    gates_key_event_t e = { .key = k, .down = true };
    return gates_input_key(t, &e);
}

int main(void) {
    gates_tree_t *t = nullptr;
    if (!gates_is_ok(gates_tree_create(&(gates_tree_desc_t){0}, &t))) return 1;
    gates_node_t root = gates_tree_root(t);
    app_t a = {0};
    /* Width in hundredths: 1.50 to 5.00 in steps of 0.25. */
    gates_range_t width = { .min = 150, .max = 500, .step = 25, .value = 200, .scale = 100 };
    gates_range_t volume = { .min = 0, .max = 100, .step = 5, .page = 20, .value = 10 };
    if (!gates_is_ok(gates_layout_set(t, root, GATES_LAYOUT_KIND_COLUMN)) ||
        !gates_is_ok(gates_spin_create(t, root, &width, &a.width)) ||
        !gates_is_ok(gates_slider_create(t, root, &volume, false, &a.volume)) ||
        !gates_is_ok(gates_node_set_bubble_handler(t, root, on_change, &a)) ||
        !gates_is_ok(gates_layout_run(t, (gates_size_t){ 400, 200 }, gates_text_backend_builtin()))) {
        return 1;
    }
    /* The person: two steps up in the spin box, a page up on the slider. */
    gates_tree_set_focus(t, gates_spin_box(t, a.width));
    (void)key(t, GATES_KEY_UP);
    (void)key(t, GATES_KEY_UP);
    gates_tree_set_focus(t, a.volume);
    (void)key(t, GATES_KEY_PAGE_UP);
    (void)gates_tree_dispatch_events(t, 0);

    char w[32];
    (void)gates_range_format(gates_range_value(t, a.width), 100, w, sizeof w);
    /* The spin box's two steps coalesce into one report of the latest value. */
    printf("width %s, volume %lld; the panel heard %d reports, the total was computed %d time: %s\n", w,
           (long long)gates_range_value(t, a.volume), a.heard, a.totals, a.total);
    gates_tree_destroy(t);
    return 0;
}
```

## Accessibility

A spin box is a Spinner with a RangeValue in its integer units, and its text box is an edit
named after it; a slider is a Slider with a RangeValue. Screen readers and automation can set
either value; it is clamped into the range and reported like a person's change.
