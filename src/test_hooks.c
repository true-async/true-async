/*
   +----------------------------------------------------------------------+
   | Copyright © TrueAsync contributors.                                  |
   +----------------------------------------------------------------------+
   | This source file is subject to the Modified BSD License that is      |
   | bundled with this package in the file LICENSE.                       |
   |                                                                      |
   | SPDX-License-Identifier: BSD-3-Clause                                |
   +----------------------------------------------------------------------+
   | Authors: Edmond <edmondifthen@proton.me>                             |
   +----------------------------------------------------------------------+
*/

/* PHP functions that drive internal structures no PHP API reaches yet, for tests under
 * tests/internal/. Built only with --enable-true-async-test-hooks, which every CI lane passes
 * (tools/test.py, the Windows ADD_CONF); a build without the flag has none of them.
 *
 * TrueAsync\Test\callbacks_scenario(string $name): string runs one scenario on the callbacks
 * vector and returns the names of the callbacks in the order they ran;
 * TrueAsync\Test\buffer_scenario(string $name): string runs one on the circular buffer and returns
 * what it popped and the sizes it saw;
 * TrueAsync\Test\fail_at(string $site): void arms a fault site of the scheduler. The rest reach the
 * core API a PHP script has no path to: defer() queues a microtask; add_throwing_finish_handler(),
 * add_clearing_finish_handler() and add_printing_switch_handler() add handlers to a coroutine;
 * enqueue_with_error() wakes one with an error. Each says more above its definition. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "zend_exceptions.h"
#include "zend_smart_str.h"
#include "php_true_async.h"
#include "test_hooks.h"
#include "coroutine.h"
#include "src/internal/circular_buffer.h"

/* A stand-in awaitable: the flags word and a vector, as the event header will have. */
typedef struct
{
	uint32_t flags;
	async_callbacks_vector_t callbacks;
} test_target_t;

typedef struct test_callback_s test_callback_t;

/* A named callback with one optional action run when it fires. */
struct test_callback_s
{
	async_event_callback_t event_callback;
	char name;
	void (*action)(test_callback_t *test_callback, async_awaitable_t *target);
	async_callbacks_vector_t *other_vector; /* the vector the action works on */
	test_callback_t *other_callback;        /* the callback the action works on */
	test_target_t *other_target;
	smart_str *trace;
	uint32_t runs;
};

static void
test_callback_fire(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	test_callback_t *test_callback = (test_callback_t *) callback;
	smart_str_appendc(test_callback->trace, test_callback->name);
	test_callback->runs++;

	/* Each callback runs with no exception pending, in scheduler context. */
	if (UNEXPECTED(EG(exception) != NULL)) {
		smart_str_appendc(test_callback->trace, '?');
	}

	if (UNEXPECTED(!ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		smart_str_appendc(test_callback->trace, '!');
	}

	if (test_callback->action != NULL) {
		test_callback->action(test_callback, target);
	}
}

/* Traces the callback's name in lower case as the vector disposes it. */
static void test_callback_dispose(async_event_callback_t *callback, async_awaitable_t *target)
{
	const test_callback_t *test_callback = (test_callback_t *) callback;
	smart_str_appendc(test_callback->trace, (char) (test_callback->name + 'a' - 'A'));
}

static void test_callback_init(test_callback_t *callback, const char name, smart_str *trace)
{
	memset(callback, 0, sizeof(*callback));
	callback->event_callback.callback = test_callback_fire;
	callback->name = name;
	callback->trace = trace;
}

static void test_callbacks_add(async_callbacks_vector_t *vector, async_event_callback_t *callback)
{
	async_callbacks_reserve(vector, 1);
	async_callbacks_push_reserved(vector, callback);
}

static void action_remove_other(test_callback_t *test_callback, async_awaitable_t *target)
{
	async_callbacks_remove(test_callback->other_vector, &test_callback->other_callback->event_callback);
}

static void action_remove_self(test_callback_t *test_callback, async_awaitable_t *target)
{
	async_callbacks_remove(test_callback->other_vector, &test_callback->event_callback);
}

static void action_add_other(test_callback_t *test_callback, async_awaitable_t *target)
{
	test_callbacks_add(test_callback->other_vector, &test_callback->other_callback->event_callback);
}

static void action_notify_other(test_callback_t *test_callback, async_awaitable_t *target)
{
	if (test_callback->other_vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING) {
		smart_str_appends(test_callback->trace, "(refused)");
	}

	async_callbacks_notify((async_awaitable_t *) test_callback->other_target, test_callback->other_vector, NULL, NULL);
}

static void action_throw(test_callback_t *test_callback, async_awaitable_t *target)
{
	const char message[2] = { (char) (test_callback->name + 'a' - 'A'), '\0' };
	zend_throw_exception(NULL, message, 0);
}

/* Bails out on its first run only. */
static void action_bailout_once(test_callback_t *test_callback, async_awaitable_t *target)
{
	if (test_callback->runs == 1) {
		zend_bailout();
	}
}

/* zend_bailout() clears the current frame and marks the shutdown unclean; the catch puts both
 * back, so the PHP code that called the hook goes on. */
static void action_notify_catching_bailout(test_callback_t *test_callback, async_awaitable_t *target)
{
	zend_execute_data *execute_data = EG(current_execute_data);
	const bool unclean_shutdown = CG(unclean_shutdown);

	zend_try
	{
		async_callbacks_notify(
				(async_awaitable_t *) test_callback->other_target, test_callback->other_vector, NULL, NULL);
	}
	zend_catch
	{
		EG(current_execute_data) = execute_data;
		CG(unclean_shutdown) = unclean_shutdown;
		smart_str_appends(test_callback->trace, "(caught)");
	}
	zend_end_try();
}

/* Appends the messages of EG(exception) and its previous chain, then clears it. */
static void test_trace_exception(smart_str *trace)
{
	smart_str_appends(trace, " caught:");

	for (zend_object *exception = EG(exception); exception != NULL;) {
		zval property_storage;
		zend_class_entry *exception_class = zend_get_exception_base(exception);
		zval *message = zend_read_property_ex(
				exception_class, exception, ZSTR_KNOWN(ZEND_STR_MESSAGE), true, &property_storage);
		zval *previous = zend_read_property_ex(
				exception_class, exception, ZSTR_KNOWN(ZEND_STR_PREVIOUS), true, &property_storage);

		smart_str_append(trace, Z_STR_P(message));
		exception = Z_TYPE_P(previous) == IS_OBJECT ? Z_OBJ_P(previous) : NULL;

		if (exception != NULL) {
			smart_str_appendc(trace, '<');
		}
	}

	zend_clear_exception();
}

static void test_vector_fill(async_callbacks_vector_t *vector, test_callback_t *callbacks, const uint32_t count)
{
	for (uint32_t i = 0; i < count; i++) {
		test_callbacks_add(vector, &callbacks[i].event_callback);
	}
}

/* A B C D; B removes A, which has already run: D and C still run once each. */
static void scenario_remove_run(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t callbacks[4];

	for (int i = 0; i < 4; i++) {
		test_callback_init(&callbacks[i], (char) ('A' + i), trace);
	}

	callbacks[1].action = action_remove_other;
	callbacks[1].other_vector = &target.callbacks;
	callbacks[1].other_callback = &callbacks[0];
	test_vector_fill(&target.callbacks, callbacks, 4);
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " length=%u", target.callbacks.length);
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
}

