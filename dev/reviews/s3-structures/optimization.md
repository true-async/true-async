# S3 data structures: optimised layouts (task items 2 and 3)

Author: the data-structure expert. Date: 2026-10-01. Inputs: `BRIEF.md`, `critic0.md`,
`EDMOND-DECISIONS.md` (overrides the brief: polarity A is final, bit 31 = 0 coroutine, 1 event;
no core STARTED flag, the extension owns `ASYNC_COROUTINE_F_STARTED`; no EH fields in
`async_coroutine_t`), `consolidation.md` (its bit list 16-20 is kept below).

Source abbreviations: `F:` fork `Zend/zend_async_API.h` (`origin/true-async-stable`, identical to
`critic/fork/zend_async_API.h`, md5 checked); `Fc:` fork `Zend/zend_async_API.c` (same branch, copy
in `critic/opt/fork_zend_async_API.c`); `R:` RFC `Zend/zend_async_API.h` (`origin/async-core-io`
`834811f2d88`, identical to `critic/rfc-core`); `Rf:` RFC `zend_fibers.c`; `ts:` RFC
`ext/test_scheduler/test_scheduler.c`; bare file names: the reference `/home/user/php-async` at
`1fdacf8`.

## 0. Result in one table

| | Reference `1fdacf8` | Proposed | How obtained |
|---|---|---|---|
| Base event (`zend_async_event_t` → `async_event_t`) | 104 B | 32 B | compiled (section 1.2) |
| Waker (embedded) | 248 B | 120 B | compiled |
| `async_coroutine_t` (sizeof) | 544 B | 360 B | compiled |
| Coroutine allocation (`zend_object_alloc`, class without properties: sizeof − 16) | 528 B → bin 640 | 344 B → bin 384 | computed from `zend_objects_API.h:83-96`, bins `zend_alloc_sizes.h:49-52` |
| Same with `zend_fcall_t` inline (option O6) | — | 448 B → bin 448 | compiled + computed |
| Bytes zeroed by `zend_object_alloc` per spawn | 488 | 304 | computed (`zend_objects_API.h:95`: `obj_size - sizeof(zend_object)`) |
| Allocations per spawn | 2 (object, fcall) | 2, or 1 with O6 | code reading; reference count from S3.md:266 (performance review) |
| Allocations on a first `await` of a coroutine | 2 (waiter's events hash 320 B, target's callback array 32 B) | 0 | code reading + compiled sizes |
| Allocations on a later `await` | 0 | 0 | code reading |
| Allocations per `suspend` | 0 | 0 | code reading |
| `Future` (`zend_future_t`, S5) | 184 B | 96 B | compiled |

"Compiled" means: a probe program built with gcc 13.3 (x86-64) against the real headers of both
branches prints `sizeof`/`offsetof` (`critic/opt/m/t_fork.c`, `t_prop.c`, `proposed.h`). Known
answer first: the probe reproduces every gdb number of the brief (RFC `zend_coroutine_t` 144, fork
`zend_async_event_t` 104, fork waker 248, reference `async_coroutine_t` 544, `zend_object` 56), so
the tool agrees with the project's own measurement. No PHP was built or run; no timing was taken.
Every speed effect below is a hypothesis until B1-B5 measure it (Edmond's criterion: at most 3 %
more `instructions:u` per operation than the reference, allocations not above it).

## 1. The type bit and the base event `async_event_t`

### 1.1 The type bit (polarity A, final)

Bit 31 of the first `uint32_t` of every awaitable: 0 = coroutine (`zend_coroutine_t.flags`, offset
0 of `async_coroutine_t`), 1 = event. One opaque header type makes the generic layer typed:

```c
/* New names (no fork equivalent): need Edmond's naming. */
typedef struct _async_awaitable_s { uint32_t flags; } async_awaitable_t;   /* never instantiated */
#define ASYNC_AWAITABLE_F_EVENT (1u << 31)

#define ASYNC_AWAITABLE_IS_COROUTINE(p) \
	((((const async_awaitable_t *) (p))->flags & ASYNC_AWAITABLE_F_EVENT) == 0)
```

Codegen (measured, `gcc -O2 -S critic/opt/pol.c`): the test compiles to `testl %edx,%edx; js`,
a sign test, identical for both polarities; the polarity costs nothing on any hot path.

