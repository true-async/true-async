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
#include "php_true_async.h"
#include "io_provider.h"
#include "reactor.h"
#include "scheduler.h"

///////////////////////////////////////////////////////////////////
/// The IO wait
///////////////////////////////////////////////////////////////////

static const char *io_op_type_name(const php_io_op_type type)
{
	switch (type) {
		case PHP_IO_OP_POLL:
			return "poll";
		case PHP_IO_OP_TIMER:
			return "timer";
		case PHP_IO_OP_READ:
			return "read";
		case PHP_IO_OP_WRITE:
			return "write";
		case PHP_IO_OP_RECV:
			return "recv";
		case PHP_IO_OP_SEND:
			return "send";
		case PHP_IO_OP_ACCEPT:
			return "accept";
		case PHP_IO_OP_CONNECT:
			return "connect";
		case PHP_IO_OP_GETADDRINFO:
			return "getaddrinfo";
		case PHP_IO_OP_GETNAMEINFO:
			return "getnameinfo";
		case PHP_IO_OP_FSYNC:
			return "fsync";
		case PHP_IO_OP_WAITPID:
			return "waitpid";
		case PHP_IO_OP_SIGWAIT:
			return "sigwait";
		case PHP_IO_OP_ANY:
			return "any";
	}

	return "unknown";
}

/* The IO event of one run() (S6.md 3.2-3.3): a copy of the core's op, which lives on the wrapper's
 * frame. An ANY op's member copies, their pointer array and the results follow the event in the
 * same block (3.4). */
static async_io_event_t *io_wait_new(const php_io_op *op)
{
	const size_t head = ZEND_MM_ALIGNED_SIZE(sizeof(async_io_event_t));
	const uint32_t members = op->type == PHP_IO_OP_ANY ? op->u.any.n : 0;
	const size_t size = head + members * (sizeof(php_io_op) + sizeof(php_io_op *) + sizeof(php_io_op_result));

	async_io_event_t *event = async_io_event_new_ex(size);

	event->op = *op;

	/* An Accept waits for the listener to be readable and leaves the connection to the core's
	 * accept(), as TrueAsync's poll does (S6.md section 4): the Ring's multishot accept would take
	 * connections stream_select() cannot see, and close the one a cancelled wait leaves. */
	if (UNEXPECTED(op->type == PHP_IO_OP_ACCEPT)) {
		event->op.type = PHP_IO_OP_POLL;
		event->op.u.poll.events = PHP_POLL_READ;
	}

	/* The Ring refuses an infinite Timer (a sleep() past the clock's range): the latest finite
	 * deadline instead, as delay() does. */
	if (UNEXPECTED(op->type == PHP_IO_OP_TIMER && php_deadline_is_infinite(&event->op.deadline))) {
		event->op.deadline.hrtime = ZEND_HRTIME_T_MAX - 1;
	}

	if (UNEXPECTED(members > 0)) {
		php_io_op *copies = (php_io_op *) ((char *) event + head);
		php_io_op **pointers = (php_io_op **) (copies + members);
		php_io_op_result *results = (php_io_op_result *) (pointers + members);

		for (uint32_t i = 0; i < members; i++) {
			copies[i] = *op->u.any.ops[i];
			pointers[i] = &copies[i];
		}

		event->op.u.any.ops = pointers;
		event->op.u.any.results = results;
		event->op.u.any.n_results = 0;
	}

	return event;
}

/* The queue decides `in_flight`: php_io_ring_req_cancel() clears it, and the orphan sets it again
 * when the Ring keeps the op. */
static void io_wait_copy_in_flight(const async_io_event_t *event, php_io_op *op)
{
	op->in_flight = event->op.in_flight;

	if (UNEXPECTED(op->type == PHP_IO_OP_ANY)) {
		for (uint32_t i = 0; i < op->u.any.n; i++) {
			op->u.any.ops[i]->in_flight = event->op.u.any.ops[i]->in_flight;
		}
	}
}

static void io_wait_deliver(const async_io_event_t *event, php_io_op *op, php_io_op_result *result)
{
	*result = event->result;
	io_wait_copy_in_flight(event, op);

	/* The readiness an Accept waited for: the core accepts itself */
	if (UNEXPECTED(op->type == PHP_IO_OP_ACCEPT && result->status == PHP_IO_DONE && result->error == 0)) {
		result->status = PHP_IO_READY;
		result->res = 0;
	}

	if (UNEXPECTED(op->type == PHP_IO_OP_ANY)) {
		op->u.any.n_results = event->op.u.any.n_results;
		memcpy(op->u.any.results, event->op.u.any.results, op->u.any.n_results * sizeof(php_io_op_result));
	}
}

