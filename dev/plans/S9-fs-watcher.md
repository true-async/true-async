# S9 notes, layer 7: FileSystemWatcher

Design note of the seventh S9 layer, 2026-10-13. The layer adds `Async\FileSystemWatcher`, an object that watches a
file or a directory from its construction until `close()` or its destruction, buffers what changed, and hands the
changes out as `Async\FileSystemEvent` objects to `foreach`. Names, signature, messages and behaviour come from
TrueAsync (`ext/async` at `tests/lists/REFERENCE`, `1fdacf8`, file names alone below; its reactor
`libuv_reactor.c`, libuv 1.48 `src/unix/linux.c` below `uv:`) and its documentation (`true-async.github.io`
`eadb6cc`, `en/docs/components/filesystem-watcher.md`, below `site:`). `true-async-doc` has no page for it. Behaviour
marked "probed" was run on 2026-10-13 on a debug ZTS build of the fork `true-async/php-src` `863f6dd90cf` with that
`ext/async` (scripts `f1_*.php`-`f16_*.php` in `/mnt/project-files/s9/probes/s9.fswatch/`); the same build passes the
13 tests of `tests/fs_watcher`. This extension has no watcher yet, so nothing here was run on ours.

**Why this layer is the seventh.** `dev/PLAN.md` orders the S9 layers scope, context, channels, task groups, pools,
iterators, FileSystemWatcher, runtime information; the first six are built. The watcher has its own iterator
(`fs_watcher.c:596-721`), not the iterator core of S9.6.

**What is new here.** TrueAsync gets the changes from libuv's `uv_fs_event_t`. This extension has no libuv
(`dev/PRINCIPLES.md` P1.2) and no filesystem op in bukka's API: `php_io_op` has no op for it
(`main/php_io_hooks.h:26-40` at `CORE_REF`), and ior has none (`src/ior.h:116-151`, `true-async/ior` `2bfd231`). On
Linux the layer reads inotify itself and waits for its descriptor with the POLL op the reactor already submits for
its wake pair (`src/reactor.c:1208`); inotify descriptors are pollable (inotify(7)). Windows has no such descriptor:
`ReadDirectoryChangesW()` completes an overlapped request, so it needs a new op in bukka's API and in ior
(section 3, "Windows").

## 1. What the layer implements

| Kind | In this layer | Elsewhere |
|---|---|---|
| Classes | `FileSystemEvent` (final readonly, `path`, `filename`, `renamed`, `changed`) and `FileSystemWatcher implements Awaitable, IteratorAggregate` in `fs_watcher.stub.php`, as `fs_watcher.stub.php` | |
| Constructor | `__construct(string $path, bool $recursive = false, bool $coalesce = true, int $debounceMs = 0, int $maxHoldMs = 0, array $extensions = [])` | |
| Methods | `close()`, `isClosed()`, `getIterator()` | |
| Source | a per-request inotify registry: one descriptor, its watch descriptors, one POLL op (section 3) | Windows: the directory-change op, a request to bukka and ior (S9.45) |
| Waits | the WATCHER wait kind: a coroutine in `foreach` until an event is buffered, the watcher closes or the coroutine is cancelled | |
| Collector | none: the kind names no target, so a parked iterator is never a candidate (section 4) | |

What a user gets, from `site:filesystem-watcher.md`:

```php
spawn(function () {
    $watcher = new FileSystemWatcher('/etc/myapp', recursive: true);  // watching starts here

    foreach ($watcher as $event) {                                     // suspends while nothing changed
        if (str_ends_with($event->filename ?? '', '.yml')) {
            reloadConfig();
        }
    }                                                                  // ends after close()
});
```

## 2. The behaviour, as TrueAsync's

From `fs_watcher.c`, the reactor and the probes; every row holds for this layer unless section 6 says otherwise.