/* A B C; A removes itself: C and B run, A does not run again. */
static void scenario_remove_self(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t callbacks[3];

	for (int i = 0; i < 3; i++) {
		test_callback_init(&callbacks[i], (char) ('A' + i), trace);
	}

	callbacks[0].action = action_remove_self;
	callbacks[0].other_vector = &target.callbacks;
	test_vector_fill(&target.callbacks, callbacks, 3);
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	smart_str_appendc(trace, ' ');
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
}

/* One inline element that removes itself: the vector ends empty without an allocation. */
static void scenario_single_self(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t callback;

	test_callback_init(&callback, 'A', trace);
	callback.action = action_remove_self;
	callback.other_vector = &target.callbacks;
	test_callbacks_add(&target.callbacks, &callback.event_callback);
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " length=%u capacity=%u", target.callbacks.length, target.callbacks.capacity);
}

/* A B; A adds C during the notify: C runs in the same notify, after B. */
static void scenario_add_during(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t callbacks[3];

	for (int i = 0; i < 3; i++) {
		test_callback_init(&callbacks[i], (char) ('A' + i), trace);
	}

	callbacks[0].action = action_add_other;
	callbacks[0].other_vector = &target.callbacks;
	callbacks[0].other_callback = &callbacks[2];
	test_vector_fill(&target.callbacks, callbacks, 2);
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
}

/* A notifies its own vector again: refused, the outer notify goes on. */
static void scenario_nested_same(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t callbacks[2];

	for (int i = 0; i < 2; i++) {
		test_callback_init(&callbacks[i], (char) ('A' + i), trace);
	}

	callbacks[0].action = action_notify_other;
	callbacks[0].other_vector = &target.callbacks;
	callbacks[0].other_target = &target;
	test_vector_fill(&target.callbacks, callbacks, 2);
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
}

/* T holds R A B; R has run when A notifies F, whose W removes R from T. The removal corrects T's
 * cursor, not F's (the inner notify): T goes on with B, and F with X. */
static void scenario_nested_other(smart_str *trace)
{
	test_target_t outer = { ASYNC_AWAITABLE_F_EVENT };
	test_target_t inner = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t r, a, b, w, x;

	test_callback_init(&r, 'R', trace);
	test_callback_init(&a, 'A', trace);
	test_callback_init(&b, 'B', trace);
	test_callback_init(&w, 'W', trace);
	test_callback_init(&x, 'X', trace);
	a.action = action_notify_other;
	a.other_vector = &inner.callbacks;
	a.other_target = &inner;
	w.action = action_remove_other;
	w.other_vector = &outer.callbacks;
	w.other_callback = &r;
	test_callbacks_add(&outer.callbacks, &r.event_callback);
	test_callbacks_add(&outer.callbacks, &a.event_callback);
	test_callbacks_add(&outer.callbacks, &b.event_callback);
	test_callbacks_add(&inner.callbacks, &w.event_callback);
	test_callbacks_add(&inner.callbacks, &x.event_callback);
	async_callbacks_notify((async_awaitable_t *) &outer, &outer.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " length=%u", outer.callbacks.length);
	async_callbacks_free((async_awaitable_t *) &outer, &outer.callbacks);
	async_callbacks_free((async_awaitable_t *) &inner, &inner.callbacks);
}

/* A B C, entered with an exception pending; A and C throw: A runs with no exception pending and
 * ends the notify, B and C stay uncalled until the free, and the chain is a, then the entry
 * exception. */
static void scenario_throw_stops(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t callbacks[3];

	for (int i = 0; i < 3; i++) {
		test_callback_init(&callbacks[i], (char) ('A' + i), trace);
	}

	callbacks[0].action = action_throw;
	callbacks[2].action = action_throw;
	test_vector_fill(&target.callbacks, callbacks, 3);
	zend_throw_exception(NULL, "entry", 0);
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	test_trace_exception(trace);
	smart_str_append_printf(trace, " left=%u", target.callbacks.length);
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
}

