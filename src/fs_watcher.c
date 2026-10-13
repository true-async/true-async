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
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "main/fopen_wrappers.h"
#include "php_true_async.h"
#include "coroutine.h"
#include "exceptions.h"
#include "fs_watcher.h"
#include "scheduler.h"
#include "internal/zval_circular_buffer.h"
#include "test_hooks.h"
#include "fs_watcher_arginfo.h"

#ifdef __linux__
#include <dirent.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* TrueAsync's FileSystemWatcher (fs_watcher.c of ext/async, cited below by line) without libuv: on Linux
 * the request's watchers share one inotify descriptor, as libuv's loop does (src/unix/linux.c:2298), and
 * the reactor polls it with one POLL op on its own list. A completion reads every event and hands each to
 * the watchers holding its watch descriptor; a coroutine in foreach parks a WATCHER record on its watcher,
 * and while one is parked the scheduler waits instead of reporting a deadlock (S9-fs-watcher.md, 3). */

zend_class_entry *async_ce_fs_watcher = NULL;
zend_class_entry *async_ce_filesystem_event = NULL;

static zend_object_handlers fs_watcher_handlers;

/* Bits of the watcher's event flags. ASYNC_EVENT_F_CLOSED: not watching, before the constructor too. */
#define FS_WATCHER_F_CONSTRUCTED (1u << ASYNC_EVENT_F_TYPE_SHIFT)
#define FS_WATCHER_F_COALESCE (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 1))
#define FS_WATCHER_F_RECURSIVE (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 2))
/* A debounced burst waits for the timer. */
#define FS_WATCHER_F_BURST (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 3))
#define FS_WATCHER_F_BURST_RENAMED (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 4))
#define FS_WATCHER_F_BURST_CHANGED (1u << (ASYNC_EVENT_F_TYPE_SHIFT + 5))

/* The libuv mask (src/unix/linux.c:2502-2509). */
#define FS_WATCH_EVENTS \
	(IN_ATTRIB | IN_CREATE | IN_MODIFY | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO)

/* One watch descriptor per directory of a recursive watch (libuv_reactor.c:2921). */
#define FS_WATCHER_MAX_NODES 8192

typedef struct _fs_watch_node_s fs_watch_node_t;

typedef struct
{
	async_event_t base; /* the parked iterators' WATCHER records; ZEND_OBJ: inside `std`'s allocation */
	zend_string *path;  /* as passed: every event's $path */

	union
	{
		HashTable coalesced;        /* COALESCE: event objects by "path/filename", in first-seen order */
		zval_circular_buffer_t raw; /* every event object, oldest first */
	};

	fs_watch_node_t **nodes; /* nodes[0] is the root while watching; the subdirectories follow */
	uint32_t node_count;
	uint32_t node_capacity;
	uint32_t debounce_ms; /* 0: every change delivered as it comes */
	uint32_t max_hold_ms; /* 0: a burst waits for its quiet as long as it lasts */
	zend_hrtime_t burst_start;
	HashTable *extensions;                    /* NULL: every entry; else lower-case extensions without the dot */
	zend_object *error;                       /* a monitoring error the next step of an empty walk throws, once */
	async_io_event_t *debounce_timer;         /* while a burst waits: a Timer op on the reactor's own list */
	async_event_callback_t debounce_callback; /* in `debounce_timer`'s vector while armed */
	zend_object std;
} async_fs_watcher_t;

static zend_always_inline async_fs_watcher_t *fs_watcher_from_object(zend_object *object)
{
	return (async_fs_watcher_t *) ((char *) object - offsetof(async_fs_watcher_t, std));
}

#define THIS_WATCHER fs_watcher_from_object(Z_OBJ_P(ZEND_THIS))

static zend_always_inline bool fs_watcher_is_closed(const async_fs_watcher_t *watcher)
{
	return (watcher->base.flags & ASYNC_EVENT_F_CLOSED) != 0;
}

///////////////////////////////////////////////////////////////////
/// The buffer
///////////////////////////////////////////////////////////////////

/* A FileSystemEvent; `filename` may be NULL. */
static void fs_event_object_new(zval *result, zend_string *path, zend_string *filename, bool renamed, bool changed)
{
	object_init_ex(result, async_ce_filesystem_event);

	zend_object *const event_object = Z_OBJ_P(result);

	ZVAL_STR_COPY(OBJ_PROP_NUM(event_object, 0), path);

	if (filename != NULL) {
		ZVAL_STR_COPY(OBJ_PROP_NUM(event_object, 1), filename);
	} else {
		ZVAL_NULL(OBJ_PROP_NUM(event_object, 1));
	}

	ZVAL_BOOL(OBJ_PROP_NUM(event_object, 2), renamed);
	ZVAL_BOOL(OBJ_PROP_NUM(event_object, 3), changed);
}

/* Coalesce mode merges a change into the entry's buffered event (fs_watcher.c:288-314). */
static void fs_watcher_buffer(async_fs_watcher_t *watcher, zend_string *filename, bool renamed, bool changed)
{
	zval event_object;

	if (!(watcher->base.flags & FS_WATCHER_F_COALESCE)) {
		fs_event_object_new(&event_object, watcher->path, filename, renamed, changed);
		zval_circular_buffer_push(&watcher->raw, &event_object, UINT32_MAX);
		zval_ptr_dtor(&event_object);
		return;
	}

	zend_string *const key = filename != NULL ? zend_strpprintf(0, "%s/%s", ZSTR_VAL(watcher->path), ZSTR_VAL(filename))
											  : zend_string_copy(watcher->path);
	zval *const buffered = zend_hash_find(&watcher->coalesced, key);

	if (buffered != NULL) {
		zend_object *const buffered_object = Z_OBJ_P(buffered);

		if (renamed) {
			ZVAL_TRUE(OBJ_PROP_NUM(buffered_object, 2));
		}

		if (changed) {
			ZVAL_TRUE(OBJ_PROP_NUM(buffered_object, 3));
		}
	} else {
		fs_event_object_new(&event_object, watcher->path, filename, renamed, changed);
		zend_hash_add_new(&watcher->coalesced, key, &event_object);
	}

	zend_string_release(key);
}