static zend_string *io_wait_info(const async_coroutine_event_callback_t *record)
{
	const async_io_event_t *event = (const async_io_event_t *) record->event;

	return zend_strpprintf(0, "await: io %s", io_op_type_name(event->op.type));
}

/* A bailout unwound run() past its release: run()'s reference goes here, before the unlink drops
 * the record's. */
static void io_wait_abort(async_coroutine_event_callback_t *record)
{
	async_io_event_release((async_io_event_t *) record->event);
}

static const async_wait_kind_t io_wait_kind = {
	.info = io_wait_info,
	.unlink = async_io_record_unlink,
	.abort = io_wait_abort,
};

static void
io_wait_wake(async_awaitable_t *target, async_event_callback_t *callback, void *result, zend_object *exception)
{
	(void) target;
	(void) result;
	(void) exception;

	async_scheduler_enqueue(&((async_coroutine_event_callback_t *) callback)->coroutine->coroutine, NULL, false);
}

///////////////////////////////////////////////////////////////////
/// The hooks
///////////////////////////////////////////////////////////////////

/* A submit the queue refused (S6.md 3.3, step 3). ENOTSUP/EOPNOTSUPP (no form for the op) and ENOSYS
 * (no queue) leave the result Unsupported: the core runs the op itself. Any other error is reported
 * as the syscall would. -1 leaves the Error of a fork rebuild pending. */
static void io_submit_failed(php_io_op_result *result, const int error)
{
	if (UNEXPECTED(error == -1 || error == ENOTSUP || error == EOPNOTSUPP || error == ENOSYS)) {
		return;
	}

	result->status = PHP_IO_DONE;
	result->res = -1;
	result->error = error;
}

/* Submits the copy and takes a completion the queue made at submit. False when the op is in flight:
 * the caller links its record or withdraws it. True when the result is written (or left
 * Unsupported) and `event` is released, with an exception pending when a fork rebuild failed. */
static bool io_wait_submit(async_io_event_t *event, php_io_op *op, php_io_op_result *result)
{
	const int error = async_io_event_try_submit(event);

	if (UNEXPECTED(error != 0)) {
		io_submit_failed(result, error);
		async_io_event_release(event);
		return true;
	}

	if (UNEXPECTED(event->base.flags & ASYNC_EVENT_F_CLOSED)) {
		io_wait_deliver(event, op, result);
		async_io_event_release(event);
		return true;
	}

	return false;
}

/* SUCCESS with result Unsupported leaves the op to the core's synchronous path. */
static zend_result io_provider_run(php_io_hooks *hooks, php_io_op *op, php_io_op_result *result)
{
	(void) hooks;

	ZEND_ASSERT(EG(exception) == NULL && "php_io_run_ex() answers itself under a pending exception");

	zend_coroutine_t *current = ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(!ZEND_ASYNC_IS_ACTIVE || current == NULL || ZEND_COROUTINE_IS_FINISHED(current) ||
				   ZEND_ASYNC_IN_SCHEDULER_CONTEXT)) {
		return SUCCESS;
	}

	if (UNEXPECTED(op->deadline.hrtime == 0)) {
		return SUCCESS;
	}

	/* A main a caught bailout left behind, or a call from a switch handler inside a suspend: neither
	 * parks, and the wait they may hold is not this run()'s to touch. */
	if (UNEXPECTED(!ZEND_COROUTINE_IS_RUNNING(current))) {
		return SUCCESS;
	}

	async_coroutine_t *coroutine = (async_coroutine_t *) current;

	/* A lookup lets the queued coroutines run first: TrueAsync's lookups always complete in a later
	 * pass of its loop (libuv's thread pool), and the Ring may complete one within this coroutine's own
	 * tick, which resumes it with no switch. */
	if (UNEXPECTED(op->type == PHP_IO_OP_GETADDRINFO || op->type == PHP_IO_OP_GETNAMEINFO)) {
		if (UNEXPECTED(!async_scheduler_enqueue(current, NULL, false) || !ZEND_ASYNC_SUSPEND())) {
			return FAILURE;
		}
	}

	ZEND_ASSERT(async_wait_is_empty(coroutine) && "a running coroutine has no linked wait");

	async_io_event_t *event = io_wait_new(op);
	async_callbacks_reserve(&event->base.callbacks, 1);

	if (io_wait_submit(event, op, result)) {
		return EG(exception) == NULL ? SUCCESS : FAILURE;
	}

	/* The record's reference; run() keeps its own and reads the event after the park, as TrueAsync's
	 * process wait does, so no unlink writes to this frame. */
	event->base.ref_count++;
	async_wait_link(&coroutine->waker.records[0], coroutine, (async_awaitable_t *) event, &io_wait_kind, io_wait_wake);

	const bool resumed = ZEND_ASYNC_SUSPEND();

	ZEND_ASSERT(coroutine->waker.records[0].event == NULL && "the wake or the cancellation unlinked the wait");

	if (EXPECTED(event->base.flags & ASYNC_EVENT_F_CLOSED)) {
		io_wait_deliver(event, op, result);
	} else {
		io_wait_copy_in_flight(event, op);

		/* A wake that is no completion and no cancellation: answered as a signal would be, EINTR at
		 * the wrapper, so the core does not run the op again beside one the queue may keep. */
		if (UNEXPECTED(resumed)) {
			result->status = PHP_IO_INTERRUPTED;
			result->res = 0;
			result->error = 0;
		}
	}

	async_io_event_release(event);

	if (EXPECTED(resumed)) {
		return SUCCESS;
	}

	/* A cancellation after the op was Done: the delivered result (an address list, a
	 * reaped status) belongs to the caller now, so it is returned with the exception pending
	 * (S6.md 3.3, step 5). */
	if (UNEXPECTED(result->status == PHP_IO_DONE && result->error == 0)) {
		return SUCCESS;
	}

	return FAILURE;
}