/* T holds A B; A notifies F inside zend_try, and F's W bails out. A's catch swallows it: T goes on
 * with B, F stays marked and refuses the next notify, and the scheduler-context flag is back to its
 * value before T. */
static void scenario_bailout_caught(smart_str *trace)
{
	test_target_t outer = { ASYNC_AWAITABLE_F_EVENT };
	test_target_t inner = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t a, b, w;

	test_callback_init(&a, 'A', trace);
	test_callback_init(&b, 'B', trace);
	test_callback_init(&w, 'W', trace);
	a.action = action_notify_catching_bailout;
	a.other_vector = &inner.callbacks;
	a.other_target = &inner;
	w.action = action_bailout_once;
	test_callbacks_add(&outer.callbacks, &a.event_callback);
	test_callbacks_add(&outer.callbacks, &b.event_callback);
	test_callbacks_add(&inner.callbacks, &w.event_callback);
	async_callbacks_notify((async_awaitable_t *) &outer, &outer.callbacks, NULL, NULL);
	smart_str_appends(trace, " again:");
	async_callbacks_notify((async_awaitable_t *) &inner, &inner.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " sched=%d", (int) ZEND_ASYNC_IN_SCHEDULER_CONTEXT);
	async_callbacks_free((async_awaitable_t *) &outer, &outer.callbacks);
	async_callbacks_free((async_awaitable_t *) &inner, &inner.callbacks);
}

/* A notify entered in scheduler context leaves the flag set; one entered outside clears it. */
static void scenario_sched_kept(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t a;

	test_callback_init(&a, 'A', trace);
	test_callbacks_add(&target.callbacks, &a.event_callback);
	smart_str_appends(trace, "outside:");
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " sched=%d inside:", (int) ZEND_ASYNC_IN_SCHEDULER_CONTEXT);
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " sched=%d", (int) ZEND_ASYNC_IN_SCHEDULER_CONTEXT);
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
}

/* A B C D; A removes B, which has not run yet and sits at the cursor: B never runs, D and C do. */
static void scenario_remove_pending(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t callbacks[4];

	for (int i = 0; i < 4; i++) {
		test_callback_init(&callbacks[i], (char) ('A' + i), trace);
	}

	callbacks[0].action = action_remove_other;
	callbacks[0].other_vector = &target.callbacks;
	callbacks[0].other_callback = &callbacks[1];
	test_vector_fill(&target.callbacks, callbacks, 4);
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " length=%u", target.callbacks.length);
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
}

/* A B C D; A removes C, one past the cursor: C never runs, B and D do. */
static void scenario_remove_past_cursor(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t callbacks[4];

	for (int i = 0; i < 4; i++) {
		test_callback_init(&callbacks[i], (char) ('A' + i), trace);
	}

	callbacks[0].action = action_remove_other;
	callbacks[0].other_vector = &target.callbacks;
	callbacks[0].other_callback = &callbacks[2];
	test_vector_fill(&target.callbacks, callbacks, 4);
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " length=%u", target.callbacks.length);
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
}

/* A B C; removing D, never added, finds nothing and changes nothing. */
static void scenario_remove_absent(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t callbacks[4];

	for (int i = 0; i < 4; i++) {
		test_callback_init(&callbacks[i], (char) ('A' + i), trace);
	}

	test_vector_fill(&target.callbacks, callbacks, 3);
	smart_str_append_printf(trace,
							"removed=%d length=%u ran:",
							async_callbacks_remove(&target.callbacks, &callbacks[3].event_callback),
							target.callbacks.length);
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
}

/* A B C, never notified: freeing the vector disposes each of them once. */
static void scenario_free_disposes(smart_str *trace)
{
	test_target_t target = { ASYNC_AWAITABLE_F_EVENT };
	test_callback_t callbacks[3];

	for (int i = 0; i < 3; i++) {
		test_callback_init(&callbacks[i], (char) ('A' + i), trace);
		callbacks[i].event_callback.dispose = test_callback_dispose;
	}

	test_vector_fill(&target.callbacks, callbacks, 3);
	smart_str_appends(trace, "disposed:");
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
	smart_str_append_printf(trace, " length=%u", target.callbacks.length);
}

/* A coroutine for the switch handler scenarios: a switch handler gets only its coroutine, so the
 * trace it writes to sits beside it. */
typedef struct
{
	async_coroutine_t coroutine;
	smart_str *trace;
} test_switch_coroutine_t;

/* A switch handler traces its name; A, C, D and E stay registered, B goes after its first call. */
static bool switch_handler_traced(zend_coroutine_t *coroutine, const char name, const bool keep)
{
	smart_str_appendc(((test_switch_coroutine_t *) coroutine)->trace, name);

	return keep;
}

static bool switch_handler_a(zend_coroutine_t *coroutine, bool is_enter)
{
	return switch_handler_traced(coroutine, 'A', true);
}

static bool switch_handler_b(zend_coroutine_t *coroutine, bool is_enter)
{
	return switch_handler_traced(coroutine, 'B', false);
}

static bool switch_handler_c(zend_coroutine_t *coroutine, bool is_enter)
{
	return switch_handler_traced(coroutine, 'C', true);
}

static bool switch_handler_d(zend_coroutine_t *coroutine, bool is_enter)
{
	return switch_handler_traced(coroutine, 'D', true);
}

static bool switch_handler_e(zend_coroutine_t *coroutine, bool is_enter)
{
	return switch_handler_traced(coroutine, 'E', true);
}