/* Moves the oldest buffered event into `result`; false when none is buffered. */
static bool fs_watcher_pop(async_fs_watcher_t *watcher, zval *result)
{
	if (!(watcher->base.flags & FS_WATCHER_F_COALESCE)) {
		if (watcher->raw.count == 0) {
			return false;
		}

		zval_circular_buffer_pop(&watcher->raw, result);
		return true;
	}

	/* The internal pointer moves past each popped key, so no pop scans the holes before it. */
	HashTable *const coalesced = &watcher->coalesced;
	const HashPosition position = zend_hash_get_current_pos(coalesced);

	if (position >= coalesced->nNumUsed) {
		return false;
	}

	ZVAL_COPY(result, &coalesced->arData[position].val);
	zend_hash_move_forward(coalesced);
	zend_hash_del_bucket(coalesced, coalesced->arData + position);

	return true;
}

/* Wakes every parked iterator: the first to run takes the events, as TrueAsync's (probe f2). A parked
 * coroutine holds the watcher through its iterator, so the wakes free nothing. */
static void fs_watcher_wake(async_fs_watcher_t *watcher)
{
	if (watcher->base.callbacks.length != 0) {
		async_callbacks_notify((async_awaitable_t *) &watcher->base, &watcher->base.callbacks, NULL, NULL);
	}
}

///////////////////////////////////////////////////////////////////
/// Extensions and debounce
///////////////////////////////////////////////////////////////////

/* Whether the entry's extension is listed; a NULL `filename` (lost changes) may have matched. In every
 * mode, unlike TrueAsync's debounce-only filter (Edmond, 2026-10-13, S9-fs-watcher.md 7, question 1). */
static bool fs_watcher_extension_matches(const async_fs_watcher_t *watcher, const zend_string *filename)
{
	if (watcher->extensions == NULL || filename == NULL) {
		return true;
	}

	const char *const name = ZSTR_VAL(filename);
	const char *const slash = strrchr(name, '/');
	const char *const basename = slash != NULL ? slash + 1 : name;
	const char *const dot = strrchr(basename, '.');

	if (dot == NULL || dot[1] == '\0') {
		return false;
	}

	const size_t extension_length = ZSTR_VAL(filename) + ZSTR_LEN(filename) - (dot + 1);
	zend_string *const extension = zend_string_init(dot + 1, extension_length, false);

	zend_str_tolower(ZSTR_VAL(extension), extension_length);

	const bool matches = zend_hash_exists(watcher->extensions, extension);

	zend_string_release(extension);

	return matches;
}

/* A timer that fired already is only unlinked by the orphan. */
static void fs_watcher_debounce_disarm(async_fs_watcher_t *watcher)
{
	async_io_event_t *const timer = watcher->debounce_timer;

	if (timer == NULL) {
		return;
	}

	watcher->debounce_timer = NULL;
	async_io_event_orphan(timer);
	async_callbacks_remove(&timer->base.callbacks, &watcher->debounce_callback);
	async_io_event_release(timer);
}

/* The burst becomes one event with no name (fs_watcher.c:161-200). */
static void fs_watcher_burst_buffer(async_fs_watcher_t *watcher)
{
	const uint32_t flags = watcher->base.flags;

	watcher->base.flags &= ~(FS_WATCHER_F_BURST | FS_WATCHER_F_BURST_RENAMED | FS_WATCHER_F_BURST_CHANGED);
	fs_watcher_buffer(
			watcher, NULL, (flags & FS_WATCHER_F_BURST_RENAMED) != 0, (flags & FS_WATCHER_F_BURST_CHANGED) != 0);
}

/* The quiet timer fired. In scheduler context. */
static void fs_watcher_debounce_fire(async_awaitable_t *target,
									 async_event_callback_t *callback,
									 void *result,
									 zend_object *exception)
{
	(void) target;
	(void) result;
	(void) exception;

	async_fs_watcher_t *const watcher =
			(async_fs_watcher_t *) ((char *) callback - offsetof(async_fs_watcher_t, debounce_callback));

	fs_watcher_debounce_disarm(watcher);
	fs_watcher_burst_buffer(watcher);
	fs_watcher_wake(watcher);
}

/* (Re)arms the quiet timer: it fires after debounce_ms of quiet, no later than max_hold_ms after the burst's
 * first change (fs_watcher.c:219-268). A submit that fails leaves the burst to the next change. */
static void fs_watcher_debounce(async_fs_watcher_t *watcher, bool renamed, bool changed)
{
	const zend_hrtime_t now = zend_hrtime();

	if (!(watcher->base.flags & FS_WATCHER_F_BURST)) {
		watcher->base.flags |= FS_WATCHER_F_BURST;
		watcher->burst_start = now;
	}

	watcher->base.flags |= (renamed ? FS_WATCHER_F_BURST_RENAMED : 0) | (changed ? FS_WATCHER_F_BURST_CHANGED : 0);

	zend_long delay_ms = watcher->debounce_ms;

	if (watcher->max_hold_ms != 0) {
		const zend_hrtime_t deadline = watcher->burst_start + (zend_hrtime_t) watcher->max_hold_ms * 1000000;
		const zend_long remaining_ms = deadline > now ? (zend_long) ((deadline - now) / 1000000) : 0;

		delay_ms = MIN(delay_ms, remaining_ms);
	}

	fs_watcher_debounce_disarm(watcher);

	async_io_event_t *const timer = async_io_event_new();

	php_io_op_timer(&timer->op, async_reactor_deadline_from_ms(MAX(delay_ms, 1)));
	async_callbacks_reserve(&timer->base.callbacks, 1);
	async_callbacks_push_reserved(&timer->base.callbacks, &watcher->debounce_callback);
	watcher->debounce_timer = timer;

	if (UNEXPECTED(async_reactor_try_submit_own(timer) != 0)) {
		watcher->debounce_timer = NULL;
		async_callbacks_remove(&timer->base.callbacks, &watcher->debounce_callback);
		async_io_event_release(timer);
	}
}

/* A change after the filter: into the debounce or the buffer, and the parked iterators woken. */
static void fs_watcher_deliver(async_fs_watcher_t *watcher, zend_string *filename, bool renamed, bool changed)
{
	if (!fs_watcher_extension_matches(watcher, filename)) {
		return;
	}

	if (watcher->debounce_ms != 0) {
		fs_watcher_debounce(watcher, renamed, changed);
		return;
	}

	fs_watcher_buffer(watcher, filename, renamed, changed);
	fs_watcher_wake(watcher);
}

#ifdef __linux__

///////////////////////////////////////////////////////////////////
/// The registry
///////////////////////////////////////////////////////////////////

/* One watch a watcher holds: its root or a subdirectory of a recursive watch. */
struct _fs_watch_node_s
{
	async_fs_watcher_t *watcher;
	zend_string *path;   /* absolute */
	zend_string *prefix; /* relative to the watcher's root, "" for the root */
	int descriptor;      /* the watch descriptor, shared by the nodes of one inode */
};