static void io_provider_add(php_io_hooks *hooks, php_io_registration *reg)
{
	(void) hooks;

	/* A forked child before its rebuild has no queue of its own: the registration is skipped, so its
	 * waits stay one-shot (S6.md section 2). */
	php_io_queue *queue = async_reactor_live_queue();

	if (EXPECTED(queue != NULL)) {
		queue->ops->add(queue, reg);
	}
}

static void io_provider_remove(php_io_hooks *hooks, php_io_registration *reg)
{
	(void) hooks;

	php_io_queue *queue = async_reactor_live_queue();

	if (EXPECTED(queue != NULL)) {
		queue->ops->remove(queue, reg);
	}
}

/* The struct lives in ASYNC_G; the core ends the registrations itself. */
static void io_provider_dtor(php_io_hooks *hooks)
{
	async_io_provider_t *provider = ZEND_CONTAINER_OF(hooks, async_io_provider_t, hooks);

	provider->installed = false;
}

static const php_io_hooks_ops io_provider_ops = {
	.run = io_provider_run,
	.add = io_provider_add,
	.remove = io_provider_remove,
	.dtor = io_provider_dtor,
};

///////////////////////////////////////////////////////////////////
/// Install
///////////////////////////////////////////////////////////////////

/* Files stay on the thread (S6.md section 6); an Accept waits for readiness (S6.md section 4). */
static uint32_t io_queue_hook_flags(php_io_queue *queue)
{
	return queue->ops->hook_flags(queue) & ~(PHP_IO_HOOKS_F_FILES | PHP_IO_HOOKS_F_DIRECT_ACCEPT);
}

void async_io_provider_request_startup(void)
{
	async_io_provider_t *provider = &ASYNC_G(io_provider);

	provider->hooks.ops = &io_provider_ops;
	provider->hooks.flags = 0;
	provider->installed = false;
	provider->installed_once = false;
}

void async_io_provider_request_shutdown(void)
{
	async_io_provider_t *provider = &ASYNC_G(io_provider);

	if (EXPECTED(provider->installed)) {
		php_io_hooks_register(NULL);
	}
}

void async_io_provider_install(void)
{
	async_io_provider_t *provider = &ASYNC_G(io_provider);

	if (UNEXPECTED(provider->installed_once)) {
		return;
	}

	php_io_queue *queue = ASYNC_G(reactor).queue;

	provider->hooks.flags = queue != NULL ? io_queue_hook_flags(queue) : 0;

	/* Refused while a provider from Io\Hooks\set_hooks() is registered: installed_once stays false,
	 * so the first trigger after that provider is removed installs this one. */
	provider->installed = php_io_hooks_register(&provider->hooks) == SUCCESS;
	provider->installed_once = provider->installed;
}

void async_io_provider_queue_destroyed(void)
{
	ASYNC_G(io_provider).hooks.flags = 0;
}

void async_io_provider_queue_created(php_io_queue *queue)
{
	async_io_provider_t *provider = &ASYNC_G(io_provider);

	if (EXPECTED(provider->installed)) {
		provider->hooks.flags = io_queue_hook_flags(queue);
		return;
	}

	async_io_provider_install();
}