/* Switch handlers A-E on one coroutine: the vector grows past its first four, an add of a handler
 * already there returns its id, a removal by id keeps the order of the others, a call keeps the
 * handlers that return true, and the vector goes with the last one. */
static void scenario_switch_handlers(smart_str *trace)
{
	test_switch_coroutine_t test_coroutine = { .trace = trace };
	async_coroutine_t *coroutine = &test_coroutine.coroutine;
	zend_coroutine_t *zend_coroutine = &coroutine->coroutine;

	smart_str_append_printf(trace, "remove-none=%d ", async_switch_handler_remove(zend_coroutine, 1));

	const uint32_t id_a = async_switch_handler_add(zend_coroutine, switch_handler_a);
	async_switch_handler_add(zend_coroutine, switch_handler_b);
	const uint32_t id_c = async_switch_handler_add(zend_coroutine, switch_handler_c);
	const uint32_t id_d = async_switch_handler_add(zend_coroutine, switch_handler_d);
	const uint32_t id_e = async_switch_handler_add(zend_coroutine, switch_handler_e);

	smart_str_append_printf(trace,
							"same=%d,%d ",
							async_switch_handler_add(zend_coroutine, switch_handler_a) == id_a,
							async_switch_handler_add(zend_coroutine, switch_handler_e) == id_e);
	smart_str_append_printf(trace, "removed-c=%d ", async_switch_handler_remove(zend_coroutine, id_c));
	smart_str_append_printf(trace, "again=%d ", async_switch_handler_remove(zend_coroutine, id_c));
	smart_str_append_printf(trace, "length=%u leave:", coroutine->switch_handlers->length);
	async_switch_handlers_call(coroutine, false);
	smart_str_append_printf(trace, " length=%u enter:", coroutine->switch_handlers->length);
	async_switch_handlers_call(coroutine, true);
	/* The last one removed leaves its copy behind the length: a search for it again finds nothing. */
	smart_str_append_printf(trace, " removed-e=%d ", async_switch_handler_remove(zend_coroutine, id_e));
	smart_str_append_printf(trace, "again-e=%d", async_switch_handler_remove(zend_coroutine, id_e));
	async_switch_handler_remove(zend_coroutine, id_a);
	smart_str_append_printf(trace, " length=%u", coroutine->switch_handlers->length);
	async_switch_handler_remove(zend_coroutine, id_d);
	smart_str_append_printf(trace, " freed=%d", coroutine->switch_handlers == NULL);
}

/* A switch handler that adds and removes switch handlers of its coroutine while the handlers run. */
static bool switch_handler_changes_handlers(zend_coroutine_t *coroutine, bool is_enter)
{
	const uint32_t added_id = async_switch_handler_add(coroutine, switch_handler_a);
	const bool removed = async_switch_handler_remove(coroutine, 1);

	smart_str_append_printf(((test_switch_coroutine_t *) coroutine)->trace, "add=%u remove=%d", added_id, removed);

	return false;
}

/* The switch handlers of a coroutine cannot change while they run: both calls warn and refuse. */
static void scenario_switch_handlers_running(smart_str *trace)
{
	test_switch_coroutine_t test_coroutine = { .trace = trace };
	async_coroutine_t *coroutine = &test_coroutine.coroutine;
	zend_coroutine_t *zend_coroutine = &coroutine->coroutine;

	async_switch_handler_add(zend_coroutine, switch_handler_changes_handlers);
	async_switch_handlers_call(coroutine, false);
	smart_str_append_printf(trace, " freed=%d", coroutine->switch_handlers == NULL);
}

/* data of a test finish handler: its name, what it returns, the trace it writes to, and an id to
 * remove on its run. */
typedef struct
{
	char name;
	bool keep;
	smart_str *trace;
	zend_coroutine_t *coroutine;
	uint32_t remove_id;
} test_finish_t;

static bool finish_handler_named(zend_coroutine_t *coroutine, zend_coroutine_t *waiter, void *data, bool is_bailout)
{
	const test_finish_t *handler = data;
	smart_str *trace = handler->trace;
	smart_str_appendc(trace, handler->name);

	if (handler->remove_id != 0) {
		smart_str_append_printf(
				trace, "(removed=%d)", async_finish_handler_remove(handler->coroutine, handler->remove_id));
	}

	return handler->keep;
}

/* Finish handlers A B C on one coroutine; A is removed by its id, then B: C stays and runs alone. A
 * positional handle would have removed C on the second call (B shifted into A's slot). */
static void scenario_finish_ids(smart_str *trace)
{
	async_coroutine_t coroutine = { 0 };
	test_finish_t handlers[3] = { { 'A', false, trace }, { 'B', false, trace }, { 'C', false, trace } };
	uint32_t ids[3];

	for (int i = 0; i < 3; i++) {
		ids[i] = async_finish_handler_add(&coroutine.coroutine, finish_handler_named, NULL, &handlers[i]);
	}

	smart_str_append_printf(trace, "removed A=%d ", async_finish_handler_remove(&coroutine.coroutine, ids[0]));
	smart_str_append_printf(trace, "B=%d ", async_finish_handler_remove(&coroutine.coroutine, ids[1]));
	smart_str_append_printf(trace, "again A=%d ran:", async_finish_handler_remove(&coroutine.coroutine, ids[0]));
	async_callbacks_notify((async_awaitable_t *) &coroutine, &coroutine.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " left=%u", coroutine.callbacks.length);
	async_callbacks_free((async_awaitable_t *) &coroutine, &coroutine.callbacks);
}

/* Finish handlers A B; B, the last, is removed by its id and searched for again: its slot behind the
 * length is not searched. A runs alone. */
