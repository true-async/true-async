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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "zend_enum.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "ext/standard/io_poll.h"
#include "php_true_async.h"
#include "await.h"
#include "coroutine.h"
#include "exceptions.h"
#include "future.h"
#include "os_signal.h"
#include "reactor.h"
#include "timeout.h"

#ifndef PHP_WIN32
#include <signal.h>
#include <unistd.h>
#include "zend_signal.h"

/* As the handles block (ext/standard/io_poll.c:987-992): the mask is the thread's under ZTS. */
#ifdef ZTS
#define SIGNAL_SIGMASK pthread_sigmask
#else
#define SIGNAL_SIGMASK sigprocmask
#endif
#endif

zend_class_entry *async_ce_signal = NULL;

/* A Future failed at once with the error of a completed token, taken over, or AsyncCancellation when
 * it has none, as TrueAsync's (async.c:1393-1419). */
static zend_object *signal_future_cancelled(zend_object *token_error)
{
	async_future_event_t *future;
	zend_object *object = async_future_new_pending(&future);

	if (token_error == NULL) {
		token_error = async_new_exception(async_ce_cancellation, "Signal wait cancelled");
	}

	async_future_event_resolve(future, NULL, token_error);
	OBJ_RELEASE(token_error);

	return object;
}

#ifndef PHP_WIN32

struct _async_signal_watch_s
{
	async_event_t base;        /* callbacks: the waits' on_signal; ref_count: the registry's, a delivery's */
	int signo;                 /* the platform's number */
	zend_object *signal_case;  /* what the Futures complete with; an enum case lives with its class */
	zend_object *handle;       /* Io\Poll\SignalHandle of `signo`, in the registry's context */
	async_io_event_t *sigwait; /* the event of the SIGWAIT op while submitted; one reference */
	async_event_callback_t sigwait_callback; /* in `sigwait`'s vector */
	php_sigset_t set;
	php_siginfo_t info; /* the delivery, written by the Ring or taken from the source */
	int fd;             /* the signal source the Poll queue waits on; -1 without one */
};

/* One Future of Async\signal(), freed when its event completes or goes. */
typedef struct
{
	async_event_callback_t on_signal; /* in the watch's vector while `watch` is set */
	async_event_callback_t on_future; /* in the Future's event: its dispose leaves the watch and the token */
	async_event_callback_t on_token;  /* in the token's vector while `token` is set */
	async_signal_watch_t *watch;      /* NULL once the request's shutdown withdrew the watch */
	async_future_event_t *future;     /* borrowed: the event disposes on_future before it goes */
	async_awaitable_t *token;         /* a reference, a Timeout's subscription; NULL without */
} signal_wait_t;

#define SIGNAL_WAIT_OF(callback, member) ((signal_wait_t *) ((char *) (callback) - offsetof(signal_wait_t, member)))

///////////////////////////////////////////////////////////////////
/// The registry
///////////////////////////////////////////////////////////////////

/* `new $class($arg)` for a class of ext/standard, `lc_name` in lower case; false with its exception. */
static bool signal_new_object(zval *object, const char *lc_name, size_t lc_name_len, zval *arg)
{
	zend_class_entry *ce = zend_hash_str_find_ptr(EG(class_table), lc_name, lc_name_len);

	ZEND_ASSERT(ce != NULL && "ext/standard registers it");
	object_init_ex(object, ce);
	zend_call_known_instance_method(ce->constructor, Z_OBJ_P(object), NULL, arg != NULL ? 1 : 0, arg);

	if (UNEXPECTED(EG(exception) != NULL)) {
		zval_ptr_dtor(object);
		return false;
	}

	return true;
}

static bool signal_context_add(zend_object *context, zend_object *handle)
{
	zval handle_value;
	zval events;
	zval event;
	zval watcher;

	ZVAL_OBJ(&handle_value, handle);
	ZVAL_OBJ_COPY(&event, zend_enum_get_case_cstr(php_io_poll_event_class_entry, "Signal"));
	array_init(&events);
	zend_hash_next_index_insert_new(Z_ARRVAL(events), &event);

	zend_call_method_with_2_params(context, context->ce, NULL, "add", &watcher, &handle_value, &events);

	/* The context holds its watcher; the handle leaves it with php_io_poll_handle_remove_from_all_contexts(). */
	zval_ptr_dtor(&watcher);
	zval_ptr_dtor(&events);

	return EG(exception) == NULL;
}