/* The nodes of one watch descriptor: a recursive watcher on /a and a plain one on /a/sub share the one of
 * /a/sub, and name its entries differently. */
typedef struct
{
	fs_watch_node_t **nodes;
	uint32_t count;
	uint32_t capacity;
} fs_watch_users_t;

struct _async_fs_registry_s
{
	int fd;                 /* the inotify descriptor, non-blocking */
	pid_t pid;              /* the process that made `fd` */
	HashTable watches;      /* fs_watch_users_t by watch descriptor */
	HashTable watchers;     /* the open watchers by object handle; borrowed */
	async_io_event_t *poll; /* the POLL op on `fd`, on the reactor's own list while submitted; one reference */
	bool draining;          /* a completion reads `fd`: the last watcher's close leaves the registry to it */
};

static void fs_watch_users_free(zval *users_zval)
{
	fs_watch_users_t *const users = Z_PTR_P(users_zval);

	efree(users->nodes);
	efree(users);
}

/* Links `node` to its descriptor's users. */
static void fs_registry_watch_add(async_fs_registry_t *registry, fs_watch_node_t *node)
{
	fs_watch_users_t *users = zend_hash_index_find_ptr(&registry->watches, (zend_ulong) node->descriptor);

	if (users == NULL) {
		users = ecalloc(1, sizeof(fs_watch_users_t));
		zend_hash_index_add_new_ptr(&registry->watches, (zend_ulong) node->descriptor, users);
	}

	if (users->count == users->capacity) {
		users->capacity = users->capacity == 0 ? 2 : users->capacity * 2;
		users->nodes = safe_erealloc(users->nodes, users->capacity, sizeof(fs_watch_node_t *), 0);
	}

	users->nodes[users->count++] = node;
}

/* Unlinks `node`; the descriptor's last user removes the kernel's watch unless the kernel dropped it. */
static void fs_registry_watch_remove(async_fs_registry_t *registry, const fs_watch_node_t *node, bool dropped)
{
	fs_watch_users_t *const users = zend_hash_index_find_ptr(&registry->watches, (zend_ulong) node->descriptor);

	if (UNEXPECTED(users == NULL)) {
		return;
	}

	for (uint32_t i = 0; i < users->count; i++) {
		if (users->nodes[i] == node) {
			users->nodes[i] = users->nodes[--users->count];
			break;
		}
	}

	if (users->count != 0) {
		return;
	}

	/* A forked child that has not rebuilt yet shares the parent's inotify instance: its watches stay. */
	if (!dropped && EXPECTED(registry->pid == getpid())) {
		inotify_rm_watch(registry->fd, node->descriptor);
	}

	zend_hash_index_del(&registry->watches, (zend_ulong) node->descriptor);
}

static void fs_registry_poll_complete(async_io_event_t *event);

/* The request's registry, made by its first watcher; NULL with an exception. */
static async_fs_registry_t *fs_registry_get(void)
{
	async_fs_registry_t *registry = ASYNC_G(fs_watchers);

	if (EXPECTED(registry != NULL)) {
		return registry;
	}

	const int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

	if (UNEXPECTED(fd < 0)) {
		zend_throw_exception_ex(async_ce_async_exception, 0, "Failed to start filesystem handle: %s", strerror(errno));
		return NULL;
	}

	registry = emalloc(sizeof(async_fs_registry_t));
	registry->fd = fd;
	registry->pid = getpid();
	zend_hash_init(&registry->watches, 8, NULL, fs_watch_users_free, false);
	zend_hash_init(&registry->watchers, 8, NULL, NULL, false);
	registry->draining = false;

	async_io_event_t *const poll = async_io_event_new();

	poll->complete = fs_registry_poll_complete;
	php_io_op_poll(&poll->op, NULL, fd, PHP_POLL_READ, php_io_deadline_infinite());
	registry->poll = poll;
	ASYNC_G(fs_watchers) = registry;

	/* Nothing is ready on a new descriptor, so the submit does not complete inline. */
	if (UNEXPECTED(async_reactor_submit_own(poll) == FAILURE)) {
		ASYNC_G(fs_watchers) = NULL;
		async_io_event_release(poll);
		zend_hash_destroy(&registry->watchers);
		zend_hash_destroy(&registry->watches);
		close(fd);
		efree(registry);
		return NULL;
	}

	return registry;
}

/* With its last watcher, unless a completion is reading it. */
static void fs_registry_release_if_empty(async_fs_registry_t *registry)
{
	if (zend_hash_num_elements(&registry->watchers) != 0 || registry->draining) {
		return;
	}

	ZEND_ASSERT(ASYNC_G(reactor).parked_watchers == 0 && "a parked iterator's watcher is open");

	if (registry->poll != NULL) {
		async_io_event_orphan(registry->poll);
		async_io_event_release(registry->poll);
	}

	zend_hash_destroy(&registry->watchers);
	zend_hash_destroy(&registry->watches);
	close(registry->fd);
	efree(registry);
	ASYNC_G(fs_watchers) = NULL;
}

///////////////////////////////////////////////////////////////////
/// Nodes
///////////////////////////////////////////////////////////////////

static void fs_watch_node_free(fs_watch_node_t *node)
{
	zend_string_release(node->path);
	zend_string_release(node->prefix);
	efree(node);
}

/* Adds a watch on `path` for the watcher; NULL with errno when the kernel refuses it or a recursive watch
 * holds its 8192 directories. A subdirectory is added with IN_ONLYDIR | IN_DONT_FOLLOW, so one replaced by a
 * symlink after the walk saw it is refused, not followed. */
static fs_watch_node_t *
fs_watch_node_add(async_fs_registry_t *registry, async_fs_watcher_t *watcher, zend_string *path, zend_string *prefix)
{
	const bool is_root = ZSTR_LEN(prefix) == 0;

	if (UNEXPECTED(watcher->node_count >= FS_WATCHER_MAX_NODES)) {
		errno = ENOSPC;
		return NULL;
	}

	const int descriptor = inotify_add_watch(
			registry->fd, ZSTR_VAL(path), FS_WATCH_EVENTS | (is_root ? 0 : IN_ONLYDIR | IN_DONT_FOLLOW));

	if (UNEXPECTED(descriptor < 0)) {
		return NULL;
	}

	/* Two subdirectories of one watcher never share an inode: a hard link to a directory does not exist. */
	fs_watch_node_t *const node = emalloc(sizeof(fs_watch_node_t));

	node->watcher = watcher;
	node->path = zend_string_copy(path);
	node->prefix = zend_string_copy(prefix);
	node->descriptor = descriptor;

	if (watcher->node_count == watcher->node_capacity) {
		watcher->node_capacity = watcher->node_capacity == 0 ? 4 : watcher->node_capacity * 2;
		watcher->nodes = safe_erealloc(watcher->nodes, watcher->node_capacity, sizeof(fs_watch_node_t *), 0);
	}

	watcher->nodes[watcher->node_count++] = node;
	fs_registry_watch_add(registry, node);

	return node;
}