static void scenario_finish_remove_last(smart_str *trace)
{
	async_coroutine_t coroutine = { 0 };
	test_finish_t handlers[2] = { { 'A', false, trace }, { 'B', false, trace } };

	async_finish_handler_add(&coroutine.coroutine, finish_handler_named, NULL, &handlers[0]);
	const uint32_t id_b = async_finish_handler_add(&coroutine.coroutine, finish_handler_named, NULL, &handlers[1]);
	smart_str_append_printf(trace, "removed B=%d ", async_finish_handler_remove(&coroutine.coroutine, id_b));
	smart_str_append_printf(trace, "again B=%d ran:", async_finish_handler_remove(&coroutine.coroutine, id_b));
	async_callbacks_notify((async_awaitable_t *) &coroutine, &coroutine.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " left=%u", coroutine.callbacks.length);
	async_callbacks_free((async_awaitable_t *) &coroutine, &coroutine.callbacks);
}

/* A returns true, B false, C removes itself by its own id: each runs once whatever it returns, C is
 * already gone when it runs, and a second notify finds nothing. */
static void scenario_finish_once(smart_str *trace)
{
	async_coroutine_t coroutine = { 0 };
	test_finish_t handlers[3] = { { 'A', true, trace },
								  { 'B', false, trace },
								  { 'C', false, trace, &coroutine.coroutine } };

	for (int i = 0; i < 3; i++) {
		const uint32_t id = async_finish_handler_add(&coroutine.coroutine, finish_handler_named, NULL, &handlers[i]);

		if (i == 2) {
			handlers[i].remove_id = id;
		}
	}

	async_callbacks_notify((async_awaitable_t *) &coroutine, &coroutine.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " left=%u again:", coroutine.callbacks.length);
	async_callbacks_notify((async_awaitable_t *) &coroutine, &coroutine.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " left=%u", coroutine.callbacks.length);
	async_callbacks_free((async_awaitable_t *) &coroutine, &coroutine.callbacks);
}

typedef struct
{
	const char *name;
	void (*run)(smart_str *trace);
} test_scenario_t;

static const test_scenario_t test_scenarios[] = {
	{ "remove-run", scenario_remove_run },           { "remove-self", scenario_remove_self },
	{ "single-self", scenario_single_self },         { "add-during", scenario_add_during },
	{ "nested-same", scenario_nested_same },         { "nested-other", scenario_nested_other },
	{ "finish-ids", scenario_finish_ids },           { "finish-remove-last", scenario_finish_remove_last },
	{ "finish-once", scenario_finish_once },         { "throw-stops", scenario_throw_stops },
	{ "bailout-caught", scenario_bailout_caught },   { "sched-kept", scenario_sched_kept },
	{ "remove-pending", scenario_remove_pending },   { "remove-past-cursor", scenario_remove_past_cursor },
	{ "remove-absent", scenario_remove_absent },     { "free-disposes", scenario_free_disposes },
	{ "switch-handlers", scenario_switch_handlers }, { "switch-handlers-running", scenario_switch_handlers_running },
};

/* Small integers stand for the pointers the scheduler's queues hold, pushed and popped by the
 * scheduler's calls. */
static void test_buffer_push(circular_buffer_t *buffer, const zend_long from, const zend_long to)
{
	for (zend_long value = from; value <= to; value++) {
		circular_buffer_push_ptr_with_resize(buffer, (void *) (uintptr_t) value);
	}
}

static void test_buffer_pop(circular_buffer_t *buffer, smart_str *trace, size_t count)
{
	void *item;

	while (count-- > 0 && circular_buffer_pop_ptr(buffer, &item) == SUCCESS) {
		smart_str_append_printf(trace, " %d", (int) (uintptr_t) item);
	}
}

/* 1 2 3 fill a buffer of 4 slots; 1 and 2 leave, 4 and 5 wrap around, 6 grows the wrapped buffer:
 * the order survives. Then a buffer full with its tail at slot 1 and its head at slot 0 grows. */
static void scenario_buffer_wrap_grow(smart_str *trace)
{
	circular_buffer_t buffer;

	circular_buffer_ctor(&buffer);
	test_buffer_push(&buffer, 1, 3);
	smart_str_append_printf(trace, "full=%d", circular_buffer_is_full(&buffer));
	test_buffer_pop(&buffer, trace, 2);
	test_buffer_push(&buffer, 4, 6);
	smart_str_append_printf(trace, " slots=%zu:", buffer.capacity);
	test_buffer_pop(&buffer, trace, SIZE_MAX);
	smart_str_append_printf(trace, " count=%zu", circular_buffer_count(&buffer));
	circular_buffer_dtor(&buffer);

	circular_buffer_ctor(&buffer);
	test_buffer_push(&buffer, 1, 3);
	test_buffer_pop(&buffer, trace, 1);
	test_buffer_push(&buffer, 4, 5);
	smart_str_append_printf(trace, " slots=%zu:", buffer.capacity);
	test_buffer_pop(&buffer, trace, SIZE_MAX);
	circular_buffer_dtor(&buffer);
}

/* push_front from tail 0 wraps to the last slot (asHiPriority on a run queue never popped). */
static void scenario_buffer_push_front(smart_str *trace)
{
	circular_buffer_t buffer;

	circular_buffer_ctor(&buffer);
	test_buffer_push(&buffer, 1, 2);
	circular_buffer_push_front(&buffer, (void *) (uintptr_t) 0);
	smart_str_append_printf(trace, "tail=%zu:", buffer.tail);
	test_buffer_pop(&buffer, trace, SIZE_MAX);
	circular_buffer_dtor(&buffer);
}