| Case | What happens |
|---|---|
| Start | watching starts in the constructor; events are buffered before any `foreach` (`008`) |
| Path | a file or a directory; one that cannot be watched throws `AsyncException` "Failed to start filesystem handle: no such file or directory" (`005`, probed `f8`) |
| `FileSystemEvent` | `path` is the value passed to the constructor; `filename` the changed entry, relative to `path` for a recursive watch (`sub/deep/a.txt`), the basename of `path` when the event is about the watched entry itself (`uv:2444`, probed `f6`, `f7`) |
| Flags | `changed` for a content or attribute change (`IN_MODIFY`, `IN_ATTRIB`), `renamed` for every other kind: create, delete, move, the watch's removal (`uv:2431-2434`) |
| Coalesce (default) | one event per `path/filename` key, in the order the keys first appeared; a later change ORs its flags into the buffered event (`003`, probed `f11`: `a` written twice and `b` renamed give `a r=1 c=1`, `b r=1 c=1`, `c r=1 c=0`) |
| Raw | every kernel event separately, in order (`004`; `f11`: 8 events for the same writes) |
| `foreach` | takes buffered events; with none, suspends until the next event, `close()`, or a cancel of the coroutine; ends after `close()` once the buffer is empty (`001`, `002`, probed `f4`) |
| By reference | `Error` "Cannot iterate FileSystemWatcher by reference" (probed `f8`) |
| Two iterators | each event goes to one of them, the first to run after the wake; the other stays parked (probed `f2`) |
| Cancel of the iterating coroutine | the cancellation leaves `foreach`; the watcher stays open and a later `foreach` gets later events (probed `f3`, `f12`) |
| `close()` | idempotent; wakes every iterator; events already buffered still come out (`002`, probed `f4`) |
| Destruction | the object's destructor closes it (`fs_watcher.c:386-391`) |
| `await()` | `TypeError`: the watcher is `Awaitable`, not `Completable` (probed `f5`), as `Channel` here (DECISIONS 2026-10-09 S9.26) |
| Recursive | macOS and Windows native; elsewhere one watch per directory, new subdirectories added and removed ones dropped as they come and go, symlinked directories not followed, at most 8192 directories, the rest silently unwatched (`011`, `012`, `libuv_reactor.c:2910-3300`) |
| Debounce | `$debounceMs > 0`: a matching change (re)arms a timer; when it fires, one event with `filename = null` and the burst's ORed flags is buffered; the iterator sleeps through the burst (`013`); `$maxHoldMs` caps the wait from the burst's first change |
| Extensions | a case-insensitive allow-list, `.php` equal to `php`; applied in debounce mode only (`014`; probed `f15`: without `$debounceMs` both `a.txt` and `b.php` come out) |
| Clone, serialize | `Error` "Trying to clone an uncloneable object", `Exception` "Serialization of ... is not allowed" (probed `f8`) |
| `new FileSystemEvent()` | allowed, all four properties uninitialised (probed `f8`) |

Behaviour that looks like a defect, each one probed and each a departure in section 6:

- **An open watcher keeps the request alive.** Its libuv handle stays referenced from the constructor to `close()`
  (`fs_watcher.c:340`), so with no coroutine iterating: a channel `recv()` that can never complete hangs instead of
  failing with `DeadlockError` (`f1`); the script does not end after its last line (`f10`); `exit()` while a coroutine
  iterates hangs after the graceful cancellation (`f9`). All three are killed by the probe's 10 s timeout.
- **A second `__construct()`** starts a second watch and leaks the first, which keeps the script from ending after
  `close()` (`f13`).
- **The watched directory removed**: two `renamed` events with its name, then `foreach` waits for good, since the
  kernel dropped the watch (`f7`).
- **Arguments read silently**: a negative `$debounceMs` or `$maxHoldMs` is 0; a non-string extension, an empty one or
  one of 16 or more characters is skipped (`fs_watcher.c:500-511`), so `extensions: ['configuration_backup']` alone
  filters nothing; a path with a NUL byte is cut at it (`Z_PARAM_STR`, `fs_watcher.c:474`).
- **No `open_basedir` check**: a watcher on a directory outside it lists the names created there.
- **The raw buffer is on the persistent allocator** (`fs_watcher.c:545`): `memory_limit` does not bound a raw watcher
  nobody reads.