/* Removes the watcher's node `index`; `dropped`: the kernel removed its watch already (IN_IGNORED). */
static void
fs_watch_node_remove_at(async_fs_registry_t *registry, async_fs_watcher_t *watcher, uint32_t index, bool dropped)
{
	fs_watch_node_t *const node = watcher->nodes[index];

	fs_registry_watch_remove(registry, node, dropped);
	watcher->nodes[index] = watcher->nodes[--watcher->node_count];
	fs_watch_node_free(node);
}

static void
fs_watch_tree_add(async_fs_registry_t *registry, async_fs_watcher_t *watcher, zend_string *path, zend_string *prefix);

/* Watches every subdirectory under `path`, symlinks skipped (libuv_reactor.c:3204-3253). A directory the
 * kernel refuses, or one past the cap, stays unwatched without an error, as in TrueAsync. */
static void fs_watch_tree_walk(async_fs_registry_t *registry,
							   async_fs_watcher_t *watcher,
							   const zend_string *path,
							   const zend_string *prefix)
{
	DIR *const directory = opendir(ZSTR_VAL(path));

	if (directory == NULL) {
		return;
	}

	const struct dirent *entry;

	while ((entry = readdir(directory)) != NULL && watcher->node_count < FS_WATCHER_MAX_NODES) {
		if (entry->d_name[0] == '.' &&
			(entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
			continue;
		}

		zend_string *const child_path = zend_strpprintf(0, "%s/%s", ZSTR_VAL(path), entry->d_name);
		bool is_directory = entry->d_type == DT_DIR;

		if (entry->d_type == DT_UNKNOWN) {
			struct stat status;

			is_directory = lstat(ZSTR_VAL(child_path), &status) == 0 && S_ISDIR(status.st_mode);
		}

		if (is_directory) {
			zend_string *const child_prefix = ZSTR_LEN(prefix) != 0
					? zend_strpprintf(0, "%s/%s", ZSTR_VAL(prefix), entry->d_name)
					: zend_string_init(entry->d_name, strlen(entry->d_name), false);

			fs_watch_tree_add(registry, watcher, child_path, child_prefix);
			zend_string_release(child_prefix);
		}

		zend_string_release(child_path);
	}

	closedir(directory);
}

/* A subdirectory created in or moved into a recursive watch, with its subtree. */
static void
fs_watch_tree_add(async_fs_registry_t *registry, async_fs_watcher_t *watcher, zend_string *path, zend_string *prefix)
{
	for (uint32_t i = 0; i < watcher->node_count; i++) {
		if (zend_string_equals(watcher->nodes[i]->path, path)) {
			return;
		}
	}

	if (fs_watch_node_add(registry, watcher, path, prefix) != NULL) {
		fs_watch_tree_walk(registry, watcher, path, prefix);
	}
}

/* A subdirectory deleted or moved out: its node and every node under it (libuv_reactor.c:3007-3030). */
static void fs_watch_tree_remove(async_fs_registry_t *registry, async_fs_watcher_t *watcher, const zend_string *path)
{
	uint32_t i = 1;

	while (i < watcher->node_count) {
		const zend_string *const node_path = watcher->nodes[i]->path;
		const bool is_under = ZSTR_LEN(node_path) >= ZSTR_LEN(path) &&
				memcmp(ZSTR_VAL(node_path), ZSTR_VAL(path), ZSTR_LEN(path)) == 0 &&
				(ZSTR_LEN(node_path) == ZSTR_LEN(path) || ZSTR_VAL(node_path)[ZSTR_LEN(path)] == '/');

		if (is_under) {
			fs_watch_node_remove_at(registry, watcher, i, false);
			continue;
		}

		i++;
	}
}

///////////////////////////////////////////////////////////////////
/// Closing
///////////////////////////////////////////////////////////////////

/* Stops watching and wakes the iterators; the buffered events stay for them. `dropped`: the kernel removed
 * the root's watch. */
static void fs_watcher_close(async_fs_watcher_t *watcher, bool dropped)
{
	if (fs_watcher_is_closed(watcher)) {
		return;
	}

	async_fs_registry_t *const registry = ASYNC_G(fs_watchers);

	watcher->base.flags |= ASYNC_EVENT_F_CLOSED;
	fs_watcher_debounce_disarm(watcher);

	while (watcher->node_count > 0) {
		fs_watch_node_remove_at(registry, watcher, watcher->node_count - 1, dropped && watcher->node_count == 1);
	}

	zend_hash_index_del(&registry->watchers, watcher->std.handle);

	/* The burst read before the close comes out, as the events of a watcher without a debounce do; after the
	 * registry lets go, since a bailout in the allocation leaves free_obj nothing to close. */
	if (watcher->base.flags & FS_WATCHER_F_BURST) {
		fs_watcher_burst_buffer(watcher);
	}

	fs_watcher_wake(watcher);
	fs_registry_release_if_empty(registry);
}

/* Closes every open watcher with `error`, which each iterator throws once. */
static void fs_registry_fail(async_fs_registry_t *registry, const char *reason)
{
	uint32_t count = zend_hash_num_elements(&registry->watchers);
	async_fs_watcher_t **const watchers = safe_emalloc(count, sizeof(async_fs_watcher_t *), 0);
	uint32_t index = 0;
	async_fs_watcher_t *watcher;

	ZEND_HASH_FOREACH_PTR(&registry->watchers, watcher)
	{
		watchers[index++] = watcher;
	}
	ZEND_HASH_FOREACH_END();

	for (uint32_t i = 0; i < count; i++) {
		if (watchers[i]->error == NULL) {
			watchers[i]->error =
					async_new_exception(async_ce_async_exception, "Filesystem monitoring error: %s", reason);
		}

		fs_watcher_close(watchers[i], false);
	}

	efree(watchers);
}

///////////////////////////////////////////////////////////////////
/// Events
///////////////////////////////////////////////////////////////////

/* One kernel event for one node (libuv's mapping, src/unix/linux.c:2431-2444). */
static void
fs_watch_node_handle(async_fs_registry_t *registry, fs_watch_node_t *node, const struct inotify_event *kernel_event)
{
	async_fs_watcher_t *const watcher = node->watcher;
	const bool is_root = ZSTR_LEN(node->prefix) == 0;
	const uint32_t mask = kernel_event->mask;

	if (mask & IN_IGNORED) {
		if (is_root) {
			fs_watcher_close(watcher, true);
			return;
		}

		for (uint32_t i = 1; i < watcher->node_count; i++) {
			if (watcher->nodes[i] == node) {
				fs_watch_node_remove_at(registry, watcher, i, true);
				break;
			}
		}

		return;
	}

	/* A subdirectory's own events: its parent reported the entry (S9-fs-watcher.md 6, item 11). */
	if (kernel_event->len == 0 && !is_root) {
		return;
	}

	zend_string *filename;

	if (kernel_event->len == 0) {
		const char *const slash = strrchr(ZSTR_VAL(node->path), '/');
		const char *const basename = slash != NULL ? slash + 1 : ZSTR_VAL(node->path);

		filename = zend_string_init(basename, strlen(basename), false);
	} else if (is_root) {
		filename = zend_string_init(kernel_event->name, strlen(kernel_event->name), false);
	} else {
		filename = zend_strpprintf(0, "%s/%s", ZSTR_VAL(node->prefix), kernel_event->name);
	}

	if ((watcher->base.flags & FS_WATCHER_F_RECURSIVE) && kernel_event->len != 0 && (mask & IN_ISDIR)) {
		zend_string *const child_path = zend_strpprintf(0, "%s/%s", ZSTR_VAL(node->path), kernel_event->name);

		if (mask & (IN_CREATE | IN_MOVED_TO)) {
			fs_watch_tree_add(registry, watcher, child_path, filename);
		} else if (mask & (IN_DELETE | IN_MOVED_FROM)) {
			fs_watch_tree_remove(registry, watcher, child_path);
		}

		zend_string_release(child_path);
	}

	const bool changed = (mask & (IN_ATTRIB | IN_MODIFY)) != 0;
	const bool renamed =
			(mask & (IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO)) != 0;

	fs_watcher_deliver(watcher, filename, renamed, changed);
	zend_string_release(filename);
}

/* An overflow lost events of every watcher: each gets one event with no name and both flags (S9-fs-watcher.md
 * 6, item 8). */
static void fs_registry_overflow(async_fs_registry_t *registry)
{
	async_fs_watcher_t *watcher;

	ZEND_HASH_FOREACH_PTR(&registry->watchers, watcher)
	{
		fs_watcher_deliver(watcher, NULL, true, true);
	}
	ZEND_HASH_FOREACH_END();
}

/* Hands one kernel event to the nodes of its descriptor. A node may go while an earlier one is handled (a
 * watcher closed by its root's IN_IGNORED takes all its nodes), so each is looked up again. */
static void fs_registry_dispatch(async_fs_registry_t *registry, const struct inotify_event *kernel_event)
{
	if (kernel_event->mask & IN_Q_OVERFLOW) {
		fs_registry_overflow(registry);
		return;
	}

	const fs_watch_users_t *users = zend_hash_index_find_ptr(&registry->watches, (zend_ulong) kernel_event->wd);

	if (users == NULL) {
		return;
	}

	const uint32_t count = users->count;
	fs_watch_node_t **const nodes = safe_emalloc(count, sizeof(fs_watch_node_t *), 0);

	memcpy(nodes, users->nodes, count * sizeof(fs_watch_node_t *));

	for (uint32_t i = 0; i < count; i++) {
		users = zend_hash_index_find_ptr(&registry->watches, (zend_ulong) kernel_event->wd);

		bool is_linked = false;

		for (uint32_t j = 0; users != NULL && j < users->count; j++) {
			is_linked |= users->nodes[j] == nodes[i];
		}

		if (is_linked) {
			fs_watch_node_handle(registry, nodes[i], kernel_event);
		}
	}

	efree(nodes);
}

/* Reads the descriptor until EAGAIN; false with errno on another error. */
static bool fs_registry_drain(async_fs_registry_t *registry)
{
	/* Holds one event of the longest name (src/unix/linux.c:2412). */
	char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));

	if (UNEXPECTED(ASYNC_TEST_IO_FAULT(ASYNC_TEST_IO_FAULT_INOTIFY_OVERFLOW))) {
		fs_registry_overflow(registry);
	}

	while (zend_hash_num_elements(&registry->watchers) != 0) {
		const ssize_t length = read(registry->fd, buffer, sizeof(buffer));

		if (length < 0) {
			if (errno == EINTR) {
				continue;
			}

			return errno == EAGAIN;
		}

		for (const char *position = buffer; position < buffer + length;) {
			const struct inotify_event *const kernel_event = (const struct inotify_event *) position;

			fs_registry_dispatch(registry, kernel_event);
			position += sizeof(struct inotify_event) + kernel_event->len;
		}
	}

	return true;
}