/* A buffer filled from tail 0 grows twice without moving its items: 4 slots to 8, then to 16. */
static void scenario_buffer_grow(smart_str *trace)
{
	circular_buffer_t buffer;

	circular_buffer_ctor(&buffer);
	test_buffer_push(&buffer, 1, 8);
	smart_str_append_printf(trace, "slots=%zu:", buffer.capacity);
	test_buffer_pop(&buffer, trace, SIZE_MAX);
	circular_buffer_dtor(&buffer);
}

/* The pointer helpers on a wrapped buffer: push_ptr refuses a full buffer, pop_ptr an empty one,
 * swap_ptr_at swaps by offset from the tail across the wrap and next to the tail,
 * push_ptr_with_resize grows. */
static void scenario_buffer_ptr(smart_str *trace)
{
	static char names[] = "ABCD";
	circular_buffer_t buffer;
	void *element;

	circular_buffer_ctor(&buffer);
	smart_str_append_printf(trace, "empty=%d ", circular_buffer_pop_ptr(&buffer, &element) == FAILURE);

	/* Tail 2: A B C take slots 2, 3 and 0. */
	for (int i = 0; i < 2; i++) {
		circular_buffer_push_ptr(&buffer, &names[i]);
		circular_buffer_pop_ptr(&buffer, &element);
	}

	for (int i = 0; i < 3; i++) {
		circular_buffer_push_ptr(&buffer, &names[i]);
	}

	smart_str_append_printf(trace, "full=%d ", circular_buffer_push_ptr(&buffer, &names[3]) == FAILURE);
	circular_buffer_swap_ptr_at(&buffer, 1, 2);
	circular_buffer_swap_ptr_at(&buffer, 0, 1);
	circular_buffer_push_ptr_with_resize(&buffer, &names[3]);
	smart_str_append_printf(trace, "slots=%zu:", buffer.capacity);

	while (circular_buffer_pop_ptr(&buffer, &element) == SUCCESS) {
		smart_str_appendc(trace, *(char *) element);
	}

	circular_buffer_dtor(&buffer);
}

/* push_front on a full wrapped buffer (asHiPriority on a full run queue): it grows first, and the
 * new item goes ahead of the rest. Then the same on a full buffer never popped: the tail wraps to the
 * last slot of the grown buffer. */
static void scenario_buffer_front_full(smart_str *trace)
{
	circular_buffer_t buffer;

	circular_buffer_ctor(&buffer);
	test_buffer_push(&buffer, 1, 2);
	test_buffer_pop(&buffer, trace, 2);
	test_buffer_push(&buffer, 10, 12);
	smart_str_append_printf(trace, " full=%d", circular_buffer_is_full(&buffer));
	circular_buffer_push_front(&buffer, (void *) (uintptr_t) 9);
	smart_str_append_printf(trace, " slots=%zu:", buffer.capacity);
	test_buffer_pop(&buffer, trace, SIZE_MAX);
	circular_buffer_dtor(&buffer);

	circular_buffer_ctor(&buffer);
	test_buffer_push(&buffer, 1, 3);
	circular_buffer_push_front(&buffer, (void *) (uintptr_t) 0);
	smart_str_append_printf(trace, " slots=%zu tail=%zu:", buffer.capacity, buffer.tail);
	test_buffer_pop(&buffer, trace, SIZE_MAX);
	circular_buffer_dtor(&buffer);
}

/* count on a wrapped buffer, its head behind its tail: 1 2 3 fill 4 slots, 1 and 2 leave, 4 takes
 * slot 3, 5 wraps to slot 0. */
static void scenario_buffer_count_wrapped(smart_str *trace)
{
	circular_buffer_t buffer;

	circular_buffer_ctor(&buffer);
	test_buffer_push(&buffer, 1, 3);
	test_buffer_pop(&buffer, trace, 2);
	test_buffer_push(&buffer, 4, 4);
	smart_str_append_printf(
			trace, " head=%zu tail=%zu count=%zu", buffer.head, buffer.tail, circular_buffer_count(&buffer));
	test_buffer_push(&buffer, 5, 5);
	smart_str_append_printf(trace,
							" head=%zu count=%zu full=%d:",
							buffer.head,
							circular_buffer_count(&buffer),
							circular_buffer_is_full(&buffer));
	test_buffer_pop(&buffer, trace, SIZE_MAX);
	circular_buffer_dtor(&buffer);
}

static const test_scenario_t buffer_scenarios[] = {
	{ "wrap-grow", scenario_buffer_wrap_grow },
	{ "push-front", scenario_buffer_push_front },
	{ "grow", scenario_buffer_grow },
	{ "ptr", scenario_buffer_ptr },
	{ "front-full", scenario_buffer_front_full },
	{ "count-wrapped", scenario_buffer_count_wrapped },
};

static void
test_run_scenario(zend_string *name, const test_scenario_t *scenarios, const size_t count, zval *return_value)
{
	for (size_t i = 0; i < count; i++) {
		if (zend_string_equals_cstr(name, scenarios[i].name, strlen(scenarios[i].name))) {
			smart_str trace = { 0 };
			scenarios[i].run(&trace);
			RETURN_STR(smart_str_extract(&trace));
		}
	}

	zend_argument_value_error(1, "is not a known scenario");
	RETURN_THROWS();
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_callbacks_scenario, 0, 1, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, name, IS_STRING, 0)
ZEND_END_ARG_INFO()

static ZEND_FUNCTION(callbacks_scenario)
{
	zend_string *name;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(name)
	ZEND_PARSE_PARAMETERS_END();

	test_run_scenario(name, test_scenarios, sizeof(test_scenarios) / sizeof(test_scenarios[0]), return_value);
}