What polarity A obliges (Edmond, decision 4; critic0 #3):
- every event constructor sets the bit; one helper does it, and ported `memset` + `flags =` sites
  (`channel.c:841-843`, `task_group.c:397-399`) call the helper instead of assigning:
  ```c
  static zend_always_inline void async_event_init(async_event_t *ev,
  		const async_event_methods_t *methods, uint32_t flags)
  {
  	ev->flags = flags | ASYNC_AWAITABLE_F_EVENT;
  	ev->methods = methods;
  	/* ref_count/object_offset and callbacks: zero from the caller's allocation */
  }
  ```
- the event-reference prefix (`F:1075`, 0x80) becomes `ASYNC_AWAITABLE_F_EVENT | ASYNC_EVENT_F_REFERENCE`,
  so a reference that escapes resolution never reads as a coroutine;
- a debug `ZEND_ASSERT` at every generic entry: a decoded coroutine's object class is `Coroutine`
  (`ZEND_COROUTINE_OBJECT(c)->ce`), a decoded event has `methods != NULL` and, if it is an object,
  a class other than `Coroutine`.

Failure mode to keep in mind (why the asserts matter): an event that misses the bit is read as a
coroutine, and generic code writes the waiters vector at offset 152 of a 32-byte event: heap
corruption, not a clean crash.

### 1.2 Layout

```c
/* One per event TYPE, const, in .rodata. Fork: the same 8 pointers in every instance (F:917-936). */
typedef struct _async_event_methods_s {
	bool (*add_callback)(async_event_t *event, async_event_callback_t *callback);   /*  0 */
	bool (*del_callback)(async_event_t *event, async_event_callback_t *callback);   /*  8 */
	bool (*start)(async_event_t *event);                                             /* 16 */
	bool (*stop)(async_event_t *event);                                              /* 24 */
	bool (*replay)(async_event_t *event, async_event_callback_t *callback,
			zval *result, zend_object **exception);             /* 32, NULL: not replayable */
	bool (*dispose)(async_event_t *event);                                           /* 40 */
	zend_string *(*info)(async_event_t *event);                                      /* 48 */
	void (*notify_handler)(async_event_t *event, void *result, zend_object *exception); /* 56, NULL: plain notify */
} async_event_methods_t;                                                             /* 64 B, shared */

struct _async_event_s {
	uint32_t flags;                        /*  0  4  bit 31 = 1; bits 0-12 base, 13-30 subtype */
	union {
		uint32_t ref_count;                /*  4  4  a C event owns its count */
		uint32_t object_offset;            /*  4  4  ASYNC_EVENT_F_ZEND_OBJ: offset of the zend_object */
	};
	const async_event_methods_t *methods;  /*  8  8 */
	async_callbacks_vector_t callbacks;    /* 16 16  section 4 */
};                                         /* 32 B, half a cache line (fork: 104 B, two lines) */
```

All offsets and sizes compiled. Decisions:

| Fork field (F:902-937) | Bytes | Fate | Why |
|---|---|---|---|
| `flags` | 4 | kept, offset 0 | type bit and event flags |
| `extra_offset` | 4 | **cut** | it serves the fork's ABI split (core declares the type, the reactor appends private data, `libuv_reactor.c:991, 1255, 2807` …) and `F_OBJ_REF` (`F:1152-1154`). True Async owns both the types and the reactor, so private data is a containing struct; an object reference is a field of the subtype that needs it (the timeout already keeps `timeout->std`, `async.c:1671`) |
| `ref_count` / `zend_object_offset` union | 4 | kept as a union, the offset renamed `object_offset` | the same name and offset (4) as `zend_coroutine_t.object_offset` (R:112), so "the object of an awaitable" is one formula for an object-backed event and an embedded coroutine; a pure C event (timer, poll) needs its own count, an object-backed one uses the object's (`F:1157-1188`) |
| `loop_ref_count` | 4 | **cut from the base** | read only by the libuv reactor (`libuv_reactor.c:355-380`) and `thread.c:3457`; it belongs to the reactor's event subtypes (S4/S6) |
| `callbacks` (with `current_iterator`) | 24 | 16 | section 4: the iterator pointer becomes a tombstone mode bit |
| 8 method pointers | 64 | **8** (`methods`) | section 1.3 |

### 1.3 Per-instance methods versus one table per type

Every reference type fills the 8 pointers with the same static functions per type
(`future.c:660-666`, `2391-2400`, `libuv_reactor.c:2808-2813`, `coroutine.c:145-151`; 131
assignments, all of a function name). The only per-instance variation is decoration: a wrapper
stores the previous `dispose` and installs its own (`async.c:1673-1676` timeout over a timer,
`async.c:1452` future extra, `channel.c:135, 319`). Each decorator becomes its own const table
whose `dispose` calls the decorated type's table entry; when the decorated type varies at run time
the wrapper keeps `const async_event_methods_t *base` (8 B, the same as today's `prev_dispose`).

Cost: a method call is two dependent loads (`ev->methods`, then the slot) instead of one. The table
is shared by every instance of a type and stays cached in a loop over many events (assumption, not
checked). Gain: 56 B per event, the whole event in one cache line, and per-instance init writes 1
pointer instead of 8. **The coroutine never goes through a table**: generic code tests bit 31 and
calls the coroutine functions directly (section 2.3), so S3's hot paths have no indirect event call
at all (the reference makes about 6 per `await`, S3.md:268). Needs a measurement for S4 (timers)
before it is final: B-timer instructions per arm/fire.

### 1.4 Event flags: what survives (bits 0-30)

Fork base bits are kept at their fork positions, so ported code changes only the prefix.

| Bit | Fork flag (F:1043-1073) | Fate | Reason |
|---|---|---|---|
| 0 | `CLOSED` | kept | 66 uses in the reference |
| 1 | `RESULT_USED` | kept (events only) | read as `ZEND_FUTURE_IS_USED` (F:1941) |
| 2 | `EXC_CAUGHT` | kept | futures, task groups |
| 3 | `ZVAL_RESULT` | kept | per event (some events pass a non-zval result) |
| 4 | `ZEND_OBJ` | kept | selects the union member |
| 5 | `NO_FREE_MEMORY` | **free** | set once (`coroutine.c:133`), never read in the reference or the fork core; with per-type `dispose` an embedded type simply does not free |
| 6 | `EXCEPTION_HANDLED` | kept | futures, groups, scope, threads, Rf-equivalent of `F` `zend_fibers.c:1176` |
| 7 | `REFERENCE` | kept, only in the reference prefix | resolved at the object boundary (section 2.4); generic code never sees a reference |
| 8 | `OBJ_REF` | **free** | the one setter (`async.c:1666`) has no reader on the generic path; the subtype keeps its object pointer |
| 9 | `CLOSE_FD` | **moves to the poll subtype range** | read only by poll (`libuv_reactor.c:562`) |
| 10 | `HIDDEN` | kept | reactor-level deadlock accounting (`libuv_reactor.c:1100`) on several types |
| 11 | `BAILOUT` | kept | scope (S9) |
| 12 | — | spare base bit | |
| 13-30 | subtype bits (fork convention: `F:1131-1135`, `1264-1282`, `1935-1937`, `1563-1569`) | kept per subtype | a subtype may also take 5, 8, 9, 12 |
| 31 | — | type bit = 1 | |

### 1.5 How a Future embeds it (S5)

```c
typedef struct {
	async_event_t event;                          /*  0 32  first: bit 31 at offset 0 */
	zval result;                                  /* 32 16 */
	zend_object *exception;                       /* 48  8 */
	uint32_t lineno;                              /* 56  4 */
	uint32_t completed_lineno;                    /* 60  4 */
	zend_string *filename;                        /* 64  8 */
	zend_string *completed_filename;              /* 72  8 */
	async_callbacks_vector_t resolve_callbacks;   /* 80 16 */
} async_future_t;                                 /* 96 B; fork zend_future_t 184 B (F:1919-1933) */
```

`resolve` (a per-instance pointer, F:1930) moves into the future's table, which extends the base
one: `typedef struct { async_event_methods_t base; zend_future_resolve_t resolve; } async_future_methods_t;`
and `future->event.methods` points at `&table.base`. The PHP `Future` object stays a reference to
the C future (`future.c:841, 1157, 1943` use `ZEND_ASYNC_EVENT_REF_SET`); nothing else changes.

## 2. `async_coroutine_t`

### 2.1 Layout (compiled)

```c
struct _async_coroutine_s {
	zend_coroutine_t coroutine;               /*   0 144  RFC (R:104-132); flags at 0, bit 31 = 0 */
	async_fiber_context_t *fiber_context;     /* 144   8  hot on every switch */
	async_callbacks_vector_t callbacks;       /* 152  16  the waiters vector (fork name `callbacks`) */
	async_waker_t waker;                      /* 168 120  section 3 */
	zend_object *deferred_cancellation;       /* 288   8 */
	HashTable *finally_handlers;              /* 296   8  lazy, as the reference */
	zend_object std;                          /* 304  56  last: properties_table is a flexible tail */
};                                            /* 360 B (reference 544) */
```

No EH fields (Edmond, decision 1: the core saves the EH window per switch). No `waker` pointer (the
RFC coroutine has none; the waker is embedded). No status field in the waker (section 3).

Cache lines (computed from the offsets; the effect is not measured): line 2 (128-191) holds
`fiber_context`, the waiters vector, `waker.error` and `waker.result`, so a switch plus a wake touches
line 0 (`flags`) and line 2; the inline callbacks sit in lines 3-4 and are touched only by `await`;
`std`'s refcount is at 304 (line 4). The RFC prefix is fixed, so this is the best order for the
extension's part.

### 2.2 Flags: bits 16-30 of `zend_coroutine_t.flags`

Bits 0-15 are the core's (status 0-3, `F_CANCELLED` 4, `F_MAIN` 5, `F_FIBER` 6, `F_OBJ_REF` 7,
8-15 spare; no core STARTED, decision 3). Extension bits, positions 16-20 as the consolidation
report:

| Bit | Name | Source |
|---|---|---|
| 16 | `ASYNC_COROUTINE_F_PROTECTED` | fork `F_PROTECTED` (F:1803) |
| 17 | `ASYNC_COROUTINE_F_EXCEPTION_HANDLED` | fork event bit 6 on the coroutine |
| 18 | `ASYNC_COROUTINE_F_EXC_CAUGHT` | fork event bit 2 on the coroutine |
| 19 | `ASYNC_COROUTINE_F_BAILOUT` | fork event bit 11 on the coroutine |
| 20 | reserved `ASYNC_COROUTINE_F_ZOMBIE` (S9) | fork F:1802 |
| 21 | `ASYNC_COROUTINE_F_STARTED` | decision 3: set right before the body's first instruction by whoever runs it |
| 22 | `ASYNC_WAKER_F_EVENTS_STOPPED` | the fork waker's `events_stopped : 1` (F:1711) moved out of the waker |
| 23-30 | spare (8 bits) | |
| 31 | 0 (the type bit) | |

No bit for `RESULT_USED` (no reader for a coroutine) or `ZVAL_RESULT` (always true for a coroutine;
the type bit answers it), as the consolidation report. `CLOSED` is status FINISHED. Any of 21-22 can
move; only the names come from the fork.

### 2.3 Typed APIs and the dispatch helpers

Critic0 #2: event helpers take `async_event_t *`, coroutine waiter helpers take
`async_coroutine_t *`, generic wait code takes `async_awaitable_t *`; all three as `static inline`
functions, not macros, so passing a coroutine to an event helper is an
incompatible-pointer-type diagnostic. That is only a **warning** in gcc 13 (checked:
`critic/opt/typed.c` compiles with a warning, exit 0) and in MSVC (C4133), so the build must add
`-Werror=incompatible-pointer-types` (config.m4) and `/we4133` (config.w32).

```c
static zend_always_inline async_coroutine_t *async_awaitable_coroutine(async_awaitable_t *a)
{
	ZEND_ASSERT(ASYNC_AWAITABLE_IS_COROUTINE(a));
	return (async_coroutine_t *) a;        /* flags is offset 0 of both structs */
}

static zend_always_inline async_event_t *async_awaitable_event(async_awaitable_t *a)
{
	ZEND_ASSERT(!ASYNC_AWAITABLE_IS_COROUTINE(a) && ((async_event_t *) a)->methods != NULL);
	return (async_event_t *) a;
}

/* The waiters vector of any awaitable. Same instruction count either way: one load, one
 * sign test, one lea (compiled, critic/opt/pol.c). */
static zend_always_inline async_callbacks_vector_t *async_awaitable_callbacks(async_awaitable_t *a)
{
	return ASYNC_AWAITABLE_IS_COROUTINE(a)
		? &((async_coroutine_t *) a)->callbacks
		: &((async_event_t *) a)->callbacks;
}

/* Methods: a coroutine has no table; generic code branches to direct calls. */
static zend_always_inline bool async_awaitable_add_callback(async_awaitable_t *a, async_event_callback_t *cb)
{
	if (ASYNC_AWAITABLE_IS_COROUTINE(a)) {
		return async_callbacks_push(&async_awaitable_coroutine(a)->callbacks, cb);  /* direct */
	}
	async_event_t *ev = async_awaitable_event(a);
	return ev->methods->add_callback(ev, cb);
}
/* The same shape for del_callback (remove), start/stop (no-op for a coroutine), replay
 * (async_coroutine_replay), info (async_coroutine_info), dispose (ZEND_COROUTINE_RELEASE). */
```

If one table pointer is preferred for uniformity, `async_awaitable_methods(a)` can return
`&async_coroutine_methods` (a const table of the coroutine functions) for bit 31 = 0; it costs an
indirect call per operation on the S3 paths, so the direct branch is the recommendation.

### 2.4 From a PHP object to an awaitable

The one place that sees an event reference:

```c
static zend_always_inline async_awaitable_t *async_awaitable_from_object(zend_object *obj)
{
	void *base = (char *) obj - obj->handlers->offset;
	if (*(const uint32_t *) base == ASYNC_EVENT_REFERENCE_PREFIX) {   /* F_EVENT | F_REFERENCE */
		return (async_awaitable_t *) ((async_event_ref_t *) base)->event;
	}
	return (async_awaitable_t *) base;   /* a Coroutine object lands on async_coroutine_t */
}
```

## 3. The waker

### 3.1 Field by field (fork 248 B, F:1706-1731, offsets compiled)

| Fork field | Off / size | S3 needs it? | Fate | Hot path helped |
|---|---|---|---|---|
| `status` | 0 / 4 (+4 pad with the bitfield) | its states map onto the RFC status: `WAITING` = SUSPENDED, `QUEUED` = QUEUED, `IGNORED` = QUEUED + `F_CANCELLED` + no `F_STARTED`, `RESULT`/`NO_STATUS` = RUNNING (consolidation.md:113). The only split without an equivalent is `RESULT` vs `NO_STATUS`; no reader needs it once `await` reads the result before cleaning (`async.c:373-378`) and `waker_new` always cleans (`Fc:868-871`) | **cut** (fold into the RFC status). The "woken before parking" fast path (`scheduler.c:1683-1691`, `coroutine.c:845-852`) becomes: suspend sets SUSPENDED before starting the events, a synchronous wake in scheduler context sets RUNNING back, suspend sees RUNNING and returns. If a reader needing `RESULT` turns up, a bit in 23-30 costs 0 B | suspend, enqueue: one status word instead of two kept in step |
| `events_stopped : 1` | 4 / 1 bit | yes | **moved** to bit 22 | — |
| `events` (HashTable) | 8 / 56 | the edges, yes; a hash for them, no: S3 waits on at most one coroutine, S5's common case is one or two events | **cut**; edges are the waker's callbacks (section 3.2) | await: no hash insert, lookup, delete (S3.md:268 counts 2 lookups + 1 delete per await); first await: no 320 B allocation |
| `triggered_events` | 64 / 8 | no: `zend_async_waker_add_triggered_event` (Fc:1082) has no caller in the fork tree or the reference | **cut**, with its branches in clean/destroy (Fc:929-931, 975-979) | suspend (one test less) |
| `result` | 72 / 16 | yes: a transient result (channel value, IO) is not stored on the event; for a coroutine target the waker's copy also survives the target's release at wake | **kept**; `await` **moves** it out (`ZVAL_COPY_VALUE` + `ZVAL_UNDEF`) instead of `ZVAL_COPY` (`async.c:376`) followed by `zval_ptr_dtor` in clean (`Fc:939`) | await: one addref/delref pair less per refcounted result |
| `error` | 88 / 8 | yes (delivery channel of R:248-254) | **kept** | — |
| `filename`, `lineno` | 96 / 16 | only for `getSuspendFileAndLine`, `getSuspendLocation`, `info` and the deadlock report | **lazy**: computed when asked from the parked frame, `fiber_context->execute_data`, which suspend already saves (`scheduler.c:290-296`) and `getTrace` already walks (`coroutine.c:1488-1515`) | suspend: no `zend_get_executed_filename_ex`, `zend_string_release`, `addref` per suspend (`zend_common.h:194-215`; about 5 % of a suspend by S3.md:267, not re-measured) |
| `dtor` | 112 / 8 | no: never assigned a non-NULL value in the fork tree or the reference (`git grep`: only resets, Fc:892, 917, 957; `coroutine.c:101`) | **cut** | clean: one test less |
| `inline_triggers[2]` | 120 / 48 | no: a trigger groups callbacks per event only because the hash is keyed by event | **cut** (section 3.2) | await |
| `inline_callbacks[2]` | 168 / 80 | yes: allocation-free registration (`Fc:1134-1144`) | **kept**; they are the edges | await: 0 allocations |

### 3.2 New waker (compiled, embedded at 168)

```c
#define ASYNC_WAKER_INLINE_SLOTS 2
typedef struct {
	zend_object *error;                                        /*   0   8 */
	zval result;                                               /*   8  16 */
	async_coroutine_event_callback_t inline_callbacks[ASYNC_WAKER_INLINE_SLOTS]; /* 24 80; free: base.callback == NULL */
	async_callbacks_vector_t callbacks;                        /* 104  16  edges beyond two, lazy (S5) */
} async_waker_t;                                               /* 120 B (fork 248) */
```

**Wait-graph edges are kept** (decision N7): every edge is one `async_coroutine_event_callback_t`
with `coroutine` = the waiter and `event` = the awaited awaitable (section 4.1). The S7 collector
walks a waiter's edges as the used inline slots plus the overflow vector, each `->event`; the
deadlock report (`scheduler.c:719-737`) and the waker's GC walk do the same. Nothing is lost against
the fork's trigger table: a trigger for event E was the set of this waker's callbacks whose
`->event == E`.

Rules the flat list needs:
- `start`/`stop` once per distinct event: a callback skips the call when an earlier edge of the same
  waker has the same `event` (a scan of two in the common case). For a coroutine target both are
  no-ops and are skipped by the type bit.
- clean = for each used slot: remove the callback from its awaitable (direct for a coroutine),
  release the awaitable, clear the slot; then the overflow vector. No hash cleaning.
- **Needs a measurement before it is final (rule 7, S3.md:276):** S5's `await_all` over N futures
  dedupes with `zend_async_waker_is_event_exists` (`async_API.c:540`), a hash lookup today and a
  linear scan here, O(N²) at N = 10 000. Proposal: a lazily created index, `HashTable *index` (8 B,
  waker 128 B, coroutine still in the 384 bin), only when the edge count passes a threshold;
  the threshold comes from a benchmark with N ∈ {1, 2, 8, 100, 10 000}. S3 does not need it.

### 3.3 Lazy suspend location: risks

- A running coroutine asking its own suspend location: the reference returns the last one (kept
  until the next clean, `coroutine.c:1556-1559`); the lazy version has no parked frame and returns
  `[null, 0]`/`"unknown"`. Only `coroutine/008` covers these methods and it checks types only.
- A suspend during compilation (`zend_is_compiling()` branch, `zend_common.h:202-204`) records the
  compiled file and line; the frame walk gives the executing frame's. Diagnostics only. Equivalence
  in the other cases is an assumption, not checked (consolidation.md:116 says the same).
- The frame must be the parked one: `fiber_context->execute_data` is written at suspend
  (`scheduler.c:295`) and read by `getTrace` for the same purpose.

## 4. Callbacks, the callbacks vector, handles

### 4.1 Callback structs (compiled)

```c
struct _async_event_callback_s {          /* fork zend_async_event_callback_t, F:830-834 */
	uint32_t ref_count;                   /*  0 4 */
	uint32_t flags;                       /*  4 4  was padding; free for a "coroutine edge" mark (S7) */
	async_event_callback_fn callback;     /*  8 8  (async_awaitable_t *, callback, void *result, zend_object *exception) */
	async_event_callback_dispose_fn dispose; /* 16 8 */
};                                        /* 24 B, as the fork */

typedef struct {                          /* fork zend_coroutine_event_callback_t, F:862-868 */
	async_event_callback_t base;          /*  0 24 */
	async_coroutine_t *coroutine;         /* 24  8  edge source (typed, critic0 #2) */
	async_awaitable_t *event;             /* 32  8  edge target: coroutine or event (fork: zend_async_event_t *) */
} async_coroutine_event_callback_t;       /* 40 B, as the fork */

typedef struct {                          /* an RFC finish handler (R:83-90) as a waiter callback */
	async_event_callback_t base;          /*  0 24 */
	zend_coroutine_finish_handler_fn handler; /* 24 8 */
	zend_coroutine_t *waiter;             /* 32  8 */
	void *data;                           /* 40  8 */
} async_finish_handler_callback_t;        /* 48 B, bin 48; one allocation per add (only the core's GC adds one, Zend/zend_gc.c:2134) */
```

The callback receives `async_awaitable_t *` instead of the fork's `zend_async_event_t *`, since the
source may be a coroutine. The `flags` word costs nothing (padding); a bit there lets S7 walk edges
backwards from an awaitable's waiters without guessing the callback type from `dispose`.

### 4.2 The vector

```c
typedef struct {                          /* fork zend_async_callbacks_vector_t, F:883-890: 24 B */
	union {
		async_event_callback_t **data;    /* capacity > 0: heap array */
		async_event_callback_t *single;   /* capacity == 0: at most one element, inline */
	};                                    /* 0 8 */
	uint32_t length;                      /* 8 4 */
	uint32_t capacity;                    /* 12 4  bit 31: a notify is iterating */
} async_callbacks_vector_t;               /* 16 B */
```

Two changes against the fork, both measured only by simulation:
1. **One element inline.** The first push stores into `single`, no allocation; the second moves it
   into a 4-slot array (32 B). A chain of awaits (B4) has one waiter per coroutine, so every link's
   first await allocates nothing on the target; the reference allocates the 4-slot array once per
   target (F:1219-1223). B5 (1000 waiters) grows as before.
2. **No `current_iterator`.** The fork keeps a pointer to the notify loop's index and shifts it on
   removal (Fc:1654-1660, 1665-1682). A removal during a notify now writes a NULL tombstone; the
   loop skips NULLs and compacts once at the end; outside a notify removal stays swap-with-last
   (O(1) after the scan, as the fork). Nested notify is refused as in the fork (Fc:1709-1714), now by
   the bit.

**Defect found in the fork's scheme** (Fc:1654-1660): when a handler removes an entry *before* the
current one, the last entry is swapped into the visited part and the index is decremented, so a
visited entry runs again and the moved one never runs. Replayed in `critic/opt/iter_sim.c` (the
algorithm transcribed, not the fork binary): callbacks A B C D, B's handler removes A, the fork calls
A B B C (D lost, B twice), the proposed scheme A B C D. A randomised check of both
(`critic/opt/vec_fuzz.c`, 100 000 runs, ASan+UBSan, a quarter of handler calls remove a random
callback or push one): proposed 0 failing runs; fork 66 153 failing runs (89 830 double calls, 76 440
missed calls). The workload is far harsher than real use: the common removal is a callback removing
itself, which the fork handles correctly. It is a latent bug in the reference to carry to
php-async, not a reason by itself for the change.

### 4.3 Handles: the callback pointer, not an index

Swap removal moves entries, so a positional handle names the wrong entry after any removal (the ts
scenario in S3.md:120-122: ts:361-381 shifts entries; ts:210-246 does the same for switch handlers).
Inside the extension every registration is identified by its callback pointer. At the RFC boundary
the slots return `uint32_t` (R:371-379, 387-390), which cannot carry a pointer on 64-bit. Options:

| Option | Bytes | Cost |
|---|---|---|
| A. RFC change: the add slots return `void *` (NULL = nothing to remove), remove takes it; the same for switch handlers and awaiting info | 0 | an RFC/core change; the one core caller (`zend_gc.c:2134-2140`) keeps its shape |
| B. No core change: a per-coroutine 32-bit counter, the id stored in the finish callback; remove scans for it | 0 (`async_finish_handler_callback_t` has room: 48 → 56 B, bin 56) | a scan per remove, O(n) like the pointer scan |

Recommendation: A, with B as the interim if the core change waits.

## 5. Totals

| Path | Reference | Proposed | Note |
|---|---|---|---|
| Bytes per coroutine (sizeof / allocated / bin) | 544 / 528 / 640 | 360 / 344 / 384 | −184 B struct, −256 B of heap per live coroutine (bins); 40 B headroom in the bin for fields the consolidation adds |
| … with `zend_fcall_t` inline (O6) | — | 464 / 448 / 448 | one allocation fewer per spawn; `Rf:808-809` reads `coroutine->fcall`, so it points at the inline storage; dispose frees only a non-inline one (fibers get a heap fcall from the core, Rf:1029-1036) |
| … with a lazy internal context (core change, not proposed for S3) | — | 312 / 296 / 320 | the RFC embeds a 56 B HashTable (R:130) where the fork had a pointer; an RFC choice (it saves an allocation, RFC `zend_async_API.c:181-183`) |
| Spawn: allocations | 2 + params | 2 + params (1 with O6) | registry, scope: other experts |
| Spawn: bytes zeroed + waker init | 488 B memset, then `zend_async_waker_init` (hash init, two loops, Fc:886-902) | 304 B memset, no waker init (`IS_UNDEF` is 0, empty slots are zero) | |
| First `await` on a coroutine: allocations | 2 (events hash 320 B: HT_SIZE_EX(8, …) printed by the probe, assuming the rotated-pointer key takes the mixed-hash path; the target's 4-slot array 32 B) | 0 | B4 makes every await a first await |
| Later `await`: allocations | 0 | 0 | |
| `await`: hash operations | 2 lookups + 1 delete (S3.md:268) + insert | 0 | |
| `await`: indirect calls on the event path | about 6 (S3.md:268) | 0 for a coroutine target | callbacks themselves stay indirect |
| `await`: result copies | copy into the waker, copy out, dtor | copy into the waker, move out | |
| `suspend`: allocations | 0 | 0 | |
| `suspend`: work removed | — | location capture, `triggered_events`/`dtor`/`filename` tests in clean, hash clean | waker clean about 10 % and location about 5 % of a suspend in the reference (S3.md:267, performance review, older build) |

## 6. Every cut: hot path, risk, evidence

| Cut | Saves | Hot path | Risk | Evidence / measurement before final |
|---|---|---|---|---|
| Coroutine without the event (agreed) | 104 B | all | none beyond the dispatch | agreed |
| Methods table per type | 56 B per event | none in S3 (coroutine bypass); event paths S4+ pay one dependent load | decorators need their own tables | measure timer arm/fire in S4 |
| `extra_offset`, `loop_ref_count` out of the base | 8 B per event | — | reactor subtypes carry `loop_ref_count` | S4 |
| Vector without `current_iterator`, tombstones | 8 B per vector | notify, remove during notify | compaction pass after every notify (only when a tombstone was written, one flag) | simulation above; B5 |
| Vector with one inline element | 1 allocation per awaited coroutine | await (B4) | an extra branch to find the base pointer in push and notify | B4, B5 |
| Waker `events` hash → flat edges | 56 B + 320 B heap + hash ops | await, suspend (clean) | O(N²) dedupe at large N (S5) | rule 7: B2, B4 now; index threshold in S5 |
| `inline_triggers` | 48 B | await | none (the callback carries the event) | — |
| `triggered_events`, `dtor` | 16 B | suspend (clean) | none: no writer | git grep |
| Waker `status` → RFC status | 8 B | suspend, enqueue | the RESULT/NO_STATUS split, if some reader needs it: one spare bit | consolidation.md:113 mapping; S3 tests |
| `filename`/`lineno` lazy | 16 B + per-suspend work | suspend | self-query while running; compile-time suspend | B2 |
| Result moved out of the waker | 1 addref + 1 delref per await | await | none | — |
| `zend_fcall_t` inline (O6) | 1 allocation per spawn | spawn (B1) | bin 384 → 448 | B1, both bins |
| Finish handlers as waiter callbacks | separate vector | none (GC only) | 1 allocation per GC run | — |

## 7. Things to carry beyond this report

- **Reference bug, GC of the waker** (`coroutine.c:310-318`): the loop reads the `waker.events`
  values as events, but they are triggers (`Fc:1203-1204` stores `trigger`); the trigger's `length`
  is read as event flags. With one callback (`length` 1 = `CLOSED`) nothing is reported, so awaited
  objects are invisible to the GC through the waker; a `length` in 16-31 would be read as `ZEND_OBJ`
  and push a garbage pointer. The new layout walks typed callbacks and does not port it; php-async
  should be fixed.
- **Fork callback-vector iterator defect** (section 4.2): fix in php-async if it stays.
- **Names**: the brief's task names the base `async_event_t`; the consolidation report keeps the
  fork's `zend_async_event_t`. Layouts do not depend on the choice. New names with no fork source:
  `async_awaitable_t`, `ASYNC_AWAITABLE_F_EVENT`, `async_event_methods_t`, `async_future_methods_t`,
  `async_finish_handler_callback_t`, `async_event_init`, `async_awaitable_*` helpers, the vector's
  notify bit. They need Edmond's naming.

## 8. Decisions taken here (not agreed with Edmond)

- Fork event bit positions kept (0-12), so ported code only changes prefixes.
- Coroutine bit 21 for `STARTED`, 22 for the waker's `events_stopped`; positions are free to move.
- The waker's `status` folded into the RFC status (the consolidation left it to this report).
- The waiters vector named `callbacks` (fork and consolidation name), not `waiters`.
- Coroutine functions called directly by type dispatch instead of a coroutine methods table.
- `HashTable *index` in the waker deferred to S5, sized by a benchmark.
- RFC handle change (option A) recommended over the 32-bit id.
- Not proposed for S3: inline `zend_fcall_t` and a lazy internal context (both change bins or the
  core; left as options with numbers).