/* The POLL op's completion: reads every event and polls again; an inline completion of the resubmit loops
 * here instead of a recursive dispatch, as the wakeup's does (reactor.c, wakeup_complete). A poll that ends
 * without readiness, a read error or a failed resubmit closes every watcher with the error. */
static void fs_registry_poll_complete(async_io_event_t *event)
{
	async_fs_registry_t *const registry = ASYNC_G(fs_watchers);
	bool completed;

	ZEND_ASSERT(registry != NULL && registry->poll == event);

	registry->draining = true;

	do {
		const php_io_op_result result = event->result;

		completed = false;

		if (UNEXPECTED(result.status != PHP_IO_DONE || result.error != 0 || result.res <= 0)) {
			registry->poll = NULL;
			async_io_event_release(event);
			fs_registry_fail(registry, strerror(result.error != 0 ? result.error : EIO));
			break;
		}

		if (UNEXPECTED(!fs_registry_drain(registry))) {
			fs_registry_fail(registry, strerror(errno));
		}

		if (zend_hash_num_elements(&registry->watchers) == 0) {
			break;
		}

		const int error = async_reactor_resubmit_own(event, &completed);

		if (UNEXPECTED(error != 0)) {
			registry->poll = NULL;
			async_io_event_release(event);
			fs_registry_fail(registry, strerror(error));
			break;
		}
	} while (UNEXPECTED(completed));

	registry->draining = false;
	fs_registry_release_if_empty(registry);
}