static async_signal_registry_t *signal_registry(void)
{
	async_signal_registry_t *registry = ASYNC_G(signals);

	if (EXPECTED(registry != NULL)) {
		return registry;
	}

	zval context;

	if (UNEXPECTED(!signal_new_object(&context, ZEND_STRL("io\\poll\\context"), NULL))) {
		return NULL;
	}

	registry = ecalloc(1, sizeof(async_signal_registry_t));
	registry->context = Z_OBJ(context);
	sigemptyset(&registry->watched);
	sigemptyset(&registry->reblocked);
	ASYNC_G(signals) = registry;

	return registry;
}

static void signal_registry_release_if_empty(async_signal_registry_t *registry)
{
	if (registry->count != 0) {
		return;
	}

	ASYNC_G(signals) = NULL;
	OBJ_RELEASE(registry->context);
	efree(registry);
}

void async_signal_reblock(void)
{
	async_signal_registry_t *registry = ASYNC_G(signals);
	sigset_t mask_before;

	if (UNEXPECTED(SIGNAL_SIGMASK(SIG_BLOCK, &registry->watched, &mask_before) != 0)) {
		return;
	}

	/* The handle's removal unblocks only what was unblocked at its add
	 * (php_io_poll_signals_blocked_by_handles): signal_watch_free() unblocks what is blocked here. */
	for (int signo = 1; signo < PHP_NSIG; signo++) {
		if (UNEXPECTED(sigismember(&mask_before, signo) == 0 && sigismember(&registry->watched, signo) == 1)) {
			sigaddset(&registry->reblocked, signo);
		}
	}
}

///////////////////////////////////////////////////////////////////
/// Watches
///////////////////////////////////////////////////////////////////

/* The platform's number of a case, which carries the Linux one; 0 for a signal the platform lacks. */
static int signal_native_number(zend_object *signal_case)
{
	switch (Z_LVAL_P(zend_enum_fetch_case_value(signal_case))) {
		case 1:
			return SIGHUP;
		case 2:
			return SIGINT;
		case 3:
			return SIGQUIT;
		case 4:
			return SIGILL;
		case 6:
			return SIGABRT;
		case 8:
			return SIGFPE;
		case 9:
			return SIGKILL;
		case 10:
			return SIGUSR1;
		case 11:
			return SIGSEGV;
		case 12:
			return SIGUSR2;
		case 15:
			return SIGTERM;
#ifdef SIGWINCH
		case 28:
			return SIGWINCH;
#endif
		default:
			return 0;
	}
}

/* A delivery also goes to the handler the Zend signal table holds (pcntl_signal()'s), as TrueAsync's
 * libuv_global_signal_callback (libuv_reactor.c:1383-1403): the watch's block keeps the signal from
 * zend_signal_handler_defer(). */
static void signal_forward(const int signo, siginfo_t *info)
{
#ifdef ZEND_SIGNALS
	const zend_signal_entry_t entry = SIGG(handlers)[signo - 1];

	if (entry.handler == (void *) SIG_DFL || entry.handler == (void *) SIG_IGN) {
		return;
	}

	/* Zend calls a handler with every signal masked (sa_mask is global_sigmask, Zend/zend_signal.c);
	 * here a signal arriving during the call would re-enter pcntl's handler, which queues its delivery
	 * without a lock. The depth makes zend_signal_handler_defer() keep it until the unblock. */
	ZEND_SIGNAL_BLOCK_INTERRUPTIONS();

	if (entry.flags & SA_SIGINFO) {
		((void (*)(int, siginfo_t *, void *)) entry.handler)(signo, info, NULL);
	} else {
		((void (*)(int)) entry.handler)(signo);
	}

	ZEND_SIGNAL_UNBLOCK_INTERRUPTIONS();
#else
	(void) signo;
	(void) info;
#endif
}

static void signal_watch_notify(async_signal_watch_t *watch, zval *signal_case, zend_object *error)
{
	watch->base.ref_count++;
	async_callbacks_notify((async_awaitable_t *) watch, &watch->base.callbacks, signal_case, error);
	watch->base.ref_count--;
}

/* Fails the watch's Futures with the pending exception, which it clears. */
static void signal_watch_fail(async_signal_watch_t *watch)
{
	zend_object *error = EG(exception);

	GC_ADDREF(error);
	zend_clear_exception();
	signal_watch_notify(watch, NULL, error);
	OBJ_RELEASE(error);
}