- **A monitoring error** closes the watcher with no exception (`fs_watcher.c:282-285`): the loop ends as after
  `close()`.

## 3. The design

**The registry.** One inotify descriptor per request, opened with `IN_NONBLOCK | IN_CLOEXEC` at the first watcher and
closed at the last, as libuv keeps one per loop (`uv:2298-2304`): `fs.inotify.max_user_instances` defaults to 128 per
user, so a descriptor per watcher would fail at the 129th. The registry maps a watch descriptor to the watchers on
it: `inotify_add_watch()` returns the same descriptor for the same inode, so two watchers on one directory share it,
and `inotify_rm_watch()` runs when the last of them leaves. Each user of a descriptor is a (watcher, prefix, root)
entry, since the name depends on the pair: a recursive watcher on `/a` and a plain one on `/a/sub` share the
descriptor of `/a/sub`, and a change of `x` there is `sub/x` for the first and `x` for the second; only the second's
root closes on `IN_IGNORED` (section 6, item 3). libuv keeps one path per descriptor (`uv:2444`). It lives in the reactor's request state beside the
signal registry (`src/os_signal.c`), and the reactor's shutdown closes it.

**The op.** While the registry has a watcher, one POLL op for `POLLIN` on the descriptor is submitted, always on the
reactor's `own` list. Its completion reads the descriptor until `EAGAIN` into a 4 KiB buffer (an event is at most
`sizeof(struct inotify_event) + NAME_MAX + 1`, `uv:2412`), hands each event to the users of its descriptor, and
submits the op again; a resubmit that completes inline loops in the completion, as `wakeup_complete()` does
(`src/reactor.c:1176-1201`), instead of nesting a drain in a drain. A parked iterator waits for something outside the
process, so the scheduler must wait instead of reporting a deadlock while one is parked, and not otherwise (section 6,
item 1): each parked WATCHER record raises a counter of parked iterators in the registry and its unlink lowers it,
and `async_reactor_has_waits()` (`src/reactor.h:199-202`) reads it beside `started_triggers`. It is a counter of its
own because the fork's `triggers_end_parent_waits()` zeroes `started_triggers` and cancels only trigger records
(`src/reactor.c:492-507`), while parked iterators stay parked in the child (below). The op never moves between the
lists.

**A watcher.** A `zend_object` with an `async_event_t` whose callbacks are the parked iterators' records, the buffer
(a hash keyed by `path/filename` in coalesce mode, the extension's circular buffer of zvals in raw mode, both on the
request allocator), the debounce state and the watch descriptors it holds. An event from the registry goes through
the extension filter, then into the buffer or the debounce, and wakes the records; the event object is built at
that point, as `fs_watcher.c:58-72`.

**The iterator** is `Channel`'s (`src/channel.c:1292-1370`) with a WATCHER record: `move_forward` pops an event; with
none, a closed watcher ends the walk, an open one parks the coroutine on a record linked to the watcher's event and,
when woken, tries again, with `async_throw_if_unavailable()` first as there. Every wake reaches every record, so the first coroutine to run takes the events, as `f2`
shows for TrueAsync.

**Recursive on Linux** follows `libuv_reactor.c:2910-3300`: one watch descriptor per directory on the same inotify
descriptor (not a POLL op each, as the libuv code needs a handle each), `filename` prefixed with the directory's path
relative to `$path`, a directory created or moved in watched with its subtree, one deleted or moved out dropped with
its subtree, at most 8192 directories, symlinks not followed. A subdirectory is added with `IN_ONLYDIR |
IN_DONT_FOLLOW`, so a directory replaced by a symlink between the `lstat()` and the add is refused by the kernel
instead of followed, which TrueAsync's `lstat()` then `uv_fs_event_start()` allows. A subdirectory's own events
without a name (`IN_DELETE_SELF`, `IN_MOVE_SELF`, `IN_IGNORED`) are dropped, since its parent already reported the
entry (section 6, item 11).