void async_fs_watcher_rebuild(void)
{
	async_fs_registry_t *const registry = ASYNC_G(fs_watchers);
	const uint32_t count = zend_hash_num_elements(&registry->watchers);
	async_fs_watcher_t **const watchers = safe_emalloc(count, sizeof(async_fs_watcher_t *), 0);
	int *const root_errors = safe_emalloc(count, sizeof(int), 0); /* why each root's watch was refused, or 0 */
	uint32_t index = 0;
	async_fs_watcher_t *watcher;

	ZEND_HASH_FOREACH_PTR(&registry->watchers, watcher)
	{
		watchers[index++] = watcher;
	}
	ZEND_HASH_FOREACH_END();

	close(registry->fd);
	zend_hash_clean(&registry->watches);
	registry->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	registry->pid = getpid();

	const int open_error = registry->fd < 0 ? errno : 0;

	for (uint32_t i = 0; i < count; i++) {
		watcher = watchers[i];
		root_errors[i] = 0;

		/* Backwards: a refused subdirectory is replaced by the last node, which is done already. */
		for (uint32_t node_index = watcher->node_count; node_index-- > 0;) {
			fs_watch_node_t *const node = watcher->nodes[node_index];

			node->descriptor = open_error == 0
					? inotify_add_watch(registry->fd,
										ZSTR_VAL(node->path),
										FS_WATCH_EVENTS | (node_index == 0 ? 0 : IN_ONLYDIR | IN_DONT_FOLLOW))
					: -1;

			if (EXPECTED(node->descriptor >= 0)) {
				fs_registry_watch_add(registry, node);
			} else if (node_index == 0) {
				root_errors[i] = open_error != 0 ? open_error : errno;
			} else {
				watcher->nodes[node_index] = watcher->nodes[--watcher->node_count];
				fs_watch_node_free(node);
			}
		}
	}

	if (registry->poll != NULL && registry->poll->reactor_link.prev != NULL && open_error == 0) {
		php_io_op_poll(&registry->poll->op, NULL, registry->fd, PHP_POLL_READ, php_io_deadline_infinite());
	}

	/* A watch that cannot be added again closes its watcher, which may take the registry with the last one:
	 * after the re-arm, so the resubmit of the reactor's own ops finds a consistent list. */
	registry->draining = true;

	for (uint32_t i = 0; i < count; i++) {
		watcher = watchers[i];

		if (root_errors[i] == 0) {
			continue;
		}

		if (watcher->error == NULL) {
			watcher->error = async_new_exception(
					async_ce_async_exception, "Filesystem monitoring error: %s", strerror(root_errors[i]));
		}

		/* The root's descriptor is gone: nothing to remove from the kernel or the registry. */
		fs_watch_node_free(watcher->nodes[0]);
		watcher->nodes[0] = watcher->nodes[--watcher->node_count];

		fs_watcher_close(watcher, false);
	}

	registry->draining = false;

	if (open_error != 0 && registry->poll != NULL) {
		async_io_event_orphan(registry->poll);
		async_io_event_release(registry->poll);
		registry->poll = NULL;
	}

	efree(root_errors);
	efree(watchers);
	fs_registry_release_if_empty(registry);
}

void async_fs_watcher_request_shutdown(void)
{
	async_fs_registry_t *const registry = ASYNC_G(fs_watchers);

	if (registry == NULL) {
		return;
	}

	async_fs_watcher_t *watcher;

	ZEND_HASH_FOREACH_PTR(&registry->watchers, watcher)
	{
		watcher->base.flags |= ASYNC_EVENT_F_CLOSED;
		fs_watcher_debounce_disarm(watcher);

		while (watcher->node_count > 0) {
			fs_watch_node_free(watcher->nodes[--watcher->node_count]);
		}
	}
	ZEND_HASH_FOREACH_END();

	zend_hash_clean(&registry->watchers);
	registry->draining = false;
	ASYNC_G(reactor).parked_watchers = 0;
	fs_registry_release_if_empty(registry);
}

#else /* !__linux__ */

void async_fs_watcher_request_shutdown(void)
{
}

void async_fs_watcher_rebuild(void)
{
}

#endif /* __linux__ */

///////////////////////////////////////////////////////////////////
/// The WATCHER wait
///////////////////////////////////////////////////////////////////

static zend_string *fs_watcher_record_info(const async_coroutine_event_callback_t *record)
{
	const async_fs_watcher_t *const watcher = (const async_fs_watcher_t *) record->event;

	return zend_strpprintf(0, "foreach: FileSystemWatcher(%s)", ZSTR_VAL(watcher->path));
}

/* The parked count goes with the record; after the request's shutdown there is none to keep. */
static void fs_watcher_record_unlink(async_coroutine_event_callback_t *record)
{
	async_wait_record_remove(record);

	if (EXPECTED(ASYNC_G(fs_watchers) != NULL)) {
		ASYNC_G(reactor).parked_watchers--;
	}
}

static void fs_watcher_record_wake(async_awaitable_t *target,
								   async_event_callback_t *callback,
								   void *result,
								   zend_object *exception)
{
	(void) target;
	(void) result;
	(void) exception;

	async_scheduler_enqueue(&((async_coroutine_event_callback_t *) callback)->coroutine->coroutine, NULL, false);
}

/* No collector_target: the kernel ends the wait whoever holds what (S9-fs-watcher.md 4). */
static const async_wait_kind_t fs_watcher_wait_kind = {
	.info = fs_watcher_record_info,
	.unlink = fs_watcher_record_unlink,
};

/* Parks the current coroutine until an event is buffered, the watcher closes or the coroutine is cancelled;
 * false with an exception. */
static bool fs_watcher_wait(async_fs_watcher_t *watcher)
{
	async_coroutine_t *const waiter = (async_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(waiter == NULL)) {
		zend_throw_error(NULL, "There is no coroutine to suspend");
		return false;
	}

	async_wait_end(waiter);
	async_callbacks_reserve(&watcher->base.callbacks, 1);
	async_wait_link(&waiter->waker.records[0],
					waiter,
					(async_awaitable_t *) &watcher->base,
					&fs_watcher_wait_kind,
					fs_watcher_record_wake);
	ASYNC_G(reactor).parked_watchers++;

	ZEND_ASYNC_SUSPEND();

	return EG(exception) == NULL;
}

///////////////////////////////////////////////////////////////////
/// The iterator
///////////////////////////////////////////////////////////////////

/* foreach over a watcher (fs_watcher.c:596-721); `data` holds the watcher. */
typedef struct
{
	zend_object_iterator iterator;
	zval current; /* UNDEF before the first event and after the last */
	bool started;
} fs_watcher_iterator_t;