static void signal_watch_disarm(async_signal_watch_t *watch)
{
	async_io_event_t *sigwait = watch->sigwait;

	if (sigwait == NULL) {
		return;
	}

	watch->sigwait = NULL;
	async_io_event_orphan(sigwait);
	async_callbacks_remove(&sigwait->base.callbacks, &watch->sigwait_callback);
	async_io_event_release(sigwait);
}

/* With its last wait: the number leaves the context, which unblocks it and records what arrived
 * since the last delivery into the handle. Nothing waited for that, so it is raised again for the
 * process's own action, as without a watch. A number async_signal_reblock() blocked is unblocked here. */
static void signal_watch_free(async_signal_watch_t *watch)
{
	async_signal_registry_t *registry = ASYNC_G(signals);

	signal_watch_disarm(watch);
	registry->watches[watch->signo] = NULL;
	registry->count--;
	sigdelset(&registry->watched, watch->signo);

	php_io_poll_handle_remove_from_all_contexts(watch->handle);

	if (UNEXPECTED(sigismember(&registry->reblocked, watch->signo) == 1)) {
		sigset_t to_unblock;

		sigemptyset(&to_unblock);
		sigaddset(&to_unblock, watch->signo);
		sigdelset(&registry->reblocked, watch->signo);
		SIGNAL_SIGMASK(SIG_UNBLOCK, &to_unblock, NULL);
	}

	php_siginfo_t info;

	while (php_io_poll_signal_handle_take(watch->handle, &watch->set, &info) > 0) {
		raise(watch->signo);
	}

	OBJ_RELEASE(watch->handle);

	if (watch->fd >= 0) {
		close(watch->fd);
	}

	async_callbacks_free((async_awaitable_t *) watch, &watch->base.callbacks);
	efree(watch);

	signal_registry_release_if_empty(registry);
}

static void
signal_op_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception);

/* timeout_arm()'s steps: an op the submit completes at once delivers before the return. */
static bool signal_watch_arm(async_signal_watch_t *watch)
{
	async_io_event_t *sigwait = async_io_event_new();

	php_io_op_sigwait(&sigwait->op, watch->handle, &watch->set, &watch->info, php_io_deadline_infinite());

	if (watch->fd >= 0) {
		sigwait->op.fd = watch->fd;
	}

	async_callbacks_reserve(&sigwait->base.callbacks, 1);
	watch->sigwait_callback.flags = 0;
	watch->sigwait_callback.callback = signal_op_wake;
	watch->sigwait_callback.dispose = NULL;
	async_callbacks_push_reserved(&sigwait->base.callbacks, &watch->sigwait_callback);

	watch->sigwait = sigwait;
	sigwait->base.ref_count++;

	const zend_result submitted = async_io_event_submit(sigwait);

	if (UNEXPECTED(submitted == FAILURE)) {
		watch->sigwait = NULL;
		async_callbacks_remove(&sigwait->base.callbacks, &watch->sigwait_callback);
		async_io_event_release(sigwait);
	}

	async_io_event_release(sigwait);

	return submitted == SUCCESS;
}

/* After a delivery and after a wait leaves: the watch goes with its last wait, and waits again for
 * the others. One that cannot fails them with the error. */
static void signal_watch_settle(async_signal_watch_t *watch)
{
	if (watch->base.ref_count > 1) {
		return;
	}

	if (watch->base.callbacks.length == 0) {
		signal_watch_free(watch);
		return;
	}

	if (watch->sigwait != NULL || EXPECTED(signal_watch_arm(watch))) {
		return;
	}

	signal_watch_fail(watch);
	signal_watch_settle(watch);
}

/* The completion of the SIGWAIT op: on the Ring the signal is in `info`, on the Poll queue the source
 * is ready and the signal is taken from it, as php_io_sigwait() does (main/io/php_io_hooks.c:2003-2013).
 * A Done with EAGAIN, or a source with nothing to take, lost the signal to another sigwait: the watch
 * waits again. */