**Debounce** as `fs_watcher.c:118-268`: the quiet timer is a soft timer on the reactor's `own` list, rearmed by each
matching change and capped by `$maxHoldMs`; the flush buffers the collapsed event and wakes the records. No trigger
event is needed, since the records wait on the watcher's own event.

**`close()` and the destructor**: the watcher leaves the registry (its watch descriptors removed when no other watcher
shares them), the timer stops, the records wake. The buffer stays until the object is freed.

**Teardown.** After a fatal error no destructor runs; `free_obj` comes either in `zend_call_destructors()`, before
RSHUTDOWN, for a global holding the only reference (it closes the watcher then, S9.44, `fs_watcher/049`), or in
`zend_deactivate()`, after the request's shutdown. The reactor's shutdown therefore withdraws the registry's op and timer before it asserts that `own` is
empty (`src/reactor.c:682`), closes the descriptor, zeroes the counter and marks the registry gone; a watcher freed
later, or a record unlinked later, only drops its own state and leaves the counter alone, as triggers that outlive the request do (`src/reactor.c:663-672`). The drain runs no user code, but an
allocation in it can bail out on `memory_limit`; the op is marked not submitted before the drain starts, so the
shutdown finds it in a known state.

**Fork.** The child inherits the inotify descriptor, which shares its event queue with the parent's. The registry's
rebuild runs in `reactor_rebuild()` before the loop that resubmits `own` (`src/reactor.c:580`), as
`wakeup_rebuild()` does for the wake pair (`src/reactor.c:536-551`): it closes the inherited descriptor, opens a new
one, adds every watch again by its path, and re-arms the same event with `php_io_op_poll()` on the new descriptor,
leaving it on `own` for that loop to submit. Iterators parked before the fork stay parked in the child, and the
counter keeps them; the rebuilt registry wakes them with the child's events. A watch that cannot be added again
closes its watcher, which wakes its iterators.

**Windows** (S9.45). `ReadDirectoryChangesW()` on a directory opened with `FILE_FLAG_BACKUP_SEMANTICS |
FILE_FLAG_OVERLAPPED` completes on the IOCP that ior's Windows backend already runs (`src/ior_iocp.c`). The request: an
op `PHP_IO_OP_DIR_CHANGES` in bukka's API (a handle, a buffer, the subtree flag; Done with the bytes written; the Poll
queue answers Unsupported) and an `IOR_OP_DIR_CHANGES` in ior. Both go as PRs to bukka, told to Edmond first
(`dev/WORKFLOW.md` "Branches"), and as a one-topic commit on our core branch with an `RFC-CHANGES.md` entry. Native
recursion and single-file watches (libuv watches the parent directory and filters by name) are checked against the
reference on Edmond's Windows machine in that step. Until then the constructor throws `AsyncException`
"FileSystemWatcher is not supported on this platform yet" on Windows, and the layer's tests that construct a watcher
are expected to fail on `pocs-win` until S9.45 (`xfail-on:pocs-win(S9.45)`).

**macOS and the BSDs** are not in the first version: no CI line builds the extension there, so no code of this layer
would run before a user ran it. The constructor throws the same exception there; the Fog of `dev/PLAN.md` records the
path, a kqueue descriptor on the POLL op with `EVFILT_VNODE` and a directory listing diffed per change to name the
entry, as `libuv_reactor.c:3030-3128` does for the BSDs.

**Arguments**: `$path` by `Z_PARAM_PATH`, checked by `php_check_open_basedir()` and expanded against the virtual
current directory (`expand_filepath()`), as a plain file stream does; `$event->path` keeps the value passed.
`$debounceMs` and `$maxHoldMs` outside 0-4294967295 are a `ValueError`; an extension that is not a string is a
`TypeError`, an empty one a `ValueError`, and an extension of any length is kept (lowercased `zend_string`s in a
hash). A second `__construct()` is an `Error` "Cannot call constructor twice", as `Channel` (`src/channel.c:1484`).