static void fs_watcher_iterator_dtor(zend_object_iterator *zend_iterator)
{
	fs_watcher_iterator_t *const iterator = (fs_watcher_iterator_t *) zend_iterator;

	zval_ptr_dtor(&iterator->current);
	zval_ptr_dtor(&zend_iterator->data);
}

static zend_result fs_watcher_iterator_valid(zend_object_iterator *zend_iterator)
{
	return Z_ISUNDEF(((fs_watcher_iterator_t *) zend_iterator)->current) ? FAILURE : SUCCESS;
}

static zval *fs_watcher_iterator_current(zend_object_iterator *zend_iterator)
{
	return &((fs_watcher_iterator_t *) zend_iterator)->current;
}

static void fs_watcher_iterator_key(zend_object_iterator *zend_iterator, zval *key)
{
	(void) zend_iterator;

	ZVAL_NULL(key);
}

static void fs_watcher_iterator_move_forward(zend_object_iterator *zend_iterator)
{
	fs_watcher_iterator_t *const iterator = (fs_watcher_iterator_t *) zend_iterator;
	async_fs_watcher_t *const watcher = fs_watcher_from_object(Z_OBJ(zend_iterator->data));

	zval event;

	zval_ptr_dtor(&iterator->current);
	ZVAL_UNDEF(&iterator->current);

	if (UNEXPECTED(async_throw_if_unavailable())) {
		return;
	}

#ifdef __linux__
	/* A forked child watches again on its own descriptor before it waits (events since the fork are lost). */
	async_reactor_check_fork();
#endif

	while (!fs_watcher_pop(watcher, &event)) {
		if (fs_watcher_is_closed(watcher)) {
			if (UNEXPECTED(watcher->error != NULL)) {
				zend_throw_exception_internal(watcher->error);
				watcher->error = NULL;
			}

			return;
		}

		if (UNEXPECTED(!fs_watcher_wait(watcher))) {
			return;
		}
	}

	/* Another coroutine's step on a shared iterator may have stored its event while this one waited. */
	zval_ptr_dtor(&iterator->current);
	ZVAL_COPY_VALUE(&iterator->current, &event);
}

/* Starts the walk once: a second foreach over the same iterator goes on where the first stopped. */
static void fs_watcher_iterator_rewind(zend_object_iterator *zend_iterator)
{
	fs_watcher_iterator_t *const iterator = (fs_watcher_iterator_t *) zend_iterator;

	if (!iterator->started) {
		iterator->started = true;
		fs_watcher_iterator_move_forward(zend_iterator);
	}
}

static const zend_object_iterator_funcs fs_watcher_iterator_funcs = {
	.dtor = fs_watcher_iterator_dtor,
	.valid = fs_watcher_iterator_valid,
	.get_current_data = fs_watcher_iterator_current,
	.get_current_key = fs_watcher_iterator_key,
	.move_forward = fs_watcher_iterator_move_forward,
	.rewind = fs_watcher_iterator_rewind,
};

static zend_object_iterator *fs_watcher_get_iterator(zend_class_entry *class_entry, zval *object, int by_ref)
{
	(void) class_entry;

	if (UNEXPECTED(by_ref)) {
		zend_throw_error(NULL, "Cannot iterate FileSystemWatcher by reference");
		return NULL;
	}

	fs_watcher_iterator_t *const iterator = emalloc(sizeof(fs_watcher_iterator_t));

	zend_iterator_init(&iterator->iterator);
	iterator->iterator.funcs = &fs_watcher_iterator_funcs;
	ZVAL_OBJ_COPY(&iterator->iterator.data, Z_OBJ_P(object));
	ZVAL_UNDEF(&iterator->current);
	iterator->started = false;

	return &iterator->iterator;
}

///////////////////////////////////////////////////////////////////
/// Objects
///////////////////////////////////////////////////////////////////

static zend_object *fs_watcher_object_create(zend_class_entry *class_entry)
{
	async_fs_watcher_t *const watcher = zend_object_alloc(sizeof(async_fs_watcher_t), class_entry);

	memset(watcher, 0, offsetof(async_fs_watcher_t, std));
	async_event_init_in_object(&watcher->base, ASYNC_EVENT_F_CLOSED, offsetof(async_fs_watcher_t, std));
	watcher->debounce_callback.callback = fs_watcher_debounce_fire;

	zend_object_std_init(&watcher->std, class_entry);
	object_properties_init(&watcher->std, class_entry);

	return &watcher->std;
}

/* The destructor closes it (fs_watcher.c:386-391); an iterator parked on it is woken only by the request's
 * end, which calls every destructor. */
static void fs_watcher_object_destroy(zend_object *object)
{
#ifdef __linux__
	async_fs_watcher_t *const watcher = fs_watcher_from_object(object);

	if (ASYNC_G(fs_watchers) != NULL) {
		fs_watcher_close(watcher, false);
	}
#endif

	zend_objects_destroy_object(object);
}

static void fs_watcher_object_free(zend_object *object)
{
	async_fs_watcher_t *const watcher = fs_watcher_from_object(object);

#ifdef __linux__
	/* After a fatal error the shutdown frees objects without their destructor, before RSHUTDOWN. */
	if (UNEXPECTED(ASYNC_G(fs_watchers) != NULL)) {
		watcher->base.flags &= ~FS_WATCHER_F_BURST;
		fs_watcher_close(watcher, false);
	}
#endif

	zend_object_std_dtor(object);

	if (watcher->base.flags & FS_WATCHER_F_CONSTRUCTED) {
		if (watcher->base.flags & FS_WATCHER_F_COALESCE) {
			zend_hash_destroy(&watcher->coalesced);
		} else {
			zval_circular_buffer_dtor(&watcher->raw);
		}
	}

	if (watcher->nodes != NULL) {
		efree(watcher->nodes);
	}

	if (watcher->extensions != NULL) {
		zend_hash_destroy(watcher->extensions);
		FREE_HASHTABLE(watcher->extensions);
	}

	if (watcher->error != NULL) {
		OBJ_RELEASE(watcher->error);
	}

	if (watcher->path != NULL) {
		zend_string_release(watcher->path);
	}

	async_callbacks_free((async_awaitable_t *) &watcher->base, &watcher->base.callbacks);
}