static ZEND_FUNCTION(buffer_scenario)
{
	zend_string *name;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(name)
	ZEND_PARSE_PARAMETERS_END();

	test_run_scenario(name, buffer_scenarios, sizeof(buffer_scenarios) / sizeof(buffer_scenarios[0]), return_value);
}

/* A microtask that prints its label and the scheduler-context flag when the tick runs it, throws
 * for "throw", is cancelled before any tick for "cancel", calls `callback` when one is given, and
 * prints its release. */
typedef struct
{
	zend_async_microtask_t microtask; /* first: the core's release frees the block through it */
	char label;
	bool throws;
	zval callback; /* UNDEF without one */
} test_microtask_t;

static void test_microtask_handler(zend_async_microtask_t *microtask)
{
	test_microtask_t *test_microtask = (test_microtask_t *) microtask;

	php_printf("microtask %c sched=%d\n", test_microtask->label, (int) ZEND_ASYNC_IN_SCHEDULER_CONTEXT);

	if (!Z_ISUNDEF(test_microtask->callback)) {
		zval retval;
		call_user_function(NULL, NULL, &test_microtask->callback, &retval, 0, NULL);
		zval_ptr_dtor(&retval);
	}

	if (test_microtask->throws) {
		zend_throw_exception_ex(NULL, 0, "microtask %c", test_microtask->label);
	}
}

static void test_microtask_dtor(zend_async_microtask_t *microtask)
{
	test_microtask_t *test_microtask = (test_microtask_t *) microtask;

	php_printf("released %c\n", test_microtask->label);
	zval_ptr_dtor(&test_microtask->callback);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_defer, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, label, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, action, IS_STRING, 1, "null")
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, callback, IS_CALLABLE, 1, "null")
ZEND_END_ARG_INFO()

static ZEND_FUNCTION(defer)
{
	zend_string *label;
	zend_string *action = NULL;
	zval *callback = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_STR(label)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR_OR_NULL(action)
		Z_PARAM_ZVAL_OR_NULL(callback)
	ZEND_PARSE_PARAMETERS_END();

	test_microtask_t *test_microtask = ecalloc(1, sizeof(test_microtask_t));

	if (callback != NULL) {
		ZVAL_COPY(&test_microtask->callback, callback);
	}

	test_microtask->microtask.handler = test_microtask_handler;
	test_microtask->microtask.dtor = test_microtask_dtor;
	test_microtask->microtask.ref_count = 1;
	test_microtask->label = ZSTR_LEN(label) > 0 ? ZSTR_VAL(label)[0] : '?';
	test_microtask->throws = action != NULL && zend_string_equals_literal(action, "throw");

	if (action != NULL && zend_string_equals_literal(action, "cancel")) {
		ZEND_ASYNC_MICROTASK_CANCEL(&test_microtask->microtask);
	}

	if (UNEXPECTED(!ZEND_ASYNC_DEFER(&test_microtask->microtask))) {
		ZEND_ASYNC_MICROTASK_RELEASE(&test_microtask->microtask);
	}
}

/* A finish handler that throws "finish handler" when its coroutine finishes. */
static bool
test_throwing_finish_handler(zend_coroutine_t *coroutine, zend_coroutine_t *waiter, void *data, const bool is_bailout)
{
	(void) coroutine;
	(void) waiter;
	(void) data;
	(void) is_bailout;

	zend_throw_exception(NULL, "finish handler", 0);

	return false;
}

/* A finish handler that ends the request with a fatal error, a bailout out of the finish (U6). */
static bool
test_bailout_finish_handler(zend_coroutine_t *coroutine, zend_coroutine_t *waiter, void *data, const bool is_bailout)
{
	(void) waiter;
	(void) data;

	zend_error_noreturn(E_ERROR,
						"finish handler of coroutine %u bails out%s",
						((async_coroutine_t *) coroutine)->std.handle,
						is_bailout ? " after a bailout" : "");
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_add_throwing_finish_handler, 0, 1, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, coroutine, Async\\Coroutine, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, bailout, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

/* The handler throws, or with `bailout` ends the request with a fatal error. */
static ZEND_FUNCTION(add_throwing_finish_handler)
{
	zend_object *coroutine;
	bool bailout = false;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_OBJ_OF_CLASS(coroutine, async_ce_coroutine)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(bailout)
	ZEND_PARSE_PARAMETERS_END();

	ZEND_ASYNC_ADD_FINISH_HANDLER(&async_coroutine_from_object(coroutine)->coroutine,
								  bailout ? test_bailout_finish_handler : test_throwing_finish_handler,
								  NULL,
								  NULL);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_enqueue_with_error, 0, 2, _IS_BOOL, 0)
	ZEND_ARG_OBJ_INFO(0, coroutine, Async\\Coroutine, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 0)
	ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, transfer, _IS_BOOL, 0, "false")
ZEND_END_ARG_INFO()

/* The core's wake with an error (ZEND_ASYNC_ENQUEUE_WITH_ERROR, the fibers' path): the waker rule of
 * a resume, beside the cancel's. With `transfer` the enqueue takes a reference of its own, as the
 * fibers' path hands over a caught exception (zend_fibers.c, zend_fiber_coroutine_finish). */
static ZEND_FUNCTION(enqueue_with_error)
{
	zend_object *coroutine;
	zend_object *error;
	bool transfer = false;

	ZEND_PARSE_PARAMETERS_START(2, 3)
		Z_PARAM_OBJ_OF_CLASS(coroutine, async_ce_coroutine)
		Z_PARAM_OBJ_OF_CLASS(error, zend_ce_throwable)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(transfer)
	ZEND_PARSE_PARAMETERS_END();

	if (transfer) {
		GC_ADDREF(error);
	}

	RETURN_BOOL(ZEND_ASYNC_ENQUEUE_WITH_ERROR(&async_coroutine_from_object(coroutine)->coroutine, error, transfer));
}