static void
signal_op_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) exception;

	async_signal_watch_t *watch =
			(async_signal_watch_t *) ((char *) callback - offsetof(async_signal_watch_t, sigwait_callback));
	const php_io_op_result *op_result = result;
	int signo = 0;
	int error = 0;

	if (op_result->status == PHP_IO_READY) {
		signo = php_poll_signal_source_take(watch->fd, &watch->set, &watch->info);
	} else if (op_result->status == PHP_IO_DONE && op_result->error == 0) {
		signo = (int) op_result->res;
	} else if (op_result->status != PHP_IO_INTERRUPTED &&
			   !(op_result->status == PHP_IO_DONE && op_result->error == EAGAIN)) {
		error = op_result->error != 0 ? op_result->error : EIO;
	}

	/* The dispatch holds the op across this notify, and frees its vector after it: the callback
	 * leaves it, as the watch may go below. */
	async_io_event_t *sigwait = watch->sigwait;

	watch->sigwait = NULL;
	async_callbacks_remove(&sigwait->base.callbacks, &watch->sigwait_callback);
	async_io_event_release(sigwait);

	if (signo > 0) {
		zval signal_case;

		ZVAL_OBJ(&signal_case, watch->signal_case);
		signal_forward(signo, &watch->info);
		signal_watch_notify(watch, &signal_case, NULL);
	} else if (UNEXPECTED(error != 0)) {
		zend_object *failure = async_new_exception(
				zend_ce_error, "The signal wait ended with status %d: %s", (int) op_result->status, strerror(error));

		signal_watch_notify(watch, NULL, failure);
		OBJ_RELEASE(failure);
	}

	signal_watch_settle(watch);
}

/* A SignalHandle of `signo` added to `context`; NULL with an exception, the handle's ValueError for a
 * number it cannot block. */
static zend_object *signal_handle_new(const int signo, zend_object *context)
{
	zval signals;
	zval handle;

	array_init(&signals);
	add_next_index_long(&signals, signo);

	const bool made = signal_new_object(&handle, ZEND_STRL("io\\poll\\signalhandle"), &signals);

	zval_ptr_dtor(&signals);

	if (UNEXPECTED(!made)) {
		return NULL;
	}

	if (UNEXPECTED(!signal_context_add(context, Z_OBJ(handle)))) {
		zval_ptr_dtor(&handle);
		return NULL;
	}

	return Z_OBJ(handle);
}

/* The watch of `signo`, a new one in the context and not armed yet; NULL with an exception, the
 * handle's ValueError for a number it cannot block. */
static async_signal_watch_t *signal_watch_get(const int signo, zend_object *signal_case)
{
	async_signal_registry_t *registry = signal_registry();

	if (UNEXPECTED(registry == NULL)) {
		return NULL;
	}

	async_signal_watch_t *watch = registry->watches[signo];

	if (watch != NULL) {
		return watch;
	}

	zend_object *handle = signal_handle_new(signo, registry->context);

	if (UNEXPECTED(handle == NULL)) {
		signal_registry_release_if_empty(registry);
		return NULL;
	}

	watch = emalloc(sizeof(async_signal_watch_t));
	async_event_init(&watch->base, 0);
	watch->signo = signo;
	watch->signal_case = signal_case;
	watch->handle = handle;
	watch->sigwait = NULL;
	php_sigemptyset(&watch->set);
	php_sigaddset(&watch->set, signo);
	memset(&watch->info, 0, sizeof(watch->info));
	watch->fd = php_poll_signal_source_open(&watch->set);

	registry->watches[signo] = watch;
	registry->count++;
	sigaddset(&registry->watched, signo);

	return watch;
}

/* In a forked child the handle and the source are made again: a kqueue is not inherited (kqueue(2)). */
static bool signal_watch_renew(async_signal_watch_t *watch, zend_object *context)
{
	zend_object *handle = signal_handle_new(watch->signo, context);

	if (UNEXPECTED(handle == NULL)) {
		return false;
	}

	OBJ_RELEASE(watch->handle);
	watch->handle = handle;

#ifdef __linux__
	/* A signalfd is inherited; a kqueue's number is empty in the child and may name another file. */
	if (watch->fd >= 0) {
		close(watch->fd);
	}
#endif

	watch->fd = php_poll_signal_source_open(&watch->set);

	return true;
}