**As built (S9.44).** The reactor now takes every completed event off its list before a custom completion runs, so
the registry's POLL completion and the wakeup's resubmit the same way (`async_reactor_resubmit_own()`). A forked child
rebuilds before the constructor adds a watch and before an iterator waits (`async_reactor_check_fork()`); changes
made between the fork and that rebuild are lost, and until it the child removes no watch, since it shares the parent's
inotify instance (`fs_watcher/044`, `048`). An overflow is tested through the test hook
`TrueAsync\Test\fail_io('inotify_overflow')` (`040`): a real one needs 16384 unread events.

## 4. Garbage collection and the collector

A `FileSystemEvent` holds three strings and two booleans, so the buffer holds no object that can reach back to the
watcher, and the watcher reports nothing to PHP's GC (TrueAsync reports the coalesce buffer, `fs_watcher.c:442-458`,
which can form no cycle either). The registry holds watchers by pointer, not by reference: a watcher nobody holds is
destroyed, which closes it. The iterator holds the watcher, so a coroutine parked in `foreach` keeps it open. The
WATCHER kind has no `collector_target`, as the IO kind (`src/io_provider.c:170`): the kernel can wake a parked
iterator whoever holds what, so the collector never takes it as a candidate.

## 5. Steps

- S9.44 The layer's block of `tests/lists/S9.txt` (section 8); the registry, its op and the parked-iterator counter, the watcher,
  the iterator and the WATCHER kind, recursive and debounce, the fork rebuild, on Linux; the platform refusal
  elsewhere; own tests for each row of sections 2 and 6 the reference tests leave out.
- S9.45 Windows: the directory-change op in bukka's API and ior (section 3), told to Edmond first, built in the core
  slot; the watcher on it; the layer's tests run on `pocs-win`.
- S9.46 Layer review: the Critic over S9.44 and S9.45, coverage of the layer's lines, Mull on its diff, 100 fuzz seeds
  over the layer's tests, the measurement of section 8; a `true-async-doc` page for the watcher with section 6's
  differences and the parameters the site leaves out (`$debounceMs`, `$maxHoldMs`, `$extensions`).
- S9.47 Security pass by `dev/SECURITY.md`: `open_basedir` and paths (NUL, relative, symlinks in the recursive walk),
  a raw buffer nobody reads under `memory_limit`, the 8192-directory cap, `close()` or the destructor called from
  inside the drain, two watchers sharing a watch descriptor when one closes, the fork rebuild.

## 6. Departures from TrueAsync

Decided here by the project's rules and recorded in `DECISIONS.md` with S9.43, except item 5, which is question 1;
S9.44 builds them.

1. **An open watcher keeps nothing waiting unless a coroutine iterates it.** The op is on the reactor's `own` list
   and only a parked iterator counts as a wait, so with no iterator: a deadlock is reported (`f1`), the script ends after its last line and the watcher is
   destroyed with it (`f10`), `exit()` ends the request (`f9`). A parked iterator still keeps the scheduler waiting.
   TrueAsync hangs in all three.
2. **A second `__construct()` is an `Error`**; TrueAsync leaks the first watch, which keeps the script from ending
   (`f13`).
3. **The watched entry removed closes the watcher**: when the kernel drops the root's watch (`IN_IGNORED` on its
   descriptor), the events already read are buffered, `IN_IGNORED` itself is not delivered, the watcher closes and
   `foreach` ends after them. TrueAsync's `foreach` waits for good (`f7`, which shows `IN_DELETE_SELF` and
   `IN_IGNORED` as two `renamed` events). An editor that saves by writing a temporary file and renaming it over the
   target unlinks a watched file, so a watcher on the file closes at the first such save; the doc page says to watch
   the directory for that. A moved root (`IN_MOVE_SELF`) keeps its watch, as in TrueAsync, and `$event->path` keeps
   the old path; a fork rebuild adds it by that path again, which fails and closes the watcher, or finds another
   entry there.
4. **Arguments are checked**: `Z_PARAM_PATH`, `open_basedir`, `ValueError` for negative or too large timings,
   `TypeError` and `ValueError` for bad extensions, no length limit on an extension. TrueAsync reads them silently
   (section 2).
