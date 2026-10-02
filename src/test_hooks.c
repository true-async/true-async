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
 * what it popped and the sizes it saw. */

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
	async_event_callback_t base;
	char name;
	void (*action)(test_callback_t *self, async_awaitable_t *target);
	async_callbacks_vector_t *other_vector; /* the vector the action works on */
	test_callback_t *other;                 /* the callback the action works on */
	test_target_t *other_target;
	smart_str *trace;
	uint32_t runs;
};

static void
test_callback_fire(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	test_callback_t *self = (test_callback_t *) callback;
	smart_str_appendc(self->trace, self->name);
	self->runs++;

	/* Each callback runs with no exception pending, in scheduler context. */
	if (UNEXPECTED(EG(exception) != NULL)) {
		smart_str_appendc(self->trace, '?');
	}

	if (UNEXPECTED(!ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		smart_str_appendc(self->trace, '!');
	}

	if (self->action != NULL) {
		self->action(self, target);
	}
}

static void test_callback_init(test_callback_t *callback, const char name, smart_str *trace)
{
	memset(callback, 0, sizeof(*callback));
	callback->base.callback = test_callback_fire;
	callback->name = name;
	callback->trace = trace;
}

static void action_remove_other(test_callback_t *self, async_awaitable_t *target)
{
	async_callbacks_remove(self->other_vector, &self->other->base);
}

static void action_remove_self(test_callback_t *self, async_awaitable_t *target)
{
	async_callbacks_remove(self->other_vector, &self->base);
}

static void action_add_other(test_callback_t *self, async_awaitable_t *target)
{
	async_callbacks_add(self->other_vector, &self->other->base);
}

static void action_notify_other(test_callback_t *self, async_awaitable_t *target)
{
	if (self->other_vector->capacity & ASYNC_CALLBACKS_F_NOTIFYING) {
		smart_str_appends(self->trace, "(refused)");
	}

	async_callbacks_notify((async_awaitable_t *) self->other_target, self->other_vector, NULL, NULL);
}

static void action_throw(test_callback_t *self, async_awaitable_t *target)
{
	const char message[2] = { (char) (self->name + 'a' - 'A'), '\0' };
	zend_throw_exception(NULL, message, 0);
}

/* Bails out on its first run only. */
static void action_bailout_once(test_callback_t *self, async_awaitable_t *target)
{
	if (self->runs == 1) {
		zend_bailout();
	}
}

/* zend_bailout() clears the current frame and marks the shutdown unclean; the catch puts both
 * back, so the PHP code that called the hook goes on. */
static void action_notify_catching_bailout(test_callback_t *self, async_awaitable_t *target)
{
	zend_execute_data *execute_data = EG(current_execute_data);
	const bool unclean_shutdown = CG(unclean_shutdown);

	zend_try
	{
		async_callbacks_notify((async_awaitable_t *) self->other_target, self->other_vector, NULL, NULL);
	}
	zend_catch
	{
		EG(current_execute_data) = execute_data;
		CG(unclean_shutdown) = unclean_shutdown;
		smart_str_appends(self->trace, "(caught)");
	}
	zend_end_try();
}

/* Appends the messages of EG(exception) and its previous chain, then clears it. */
static void test_trace_exception(smart_str *trace)
{
	smart_str_appends(trace, " caught:");

	for (zend_object *exception = EG(exception); exception != NULL;) {
		zval rv;
		zend_class_entry *exception_class = zend_get_exception_base(exception);
		zval *message = zend_read_property_ex(exception_class, exception, ZSTR_KNOWN(ZEND_STR_MESSAGE), true, &rv);
		zval *previous = zend_read_property_ex(exception_class, exception, ZSTR_KNOWN(ZEND_STR_PREVIOUS), true, &rv);

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
		async_callbacks_add(vector, &callbacks[i].base);
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
	callbacks[1].other = &callbacks[0];
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
	async_callbacks_add(&target.callbacks, &callback.base);
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
	callbacks[0].other = &callbacks[2];
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
	w.other = &r;
	async_callbacks_add(&outer.callbacks, &r.base);
	async_callbacks_add(&outer.callbacks, &a.base);
	async_callbacks_add(&outer.callbacks, &b.base);
	async_callbacks_add(&inner.callbacks, &w.base);
	async_callbacks_add(&inner.callbacks, &x.base);
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
	async_callbacks_add(&outer.callbacks, &a.base);
	async_callbacks_add(&outer.callbacks, &b.base);
	async_callbacks_add(&inner.callbacks, &w.base);
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
	async_callbacks_add(&target.callbacks, &a.base);
	smart_str_appends(trace, "outside:");
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " sched=%d inside:", (int) ZEND_ASYNC_IN_SCHEDULER_CONTEXT);
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;
	async_callbacks_notify((async_awaitable_t *) &target, &target.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " sched=%d", (int) ZEND_ASYNC_IN_SCHEDULER_CONTEXT);
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;
	async_callbacks_free((async_awaitable_t *) &target, &target.callbacks);
}

/* data of a test finish handler: its name, what it returns, and an id to remove on its run. */
typedef struct
{
	char name;
	bool keep;
	async_coroutine_t *coroutine;
	uint32_t remove_id;
} test_finish_t;

static bool finish_handler_named(zend_coroutine_t *coroutine, zend_coroutine_t *waiter, void *data, bool is_bailout)
{
	test_finish_t *handler = data;
	smart_str *trace = ASYNC_G(test_trace);
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
	test_finish_t handlers[3] = { { 'A' }, { 'B' }, { 'C' } };
	uint32_t ids[3];

	ASYNC_G(test_trace) = trace;

	for (int i = 0; i < 3; i++) {
		ids[i] = async_finish_handler_add(&coroutine, finish_handler_named, NULL, &handlers[i]);
	}

	smart_str_append_printf(trace, "removed A=%d ", async_finish_handler_remove(&coroutine, ids[0]));
	smart_str_append_printf(trace, "B=%d ", async_finish_handler_remove(&coroutine, ids[1]));
	smart_str_append_printf(trace, "again A=%d ran:", async_finish_handler_remove(&coroutine, ids[0]));
	async_callbacks_notify((async_awaitable_t *) &coroutine, &coroutine.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " left=%u", coroutine.callbacks.length);
	async_callbacks_free((async_awaitable_t *) &coroutine, &coroutine.callbacks);
	ASYNC_G(test_trace) = NULL;
}

/* A returns true, B false, C removes itself by its own id: each runs once whatever it returns, C is
 * already gone when it runs, and a second notify finds nothing. */
static void scenario_finish_once(smart_str *trace)
{
	async_coroutine_t coroutine = { 0 };
	test_finish_t handlers[3] = { { 'A', true }, { 'B', false }, { 'C', false, &coroutine } };

	ASYNC_G(test_trace) = trace;

	for (int i = 0; i < 3; i++) {
		const uint32_t id = async_finish_handler_add(&coroutine, finish_handler_named, NULL, &handlers[i]);

		if (i == 2) {
			handlers[i].remove_id = id;
		}
	}

	async_callbacks_notify((async_awaitable_t *) &coroutine, &coroutine.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " left=%u again:", coroutine.callbacks.length);
	async_callbacks_notify((async_awaitable_t *) &coroutine, &coroutine.callbacks, NULL, NULL);
	smart_str_append_printf(trace, " left=%u", coroutine.callbacks.length);
	async_callbacks_free((async_awaitable_t *) &coroutine, &coroutine.callbacks);
	ASYNC_G(test_trace) = NULL;
}

typedef struct
{
	const char *name;
	void (*run)(smart_str *trace);
} test_scenario_t;

static const test_scenario_t test_scenarios[] = {
	{ "remove-run", scenario_remove_run },   { "remove-self", scenario_remove_self },
	{ "single-self", scenario_single_self }, { "add-during", scenario_add_during },
	{ "nested-same", scenario_nested_same }, { "nested-other", scenario_nested_other },
	{ "finish-ids", scenario_finish_ids },   { "finish-once", scenario_finish_once },
	{ "throw-stops", scenario_throw_stops }, { "bailout-caught", scenario_bailout_caught },
	{ "sched-kept", scenario_sched_kept },
};

static void test_buffer_push(circular_buffer_t *buffer, const zend_long from, const zend_long to)
{
	for (zend_long value = from; value <= to; value++) {
		circular_buffer_push(buffer, &value, true);
	}
}

static void test_buffer_pop(circular_buffer_t *buffer, smart_str *trace, size_t count)
{
	zend_long value;

	while (count-- > 0 && circular_buffer_is_not_empty(buffer)) {
		circular_buffer_pop(buffer, &value);
		smart_str_append_printf(trace, " %d", (int) value);
	}
}

/* 1 2 3 fill a buffer of 4 slots; 1 and 2 leave, 4 and 5 wrap around, 6 grows the wrapped buffer:
 * the order survives. */
static void scenario_buffer_wrap_grow(smart_str *trace)
{
	circular_buffer_t buffer;

	circular_buffer_ctor(&buffer, 4, sizeof(zend_long), NULL);
	test_buffer_push(&buffer, 1, 3);
	smart_str_append_printf(trace, "full=%d", circular_buffer_is_full(&buffer));
	test_buffer_pop(&buffer, trace, 2);
	test_buffer_push(&buffer, 4, 6);
	smart_str_append_printf(trace, " capacity=%zu:", circular_buffer_capacity(&buffer));
	test_buffer_pop(&buffer, trace, SIZE_MAX);
	smart_str_append_printf(trace, " count=%zu", circular_buffer_count(&buffer));
	circular_buffer_dtor(&buffer);
}

/* push_front from tail 0 wraps to the last slot; with no room and no resize it falls back to a
 * push, which refuses the full buffer. */
static void scenario_buffer_push_front(smart_str *trace)
{
	circular_buffer_t *buffer = circular_buffer_new(4, sizeof(zend_long), NULL);
	zend_long value = 0;

	test_buffer_push(buffer, 1, 2);
	circular_buffer_push_front(buffer, &value, true);
	value = 9;
	smart_str_append_printf(trace, "refused=%d:", circular_buffer_push_front(buffer, &value, false) == FAILURE);
	test_buffer_pop(buffer, trace, SIZE_MAX);
	circular_buffer_destroy(buffer);
}

/* 32 items grow a 4-slot buffer to 64 slots; after 30 leave, the next push halves it, keeping the
 * order. With auto_optimize off it stays at 64. */
static void scenario_buffer_shrink(smart_str *trace)
{
	for (int optimize = 1; optimize >= 0; optimize--) {
		circular_buffer_t buffer;

		circular_buffer_ctor(&buffer, 0, sizeof(zend_long), NULL);
		buffer.auto_optimize = optimize;
		test_buffer_push(&buffer, 1, 32);
		smart_str_append_printf(trace, "%sslots %zu", optimize ? "" : " off: ", buffer.capacity);

		zend_long value;
		for (int i = 0; i < 30; i++) {
			circular_buffer_pop(&buffer, &value);
		}

		test_buffer_push(&buffer, 33, 33);
		smart_str_append_printf(trace, "->%zu:", buffer.capacity);
		test_buffer_pop(&buffer, trace, SIZE_MAX);
		circular_buffer_dtor(&buffer);
	}
}

/* The pointer helpers on a wrapped buffer: push_ptr refuses a full buffer, pop_ptr an empty one,
 * swap_ptr_at swaps by offset from the tail across the wrap, push_ptr_with_resize grows. */
static void scenario_buffer_ptr(smart_str *trace)
{
	static char names[] = "ABCD";
	circular_buffer_t buffer;
	void *ptr;

	circular_buffer_ctor(&buffer, 4, sizeof(void *), &true_async_persistent_allocator);
	smart_str_append_printf(trace, "empty=%d ", circular_buffer_pop_ptr(&buffer, &ptr) == FAILURE);

	/* Tail 2: A B C take slots 2, 3 and 0. */
	for (int i = 0; i < 2; i++) {
		circular_buffer_push_ptr(&buffer, &names[i]);
		circular_buffer_pop_ptr(&buffer, &ptr);
	}

	for (int i = 0; i < 3; i++) {
		circular_buffer_push_ptr(&buffer, &names[i]);
	}

	smart_str_append_printf(trace, "full=%d ", circular_buffer_push_ptr(&buffer, &names[3]) == FAILURE);
	circular_buffer_swap_ptr_at(&buffer, 1, 2);
	circular_buffer_push_ptr_with_resize(&buffer, &names[3]);
	smart_str_append_printf(trace, "capacity=%zu:", circular_buffer_capacity(&buffer));

	while (circular_buffer_pop_ptr(&buffer, &ptr) == SUCCESS) {
		smart_str_appendc(trace, *(char *) ptr);
	}

	circular_buffer_dtor(&buffer);
}

/* push_front with resize on a full wrapped buffer (asHiPriority on a full run queue): it grows
 * first, and the new item goes ahead of the rest. */
static void scenario_buffer_front_full(smart_str *trace)
{
	circular_buffer_t buffer;
	zend_long value = 9;

	circular_buffer_ctor(&buffer, 4, sizeof(zend_long), NULL);
	test_buffer_push(&buffer, 1, 2);
	test_buffer_pop(&buffer, trace, 2);
	test_buffer_push(&buffer, 10, 12);
	smart_str_append_printf(trace, " full=%d", circular_buffer_is_full(&buffer));
	circular_buffer_push_front(&buffer, &value, true);
	smart_str_append_printf(trace, " capacity=%zu:", circular_buffer_capacity(&buffer));
	test_buffer_pop(&buffer, trace, SIZE_MAX);
	circular_buffer_dtor(&buffer);
}

/* A zero-filled buffer (a scheduler queue before the scheduler allocates it) reads as empty. */
static void scenario_buffer_zeroed(smart_str *trace)
{
	circular_buffer_t buffer = { 0 };
	void *ptr;

	smart_str_append_printf(trace,
							"count=%zu empty=%d not_empty=%d pop=%d",
							circular_buffer_count(&buffer),
							circular_buffer_is_empty(&buffer),
							circular_buffer_is_not_empty(&buffer),
							circular_buffer_pop_ptr(&buffer, &ptr) == FAILURE);
}

static const test_scenario_t buffer_scenarios[] = {
	{ "wrap-grow", scenario_buffer_wrap_grow },   { "push-front", scenario_buffer_push_front },
	{ "shrink", scenario_buffer_shrink },         { "ptr", scenario_buffer_ptr },
	{ "front-full", scenario_buffer_front_full }, { "zeroed", scenario_buffer_zeroed },
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
 * for "throw", is cancelled before any tick for "cancel", and prints its release. */
typedef struct
{
	zend_async_microtask_t microtask; /* first: the core's release frees the block through it */
	char label;
	bool throws;
} test_microtask_t;

static void test_microtask_handler(zend_async_microtask_t *microtask)
{
	const test_microtask_t *test_microtask = (test_microtask_t *) microtask;

	php_printf("microtask %c sched=%d\n", test_microtask->label, (int) ZEND_ASYNC_IN_SCHEDULER_CONTEXT);

	if (test_microtask->throws) {
		zend_throw_exception_ex(NULL, 0, "microtask %c", test_microtask->label);
	}
}

static void test_microtask_dtor(zend_async_microtask_t *microtask)
{
	php_printf("released %c\n", ((test_microtask_t *) microtask)->label);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_defer, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, label, IS_STRING, 0)
	ZEND_ARG_TYPE_INFO(0, action, IS_STRING, 0)
ZEND_END_ARG_INFO()

static ZEND_FUNCTION(defer)
{
	zend_string *label;
	zend_string *action = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_STR(label)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR(action)
	ZEND_PARSE_PARAMETERS_END();

	test_microtask_t *test_microtask = ecalloc(1, sizeof(test_microtask_t));

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

/* clang-format off */
const zend_function_entry true_async_test_hooks_functions[] = {
	ZEND_RAW_FENTRY("TrueAsync\\Test\\callbacks_scenario", ZEND_FN(callbacks_scenario), arginfo_callbacks_scenario, 0, NULL, NULL)
	ZEND_RAW_FENTRY("TrueAsync\\Test\\buffer_scenario", ZEND_FN(buffer_scenario), arginfo_callbacks_scenario, 0, NULL, NULL)
	ZEND_RAW_FENTRY("TrueAsync\\Test\\defer", ZEND_FN(defer), arginfo_defer, 0, NULL, NULL)
	ZEND_FE_END
};
/* clang-format on */