/* The allow-list in lower case without the dot; false with an exception. */
static bool fs_watcher_extensions_init(async_fs_watcher_t *watcher, HashTable *extensions)
{
	if (zend_hash_num_elements(extensions) == 0) {
		return true;
	}

	ALLOC_HASHTABLE(watcher->extensions);
	zend_hash_init(watcher->extensions, zend_hash_num_elements(extensions), NULL, NULL, false);

	zval *extension_zval;

	ZEND_HASH_FOREACH_VAL(extensions, extension_zval)
	{
		if (UNEXPECTED(Z_TYPE_P(extension_zval) != IS_STRING)) {
			zend_argument_type_error(6, "must contain only strings, %s given", zend_zval_value_name(extension_zval));
			return false;
		}

		const char *name = Z_STRVAL_P(extension_zval);
		size_t length = Z_STRLEN_P(extension_zval);

		if (length > 0 && name[0] == '.') {
			name++;
			length--;
		}

		if (UNEXPECTED(length == 0)) {
			zend_argument_value_error(6, "must not contain an empty extension");
			return false;
		}

		zend_string *const extension = zend_string_init(name, length, false);

		zend_str_tolower(ZSTR_VAL(extension), length);
		zend_hash_add_empty_element(watcher->extensions, extension);
		zend_string_release(extension);
	}
	ZEND_HASH_FOREACH_END();

	return true;
}

ZEND_METHOD(Async_FileSystemWatcher, __construct)
{
	char *path;
	size_t path_length;
	bool recursive = false;
	bool coalesce = true;
	zend_long debounce_ms = 0;
	zend_long max_hold_ms = 0;
	HashTable *extensions = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 6)
		Z_PARAM_PATH(path, path_length)
		Z_PARAM_OPTIONAL
		Z_PARAM_BOOL(recursive)
		Z_PARAM_BOOL(coalesce)
		Z_PARAM_LONG(debounce_ms)
		Z_PARAM_LONG(max_hold_ms)
		Z_PARAM_ARRAY_HT(extensions)
	ZEND_PARSE_PARAMETERS_END();

	async_fs_watcher_t *const watcher = THIS_WATCHER;

	/* TrueAsync's second construction leaks the first watch, which keeps the script running (probe f13). */
	if (UNEXPECTED(watcher->base.flags & FS_WATCHER_F_CONSTRUCTED)) {
		zend_throw_error(NULL, "Cannot call constructor twice");
		RETURN_THROWS();
	}

	const zend_long timings[] = { debounce_ms, max_hold_ms };

	for (uint32_t i = 0; i < sizeof(timings) / sizeof(timings[0]); i++) {
		if (UNEXPECTED(timings[i] < 0 || timings[i] > UINT32_MAX)) {
			zend_argument_value_error(i + 4, "must be between 0 and %u", UINT32_MAX);
			RETURN_THROWS();
		}
	}

	watcher->base.flags |= FS_WATCHER_F_CONSTRUCTED | (coalesce ? FS_WATCHER_F_COALESCE : 0) |
			(recursive ? FS_WATCHER_F_RECURSIVE : 0);
	watcher->path = zend_string_init(path, path_length, false);
	watcher->debounce_ms = (uint32_t) debounce_ms;
	watcher->max_hold_ms = (uint32_t) max_hold_ms;

	if (coalesce) {
		zend_hash_init(&watcher->coalesced, 8, NULL, ZVAL_PTR_DTOR, false);
	}

	if (extensions != NULL && UNEXPECTED(!fs_watcher_extensions_init(watcher, extensions))) {
		RETURN_THROWS();
	}

#ifdef __linux__
	if (UNEXPECTED(php_check_open_basedir(path))) {
		zend_throw_exception(
				async_ce_async_exception, "Failed to start filesystem handle: open_basedir restriction in effect", 0);
		RETURN_THROWS();
	}

	char *const absolute_path = expand_filepath(path, NULL);

	if (UNEXPECTED(absolute_path == NULL)) {
		zend_throw_exception_ex(async_ce_async_exception, 0, "Failed to start filesystem handle: %s", strerror(ENOENT));
		RETURN_THROWS();
	}

	zend_string *const root_path = zend_string_init(absolute_path, strlen(absolute_path), false);

	efree(absolute_path);
	/* A forked child's watch goes on its own descriptor, not on the one it shares with the parent. */
	async_reactor_check_fork();

	async_fs_registry_t *const registry = fs_registry_get();

	if (UNEXPECTED(registry == NULL)) {
		zend_string_release(root_path);
		RETURN_THROWS();
	}

	if (UNEXPECTED(fs_watch_node_add(registry, watcher, root_path, ZSTR_EMPTY_ALLOC()) == NULL)) {
		zend_throw_exception_ex(async_ce_async_exception, 0, "Failed to start filesystem handle: %s", strerror(errno));
		zend_string_release(root_path);
		fs_registry_release_if_empty(registry);
		RETURN_THROWS();
	}

	watcher->base.flags &= ~ASYNC_EVENT_F_CLOSED;
	zend_hash_index_add_new_ptr(&registry->watchers, watcher->std.handle, watcher);

	if (recursive) {
		fs_watch_tree_walk(registry, watcher, root_path, ZSTR_EMPTY_ALLOC());
	}

	zend_string_release(root_path);
#else
	zend_throw_exception(async_ce_async_exception, "FileSystemWatcher is not supported on this platform yet", 0);
	RETURN_THROWS();
#endif
}

ZEND_METHOD(Async_FileSystemWatcher, close)
{
	ZEND_PARSE_PARAMETERS_NONE();

#ifdef __linux__
	if (ASYNC_G(fs_watchers) != NULL) {
		fs_watcher_close(THIS_WATCHER, false);
	}
#endif
}

ZEND_METHOD(Async_FileSystemWatcher, isClosed)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(fs_watcher_is_closed(THIS_WATCHER));
}

ZEND_METHOD(Async_FileSystemWatcher, getIterator)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_create_internal_iterator_zval(return_value, ZEND_THIS);
}

void async_register_fs_watcher_ce(void)
{
	async_ce_filesystem_event = register_class_Async_FileSystemEvent();
	async_ce_fs_watcher = register_class_Async_FileSystemWatcher(async_ce_awaitable, zend_ce_aggregate);
	async_ce_fs_watcher->create_object = fs_watcher_object_create;
	async_ce_fs_watcher->get_iterator = fs_watcher_get_iterator;
	async_ce_fs_watcher->default_object_handlers = &fs_watcher_handlers;

	memcpy(&fs_watcher_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	fs_watcher_handlers.offset = offsetof(async_fs_watcher_t, std);
	fs_watcher_handlers.dtor_obj = fs_watcher_object_destroy;
	fs_watcher_handlers.free_obj = fs_watcher_object_free;
	fs_watcher_handlers.clone_obj = NULL;
}