5. **`$extensions` in every mode**, Edmond's answer B to question 1. TrueAsync documents it for debounce mode
   only (the stub's comment), so `extensions: ['php']` without a debounce delivers every file (`f15`).
6. **The buffers are on the request allocator**, so `memory_limit` bounds a raw watcher nobody reads.
7. **A monitoring error is thrown** from `foreach` as `AsyncException` "Filesystem monitoring error: ...", once, and
   the watcher closes. TrueAsync closes silently (`fs_watcher.c:282-285`).
8. **An inotify queue overflow** (`IN_Q_OVERFLOW`, events lost) gives every watcher of the descriptor one event with
   `filename = null` and both flags set, the shape of a debounce event, meaning "something changed, rescan". It
   passes the extension filter (the lost events may have matched); in debounce mode it arms the timer as a matching
   change, so it merges into the collapsed event; in coalesce mode it shares the key of `path` alone with that
   event.
   libuv drops it (`uv:2436-2438`: the overflow's descriptor is -1, no watcher has it).
9. **Subdirectories are added with `IN_DONT_FOLLOW`**, closing the window between `lstat()` and the add.
10. **macOS and the BSDs are refused** in the first version (section 3); TrueAsync runs there on libuv.
11. **No name-less events of subdirectories** in a recursive watch. TrueAsync prefixes them with the node's own path
   (`libuv_reactor.c:3165-3168`), so `rmdir sub/deep` gives `sub/deep` from the parent and then `sub/deep/deep` twice
   (`IN_DELETE_SELF`, `IN_IGNORED`); test `012` passes either way, it matches with `str_contains()`.

Items 2, 4, 6 and 7 follow Edmond's rule of 2026-10-12 (pools): a programmer's error is caught even where TrueAsync
does not, when the check is small. Items 1, 3, 8 and 11 follow his rule of 2026-10-13 (iterate): TrueAsync behaviour
that looks like a bug is not copied.

TrueAsync's documentation, to correct in the doc page of S9.46: `site:filesystem-watcher.md` shows the constructor
with three parameters and says "If the path doesn't exist, an `Error` is thrown" (it is `AsyncException`, `f8`); its
"Cancellation via Scope" example cancels nothing, it calls `close()`.

## 7. Questions for Edmond

Asked one at a time, each with the PHP code and what the programmer sees. The plan itself needs his agreement before
S9.44 (`dev/PLAN.md` S9).

1. **`$extensions` without `$debounceMs`.** `new FileSystemWatcher($dir, extensions: ['php'])`, then `a.txt` and
   `b.php` written: (A) as TrueAsync, both come out, the filter works only with a debounce (its stub says so); (B) only
   `b.php` comes out, in every mode. Under B a directory created in a recursive watch has no extension and is not
   delivered either. Recommended: B, since a programmer who passes a filter expects it to filter.
   **Answered B** (Edmond, 2026-10-13: "согласен делай" to the recommendation); test `fs_watcher/038`.

## 8. Tests and measurements

**List** `tests/lists/S9.txt`, a block for layer 7, frozen in S9.44: the 13 reference tests of `tests/fs_watcher`
(`001`-`005`, `007`-`014`; the reference has no `006`), each passing on the reference build above (2026-10-13); they
leave `tests/lists/S10.excluded`. On `pocs-win` they carry `xfail-on:pocs-win(S9.45)`. Most of them wait for
the kernel with `delay()` (`003`, `008`, `011`-`014`) and compare counts, not elapsed time; S9.44 lists any change
before it freezes the block.

**Own tests** (S9.44): each probe of section 2 and each departure of section 6 as one test; a shared watch descriptor
with one watcher closed; the fork rebuild (`skip-on:pocs-win`).

**Measurement**: B24, 10 000 file writes into a watched directory, coalesce and raw, instructions and allocations of
the extension per delivered event against the reference, as B21-B23 (`dev/BENCHMARKS.md`).