/* A step that fails fails that watch's Futures: the reactor's rebuild leaves no exception. */
void async_signal_rebuild(void)
{
	async_signal_registry_t *registry = ASYNC_G(signals);
	async_signal_watch_t *watches[PHP_NSIG];
	uint32_t count = 0;
	zend_object *pending_exception = NULL;

	/* A rebuild may come from the poll of a notify that left one: the calls below need none. */
	async_exception_save_fast(&EG(exception), &pending_exception);

	/* Held across the loop: an arm that completes at once may settle a watch, and the last one's
	 * free takes the registry. The rebuild dropped the parent's ops from the reactor's lists. */
	for (int signo = 1; signo < PHP_NSIG; signo++) {
		async_signal_watch_t *watch = registry->watches[signo];

		if (watch != NULL) {
			watch->base.ref_count++;
			signal_watch_disarm(watch);
			watches[count++] = watch;
		}
	}

	zval context;

	if (EXPECTED(signal_new_object(&context, ZEND_STRL("io\\poll\\context"), NULL))) {
		/* The parent's context refuses this process's add() and remove() (main/poll/poll_core.c:449-462):
		 * it goes once the handles are in the new one, so their numbers stay blocked. */
		zend_object *parent_context = registry->context;

		registry->context = Z_OBJ(context);

		for (uint32_t index = 0; index < count; index++) {
			if (UNEXPECTED(!signal_watch_renew(watches[index], registry->context) ||
						   !signal_watch_arm(watches[index]))) {
				signal_watch_fail(watches[index]);
			}
		}

		OBJ_RELEASE(parent_context);
	} else {
		zend_object *context_error = EG(exception);

		GC_ADDREF(context_error);
		zend_clear_exception();

		for (uint32_t index = 0; index < count; index++) {
			signal_watch_notify(watches[index], NULL, context_error);
		}

		OBJ_RELEASE(context_error);
	}

	for (uint32_t index = 0; index < count; index++) {
		watches[index]->base.ref_count--;
		signal_watch_settle(watches[index]);
	}

	async_exception_restore_fast(&EG(exception), &pending_exception);
}

void async_signal_request_shutdown(void)
{
	for (int signo = 1; ASYNC_G(signals) != NULL && signo < PHP_NSIG; signo++) {
		async_signal_watch_t *watch = ASYNC_G(signals)->watches[signo];

		if (watch == NULL) {
			continue;
		}

		/* A Future may outlive the request's shutdown in a variable freed after it. */
		async_event_callback_t **slots = async_callbacks_slots(&watch->base.callbacks);

		for (uint32_t index = 0; index < watch->base.callbacks.length; index++) {
			SIGNAL_WAIT_OF(slots[index], on_signal)->watch = NULL;
		}

		watch->base.callbacks.length = 0;
		signal_watch_free(watch);
	}
}

void async_signal_collector_seed(async_collector_t *collector)
{
	for (int signo = 1; ASYNC_G(signals) != NULL && signo < PHP_NSIG; signo++) {
		async_signal_watch_t *watch = ASYNC_G(signals)->watches[signo];

		if (EXPECTED(watch == NULL)) {
			continue;
		}

		async_event_callback_t **slots = async_callbacks_slots(&watch->base.callbacks);

		for (uint32_t index = 0; index < watch->base.callbacks.length; index++) {
			async_future_collector_live(collector, SIGNAL_WAIT_OF(slots[index], on_signal)->future);
		}
	}
}

///////////////////////////////////////////////////////////////////
/// The waits
///////////////////////////////////////////////////////////////////

static void
signal_wait_on_signal(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;

	async_future_event_t *future = SIGNAL_WAIT_OF(callback, on_signal)->future;

	if (EXPECTED(!(future->base.flags & ASYNC_EVENT_F_CLOSED))) {
		async_future_event_resolve(future, result, exception);
	}
}

static void
signal_wait_on_token(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) result;
	(void) exception;

	async_future_event_t *future = SIGNAL_WAIT_OF(callback, on_token)->future;
	zend_object *token_error;

	if (UNEXPECTED(future->base.flags & ASYNC_EVENT_F_CLOSED) || !async_await_token_completed(target, &token_error)) {
		return;
	}

	if (token_error == NULL) {
		token_error = async_new_exception(async_ce_cancellation, "Signal wait cancelled");
	}

	async_future_event_resolve(future, NULL, token_error);
	OBJ_RELEASE(token_error);
}

static void
signal_wait_on_future(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) callback;
	(void) result;
	(void) exception;
}

static void signal_token_release(async_awaitable_t *token)
{
	if (ASYNC_AWAITABLE_IS_TIMEOUT(token)) {
		async_timeout_unsubscribe((async_timeout_event_t *) token);
	} else {
		async_awaitable_release(token);
	}
}

/* The Future completed or went: the wait leaves the watch, which may go with it, and the token. */
static void signal_wait_leave(async_event_callback_t *callback, async_awaitable_t *target)
{
	(void) target;

	signal_wait_t *wait = SIGNAL_WAIT_OF(callback, on_future);
	async_signal_watch_t *watch = wait->watch;
	async_awaitable_t *token = wait->token;

	if (token != NULL) {
		async_callbacks_remove(async_awaitable_callbacks(token), &wait->on_token);
	}

	if (watch != NULL) {
		async_callbacks_remove(&watch->base.callbacks, &wait->on_signal);
	}

	efree(wait);

	if (watch != NULL) {
		signal_watch_settle(watch);
	}

	if (token != NULL) {
		signal_token_release(token);
	}
}