/* Prints each switch of its coroutine, "leave #<id>" or "enter #<id>", and stays registered. */
static bool test_printing_switch_handler(zend_coroutine_t *coroutine, const bool is_enter)
{
	php_printf("%s #%u\n", is_enter ? "enter" : "leave", ((async_coroutine_t *) coroutine)->std.handle);

	return true;
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_add_printing_switch_handler, 0, 1, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, coroutine, Async\\Coroutine, 0)
ZEND_END_ARG_INFO()

static ZEND_FUNCTION(add_printing_switch_handler)
{
	zend_object *coroutine;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(coroutine, async_ce_coroutine)
	ZEND_PARSE_PARAMETERS_END();

	ZEND_ASYNC_ADD_SWITCH_HANDLER(&async_coroutine_from_object(coroutine)->coroutine, test_printing_switch_handler);
}

/* A finish handler that takes the coroutine's exception: clearing it marks it handled (the finish
 * handler contract, Zend/zend_async_API.h). */
static bool
test_clearing_finish_handler(zend_coroutine_t *coroutine, zend_coroutine_t *waiter, void *data, const bool is_bailout)
{
	(void) waiter;
	(void) data;
	(void) is_bailout;

	zend_object *exception = coroutine->exception;

	if (exception != NULL) {
		php_printf("finish handler takes %s\n", ZSTR_VAL(exception->ce->name));
		coroutine->exception = NULL;
		OBJ_RELEASE(exception);
	}

	return false;
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_add_clearing_finish_handler, 0, 1, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, coroutine, Async\\Coroutine, 0)
ZEND_END_ARG_INFO()

static ZEND_FUNCTION(add_clearing_finish_handler)
{
	zend_object *coroutine;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OF_CLASS(coroutine, async_ce_coroutine)
	ZEND_PARSE_PARAMETERS_END();

	ZEND_ASYNC_ADD_FINISH_HANDLER(
			&async_coroutine_from_object(coroutine)->coroutine, test_clearing_finish_handler, NULL, NULL);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_coroutine_count, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

static ZEND_FUNCTION(coroutine_count)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_LONG(ZEND_ASYNC_GET_COROUTINE_COUNT());
}

/* Indexed by async_test_fault_site_t. */
static const char *const fault_site_names[] = { NULL, "enqueue", "reserve", "link" };

void async_test_fault_hit(const async_test_fault_site_t site)
{
	if (EXPECTED(ASYNC_G(fault_site) != site)) {
		return;
	}

	ASYNC_G(fault_site) = ASYNC_TEST_FAULT_NONE;
	zend_error_noreturn(E_ERROR, "Fault injected at %s", fault_site_names[site]);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_fail_at, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, site, IS_STRING, 0)
ZEND_END_ARG_INFO()

/* The next pass through `site` ends the request with "Fault injected at <site>", a fatal error as
 * running out of memory raises; one site is armed at a time. */
static ZEND_FUNCTION(fail_at)
{
	zend_string *site_name;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_STR(site_name)
	ZEND_PARSE_PARAMETERS_END();

	const uint8_t site_count = sizeof(fault_site_names) / sizeof(fault_site_names[0]);

	for (uint8_t site = ASYNC_TEST_FAULT_ENQUEUE; site < site_count; site++) {
		if (zend_string_equals_cstr(site_name, fault_site_names[site], strlen(fault_site_names[site]))) {
			ASYNC_G(fault_site) = site;
			return;
		}
	}

	zend_argument_value_error(1, "must be \"enqueue\", \"reserve\" or \"link\"");
}

/* clang-format off */
const zend_function_entry true_async_test_hooks_functions[] = {
	ZEND_RAW_FENTRY("TrueAsync\\Test\\callbacks_scenario", ZEND_FN(callbacks_scenario), arginfo_callbacks_scenario, 0, NULL, NULL)
	ZEND_RAW_FENTRY("TrueAsync\\Test\\buffer_scenario", ZEND_FN(buffer_scenario), arginfo_callbacks_scenario, 0, NULL, NULL)
	ZEND_RAW_FENTRY("TrueAsync\\Test\\defer", ZEND_FN(defer), arginfo_defer, 0, NULL, NULL)
	ZEND_RAW_FENTRY("TrueAsync\\Test\\add_throwing_finish_handler", ZEND_FN(add_throwing_finish_handler), arginfo_add_throwing_finish_handler, 0, NULL, NULL)
	ZEND_RAW_FENTRY("TrueAsync\\Test\\enqueue_with_error", ZEND_FN(enqueue_with_error), arginfo_enqueue_with_error, 0, NULL, NULL)
	ZEND_RAW_FENTRY("TrueAsync\\Test\\fail_at", ZEND_FN(fail_at), arginfo_fail_at, 0, NULL, NULL)
	ZEND_RAW_FENTRY("TrueAsync\\Test\\add_printing_switch_handler", ZEND_FN(add_printing_switch_handler), arginfo_add_printing_switch_handler, 0, NULL, NULL)
	ZEND_RAW_FENTRY("TrueAsync\\Test\\add_clearing_finish_handler", ZEND_FN(add_clearing_finish_handler), arginfo_add_clearing_finish_handler, 0, NULL, NULL)
	ZEND_RAW_FENTRY("TrueAsync\\Test\\coroutine_count", ZEND_FN(coroutine_count), arginfo_coroutine_count, 0, NULL, NULL)
	ZEND_FE_END
};
/* clang-format on */