/* A hold on the token for the wait: a Timeout's subscription arms its timer. False when a Timeout
 * fired in the subscribe, or with an Error. */
static bool signal_token_hold(async_awaitable_t *token)
{
	if (ASYNC_AWAITABLE_IS_TIMEOUT(token)) {
		return async_timeout_subscribe((async_timeout_event_t *) token);
	}

	async_awaitable_addref(token);

	return true;
}

static zend_object *signal_wait_new(async_signal_watch_t *watch, async_awaitable_t *token)
{
	async_future_event_t *future;
	zend_object *object = async_future_new_pending(&future);
	signal_wait_t *wait = emalloc(sizeof(signal_wait_t));

	wait->watch = watch;
	wait->future = future;
	wait->token = token;

	wait->on_signal.flags = 0;
	wait->on_signal.callback = signal_wait_on_signal;
	wait->on_signal.dispose = NULL;
	async_callbacks_reserve(&watch->base.callbacks, 1);
	async_callbacks_push_reserved(&watch->base.callbacks, &wait->on_signal);

	wait->on_future.flags = 0;
	wait->on_future.callback = signal_wait_on_future;
	wait->on_future.dispose = signal_wait_leave;
	async_callbacks_reserve(&future->base.callbacks, 1);
	async_callbacks_push_reserved(&future->base.callbacks, &wait->on_future);

	if (token != NULL) {
		wait->on_token.flags = 0;
		wait->on_token.callback = signal_wait_on_token;
		wait->on_token.dispose = NULL;
		async_callbacks_reserve(async_awaitable_callbacks(token), 1);
		async_callbacks_push_reserved(async_awaitable_callbacks(token), &wait->on_token);
	}

	return object;
}

#endif /* PHP_WIN32 */

///////////////////////////////////////////////////////////////////
/// Async\signal()
///////////////////////////////////////////////////////////////////

ZEND_FUNCTION(Async_signal)
{
	zend_object *signal_case;
	zend_object *cancellation = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_OBJ_OF_CLASS(signal_case, async_ce_signal)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(cancellation, async_ce_completable)
	ZEND_PARSE_PARAMETERS_END();

	THROW_IF_ASYNC_OFF();

	async_awaitable_t *token = NULL;
	zend_object *token_error;

	if (cancellation != NULL) {
		token = async_await_awaitable_of(cancellation);

		if (UNEXPECTED(token == NULL)) {
			RETURN_THROWS();
		}

		if (async_await_token_completed(token, &token_error)) {
			RETURN_OBJ(signal_future_cancelled(token_error));
		}
	}

#ifdef PHP_WIN32
	zend_throw_error(NULL, "Async\\signal() is not supported on Windows");
	RETURN_THROWS();
#else
	const int signo = signal_native_number(signal_case);

	if (UNEXPECTED(signo == 0)) {
		zend_argument_value_error(1,
								  "must not be %s, which this platform lacks",
								  ZSTR_VAL(Z_STR_P(zend_enum_fetch_case_name(signal_case))));
		RETURN_THROWS();
	}

	/* A forked child rebuilds the reactor, and the watches with it, before it touches them or submits. */
	async_reactor_check_fork();

	async_signal_watch_t *watch = signal_watch_get(signo, signal_case);

	if (UNEXPECTED(watch == NULL)) {
		RETURN_THROWS();
	}

	if (token != NULL && UNEXPECTED(!signal_token_hold(token))) {
		signal_watch_settle(watch);

		if (UNEXPECTED(EG(exception) != NULL)) {
			RETURN_THROWS();
		}

		/* The Timeout fired in its subscribe. */
		const bool completed = async_await_token_completed(token, &token_error);

		ZEND_ASSERT(completed);
		(void) completed;
		RETURN_OBJ(signal_future_cancelled(token_error));
	}

	zend_object *future = signal_wait_new(watch, token);

	if (watch->sigwait == NULL && UNEXPECTED(!signal_watch_arm(watch))) {
		async_future_event_from_object(future)->base.flags |= ASYNC_FUTURE_F_IGNORED;
		OBJ_RELEASE(future);
		RETURN_THROWS();
	}

	RETURN_OBJ(future);
#endif
}
