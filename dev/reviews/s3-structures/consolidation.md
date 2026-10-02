# S3 consolidation: the reference coroutine onto the RFC core (task item 1)

Scope: item 1 of the brief only (what maps where). Sizes and layout optimisation are the other
expert's; where a verdict touches size it says so and stops. Critic round 0 (`critic0.md`) is
answered in section 8. The EH_THROW window is taken as decided (core, per switch, S3.2).

Source keys (all line numbers checked on 2026-10-01):

| Key | File |
|---|---|
| R | RFC core `Zend/zend_async_API.h` (`834811f2d88`; identical to `S/rfc-core/...`) |
| Ra | RFC core `Zend/zend_async_API.c` |
| Rf | RFC core `Zend/zend_fibers.c` |
| ts | `S/rfc-core/ext/test_scheduler/test_scheduler.c` |
| F | fork `Zend/zend_async_API.h` (`origin/true-async-stable`; identical to `S/fork/...`) |
| Fc | fork `Zend/zend_async_API.c` (`git show origin/true-async-stable:Zend/zend_async_API.c`, copy in `S/consolidation-src/fork_zend_async_API.c`) |
| Ff | fork `Zend/zend_fibers.c` |
| bare name | reference extension `/home/user/php-async` at `1fdacf8` |
| RFC.md | `/home/user/php-async-core-rfc/scheduler_rfc.md` |

Verdicts: **(a)** RFC field/flag as is; **(b)** RFC field/flag with a core change in S3.2 (stated);
**(c)** extension field/bit (reference name, `ASYNC_` prefix where the core owns the `ZEND_` name,
bits 16-30 only); **(d)** dropped, with the reason.

## 1. The flags word after consolidation

`zend_coroutine_t.flags` (R:107), the first word of `async_coroutine_t`:

| Bits | Owner | Content |
|---|---|---|
| 0-3 | core, written by the scheduler | status (R:96-102, 135-141) |
| 4 | core | `ZEND_COROUTINE_F_CANCELLED`: cancellation requested (R:144) |
| 5 | core | `ZEND_COROUTINE_F_MAIN` (R:145) |
| 6 | core | `ZEND_COROUTINE_F_FIBER` (R:146) |
| 7 | core | `ZEND_COROUTINE_F_OBJ_REF` (R:150) |
| 8 | core (S3.2) | `ZEND_COROUTINE_F_STARTED`: the body has executed its first instruction |
| 9-15 | core | spare |
| 16 | extension | `ASYNC_COROUTINE_F_PROTECTED` (fork `ZEND_COROUTINE_F_PROTECTED`, F:1803) |
| 17 | extension | `ASYNC_COROUTINE_F_EXCEPTION_HANDLED` (fork event bit `ZEND_ASYNC_EVENT_F_EXCEPTION_HANDLED`, F:1051) |
| 18 | extension | `ASYNC_COROUTINE_F_EXC_CAUGHT` (fork event bit `ZEND_ASYNC_EVENT_F_EXC_CAUGHT`, F:1045) |
| 19 | extension | `ASYNC_COROUTINE_F_BAILOUT` (fork event bit `ZEND_ASYNC_EVENT_F_BAILOUT`, F:1073) |
| 20 | extension, S9 | reserved for `ASYNC_COROUTINE_F_ZOMBIE` (fork F:1802); not defined in S3 |
| 21-30 | extension | spare |
| 31 | extension | the awaitable type bit (polarity open, see below) |

Numbering of 16-20 is a proposal; only the names are fixed by the brief's rule. `DECISIONS.md`
last entry (16 `STARTED`, 17 `YIELD`, 18 `PROTECTED`, 19 `CANCEL_REQUESTED`) is superseded by the
brief: no extension STARTED, no YIELD, no CANCEL_REQUESTED.

**Type bit, written polarity-neutral.** Every reader goes through one macro on the first word of
an awaitable header, `ASYNC_AWAITABLE_IS_COROUTINE(p)`; nothing else tests bit 31.

| | Polarity A: 0 = coroutine | Polarity B: 1 = coroutine |
|---|---|---|
| Who sets the bit | every non-coroutine awaitable at init: the event init helper, every `flags =` site (`channel.c:843`, `task_group.c:399`, `libuv_reactor.c:3478`), and the event-reference prefix (`F:1075` `ZEND_ASYNC_EVENT_REFERENCE_PREFIX` 0x80 becomes `0x80 | bit31`, and `F:1142-1143` compares against that) | the coroutine allocator only (`coroutine_object_create`, the extension's `new_coroutine`, `gc_new_coroutine`, `launch`, `intercept_fiber` all reach it); the core mints no coroutine itself (Ra:722-743 takes main from the slot) |
| Zeroed memory | reads as a coroutine (Critic 3) | reads as an event |
| S3 exposure | none: S3 awaits only `Coroutine` (S3.md section 1) | none |
| From S4 | every new event kind must remember the bit | nothing to remember |

Every table below names "the type bit" and holds for both columns.

## 2. Fork `zend_coroutine_t` fields (F:1747-1790)

| Field | Reference use | RFC core | Verdict |
|---|---|---|---|
| `event` (`zend_async_event_t`, 104 B) | coroutine is an event: flags, refcount by object offset, callbacks, vtable (`coroutine.c:132-153`) | nothing | **(d)** agreed (brief). Its parts are mapped in section 3 |
| `fcall` | userland entry (`coroutine.c:528-536`) | `fcall` R:114 | **(a)** |
| `internal_entry` | C entry (`coroutine.c:538`; await iterator `async_API.c:1099`) | `internal_entry` R:116 | **(a)** |
| `extended_data` | fiber (`F_FIBER`, `coroutine.c:172`), await iterator (`async_API.c:759, 1098`), thread pool (`thread_pool.c:1062`) | `extended_data` R:118; the core uses it for fibers (Rf:1009) | **(a)** |
| `waker` (pointer) | always `&async_coroutine->waker` (`coroutine.c:137`), NULL only after `coroutine_object_destroy` (`:210`); NULL tests at `coroutine.c:466, 804, 905, 1591` | nothing | **(d)** the pointer: the waker is embedded (section 4); the NULL tests become `IS_FINISHED` tests (they guard a destroyed coroutine) |
| `scope` | spawn, finalize, scope catch (`coroutine.c:178-181, 714-730`) | nothing | **(d) in S3** (scope is S9; brief). Comes back in S9 as an extension field `scope` |
| `result` | `coroutine.c:530, 1053, 1456` | `result` R:120 | **(a)** |
| `exception` | finalize (`coroutine.c:646-662`), self-cancel stores it (`:909-919`), `getException` (`:1473`) | `exception` R:122; note R:84 "clearing ->exception marks it handled" for finish handlers | **(a)**. The extension marks handled with bit 17 and keeps the exception visible (`coroutine/039`); a core finish handler that clears it is honoured too (section 6, finish handlers) |
| `context` (`zend_async_context_t *`) | `getContext` (`coroutine.c:1361-1378`) | `context` (`zend_object *`, lazy) R:131 | **(a)**, used from S9 (brief: context out of S3) |
| `internal_context` (`HashTable *`, lazy) | disposed at finalize and destroy (`coroutine.c:212-214, 676-678`) | embedded `HashTable internal_context` R:130, init/destroy by the provider (R:483-486) | **(a)**. Embedded 56 B instead of a lazy 8 B pointer: a size question for the other expert, not a mapping one |
| `filename`, `lineno` | spawn location (`async_API.c:118`, `scheduler.c:1471`; `coroutine.c:1518-1546`) | `filename`, `lineno` R:124-125 | **(a)** |
| `extended_dispose` | thread pool, await iterator; called at finalize (`coroutine.c:687-691`) | `extended_dispose` R:127; the core installs it for fibers (Rf:1010) | **(a)**. The extension must call it on every death path, including a coroutine destroyed without finalize, or `zend_fiber_coroutine_dispose` (Rf:741-751) is skipped (the reference unlinked the fiber itself, `coroutine.c:172-176`) |
| `switch_handlers` (vector pointer, lazy) | enter/leave/finish calls (`scheduler.c:1708-1731`, `coroutine.c:610-624`) | slots `add/remove_switch_handler` R:371-374, storage with the scheduler | **(c)** `switch_handlers` in `async_coroutine_t`, lazy. The fork's `is_finishing` call (`coroutine.c:610-611`, F:269-273) is **(d)**: the RFC handler has no such argument (R:70-81); finishing is the finish handlers' job |

## 3. The embedded event's fields (F:902-937), now that there is no event in the coroutine

| Field | Reference use on the coroutine | RFC core | Verdict |
|---|---|---|---|
| `flags` | coroutine state via `ZEND_COROUTINE_*` (F:1808-1849) and event bits | `flags` R:107 | **(a)** the word; each bit in sections 5-6 |
| `extra_offset` | not used by the coroutine (only with `EVENT_F_OBJ_REF`, F:1152) | `F_OBJ_REF` + `object_offset` (R:147-150, 166-172) cover the separate-object case | **(d)** |
| `zend_object_offset` (union with `ref_count`) | `offsetof(async_coroutine_t, std)` (`coroutine.c:134`) | `object_offset` R:112 | **(a)** |
| `ref_count` | unused: the coroutine is always an object (`EVENT_F_ZEND_OBJ`, `coroutine.c:132`) | `ZEND_COROUTINE_ADD_REF/RELEASE` through the object (R:174-187) | **(d)**; refcounting is **(a)** via the object |
| `loop_ref_count` | none (reactor events only) | nothing | **(d)** |
| `callbacks` (`zend_async_callbacks_vector_t`, F:883-890) | awaiters and finish listeners: `add_callback` (`coroutine.c:1021-1024`, `task_group.c:841`, `zend_async_resume_when` Fc:1171), notify at finalize (`coroutine.c:668`), freed at `:248, 674` | finish-handler storage "lives with the scheduler" (R:367-378) | **(c)** `callbacks` in `async_coroutine_t` (the reference's name; the brief calls it "the waiters vector"). RFC finish handlers are stored in it as one more callback kind, with stable handles (S3.md section 4) |
| `add_callback`, `del_callback` | thin wrappers over `zend_async_callbacks_push/remove` (`coroutine.c:1021-1029`) | nothing | **(d)** the slots; generic code dispatches on the type bit and calls the vector functions directly |
| `start`, `stop` | no-ops (`coroutine.c:1010-1019`); called by generic waker code (`scheduler.c:1146`, Fc:829, 1026) | nothing | **(d)**; the generic call sites skip coroutines by the type bit |
| `replay` | `coroutine_replay` (`coroutine.c:1040-1070`): result or exception of a finished coroutine; used by `await` on a closed awaitable (`async.c:328-339`) and `start_waker_events` (`scheduler.c:1135-1144`) | nothing | **(d)** the slot; **(c)** the function, `async_coroutine_replay(async_coroutine_t *, ...)`, reached by type dispatch |
| `dispose` | `OBJ_RELEASE(&std)` (`coroutine.c:1101-1106`); called at `coroutine.c:430, 492, 503`, `async_API.c:140, 149, 166` | `ZEND_COROUTINE_RELEASE` R:181-187 | **(d)** the slot; the calls become `ZEND_COROUTINE_RELEASE` **(a)** |
| `info` | `coroutine_info` (`coroutine.c:1072-1099`), read by the deadlock report (`scheduler.c:708-716`) | nothing | **(d)** the slot; **(c)** the function, called directly |
| `notify_handler` | never set for a coroutine (`coroutine_object_create` leaves it NULL) | nothing | **(d)** |

## 4. Reference `async_coroutine_t` (`coroutine.h:47-66`)

| Field | Reference use | RFC core | Verdict |
|---|---|---|---|
| `coroutine` (`zend_coroutine_t`, first) | the core's view; casts `(async_coroutine_t *) zend_coroutine` everywhere | R:104-132 | **(a)** stays first: bit 31 and the status live at offset 0 |
| `waker` (embedded, 248 B) | the whole waiting model (`coroutine.c:68-124, 136-140`) | nothing | **(c)** `waker` (Edmond: the waiting model stays). Sub-fields below |
| `fiber_context` | stack and frame of a parked coroutine (`coroutine.c:353-363, 1494-1514`, `async_API.c:1285-1290`) | slot `coroutine_execute_data` R:358-363 reads it | **(c)** `fiber_context` |
| `deferred_cancellation` | the cancel that arrived inside `protect()` (`coroutine.c:936-951`), delivered at protect's end (`async.c:289-293`), read by `isCancellationRequested` (`coroutine.c:1638`) | nothing | **(c)** `deferred_cancellation` |
| `finally_handlers` (`HashTable *`, lazy) | `finally()` (`coroutine.c:1381-1424`), run at finalize (`:680-683`, `1329-1354`) | nothing (finish handlers are C callbacks, R:83-90) | **(c)** `finally_handlers`. Not folded into finish handlers: they are PHP callables that need a coroutine of their own and collect a `CompositeException` |
| `std` | the object | `object_offset` = `offsetof(async_coroutine_t, std)` | **(a)** |

**Waker sub-fields** (F:1706-1731). The fields stay with the waiting model; their size is the
other expert's. Consolidation verdicts:

| Sub-field | Verdict |
|---|---|
| `status` | **(c)** kept, but it must agree with the RFC status (the single source of truth, R:92-102). Mapping: `NO_STATUS` = RUNNING, no wait open; `WAITING` = SUSPENDED; `QUEUED` = QUEUED and not (`F_CANCELLED` and not `F_STARTED`); `IGNORED` = QUEUED and `F_CANCELLED` and not `F_STARTED` (set only at `coroutine.c:970`, `scheduler.c:918`); `RESULT` = RUNNING after a wake (the short path, `coroutine.c:845-852`, `scheduler.c:1726-1728`). Every reader that asks "in the queue" (`ZEND_ASYNC_WAKER_IN_QUEUE`, F:1684-1687: `coroutine.c:168, 601, 958`, `scheduler.c:962`) reads the RFC status instead. Only the `RESULT`/`NO_STATUS` split has no RFC equivalent; whether that alone justifies the field is for the size expert |
| `events`, `events_stopped`, `inline_triggers`, `inline_callbacks`, `dtor` | **(c)** the waiting model, unchanged in meaning |
| `result`, `error` | **(c)**. `error` is the RFC "error thrown at the suspension point" (R:248-254): the RFC delivery channel is stored here |
| `filename`, `lineno` | **(c)** suspend location (set `scheduler.c:1713-1716`, read `coroutine.c:1548-1576, 1085`). Alternative (S3.md item 11.3): derive it from the parked frame and drop both; equivalence with `zend_apply_current_filename_and_line` is an assumption, not checked |
| `triggered_events` | **(d)**: unused in S3 (brief); written only by reactor paths (Fc:1085-1098) |

## 5. Fork `ZEND_COROUTINE_F_*` (F:1799-1849)

| Fork flag | Reference meaning, set / read | RFC core | Verdict |
|---|---|---|---|
| `F_STARTED` (13) | body has started: set `coroutine.c:524` (after the `IGNORED` exit, so a cancelled-before-run coroutine is never started) and for main `scheduler.c:1246`; read `coroutine.c:956, 1582, 1605`, `scheduler.c:916, 1468` | `ZEND_COROUTINE_IS_STARTED` = `status != CREATED` (R:189-190): true right after enqueue | **(b)**: S3.2 adds `ZEND_COROUTINE_F_STARTED` (bit 8), `IS_STARTED` tests it; the old predicate is kept as `ZEND_COROUTINE_IS_SCHEDULED` (`status != CREATED`) and the core's callers that mean "handed to the scheduler" move to it (Rf:770; ts:1069, 1083, 1166, 1451). Writers: the scheduler, immediately before the body's first instruction, on every path that runs a body (switch in, in-place run `scheduler.c:633-641`, pooled context); `zend_async_scheduler_launch` sets it on main next to Ra:737-738. Never set on the cancelled-before-run path (RFC.md:169-171). `scheduler.c:1468` ("first enqueue") reads `IS_SCHEDULED` |
| `F_CANCELLED` (14) | set at request (`coroutine.c:907, 954`), at protect exit (`async.c:290`), at shutdown for unstarted (`scheduler.c:919`); read `coroutine.c:468, 953, 1626, 1636`, Ff:1719 | `F_CANCELLED` bit 4 "cancellation was requested" (R:144) | **(b)**: same meaning as the reference (set at the request, not at delivery; not set while protected, `coroutine.c:936-951` returns first). S3.2 change: Rf:1375 drops the `ZEND_COROUTINE_IS_CANCELLED(current)` term and keeps `extended_data == NULL`, which already detects every core force-close (Rf:767, 749) (Critic 8). Supersedes S3.md section 2 ("set when delivered") |
| `F_ZOMBIE` (15) | `is_safely` cancel (`coroutine.c:984-992` → `scope.c:905-918`), not counted active (`coroutine.c:764`, `scope.c:51, 64`) | nothing | **(d) in S3**: `is_safely` is delivered as a plain cancellation (S3.md section 5); **(c)** `ASYNC_COROUTINE_F_ZOMBIE` in S9 (bit 20 reserved) |
| `F_PROTECTED` (16) | `protect()` (`async.c:251, 276`), cancel defers (`coroutine.c:937`), cleared by deadlock and shutdown (`scheduler.c:867-868, 924-925`) | nothing | **(c)** `ASYNC_COROUTINE_F_PROTECTED` (bit 16), plus a `was_protected` local in `Async\protect` for nesting (section 8, finding 6); no field |
| `F_MAIN` (17) | `scheduler.c:1247` set; read `coroutine.c:738`, `scheduler.c:160`, `async.c:1763`, F:1854 | `F_MAIN` bit 5, set by the core (Ra:737) | **(a)** |
| `F_FIBER` (18) | set by the fork core (Ff:1307); read `coroutine.c:172, 739`, `scheduler.c:785, 802` | `F_FIBER` bit 6, set by the core (Rf:1011) | **(a)** |
| `F_YIELD` (19) | fiber parked in `Fiber::suspend()`: set Ff:979, cleared Ff:1060; read `scheduler.c:785, 802` (deadlock exemption), Ff:1464 (GC) | nothing; the core tracks it as `fiber->context.status == ZEND_FIBER_STATUS_SUSPENDED` (set Rf:899, cleared Rf:920; Rf:1221-1225 relies on it) | **(d)**. The deadlock exemption reads `((zend_fiber *) c->extended_data)->context.status == ZEND_FIBER_STATUS_SUSPENDED` under `IS_FIBER && extended_data != NULL` (the reference's own guard, `scheduler.c:785-786`). No Coroutine method needs it: `coroutine/028` holds with `isQueued` and `isSuspended` both read from QUEUED (section 7) |

## 6. Fork `ZEND_ASYNC_EVENT_F_*` (F:1043-1073) as they apply to a coroutine

| Event flag (bit) | On the coroutine: set / read | RFC core | Verdict |
|---|---|---|---|
| `CLOSED` (0) | "finished": `ZEND_COROUTINE_SET_FINISHED` at the very start of finalize (`coroutine.c:607`), `IS_FINISHED` (F:1815-1816) read 12 times | status FINISHED (R:101, 197-198) | **(a)**, with one ordering rule: FINISHED is set where the reference sets CLOSED, before notify and before finally handlers, so a `finally()` added from an awaiter callback runs at once (`coroutine.c:1397-1411`) and awaiters see `isCompleted()` |
| `RESULT_USED` (1) | written on any awaitable by `await` and friends (`async.c:319`, `async_API.c:842, 1007, 1258`, `scope.c:311, 459`); read only as `ZEND_FUTURE_IS_USED` (F:1941), never for a coroutine | nothing | **(d)**: no reader for a coroutine. The generic writers dispatch on the type bit and skip coroutines. Critic 2 asked for a bit: rejected, nothing reads it |
| `EXC_CAUGHT` (2) | set by `await` (`async.c:320`) and at finalize (`coroutine.c:671, 697, 720, 740`); read in `coroutine_object_destroy` to decide the rethrow (`coroutine.c:224`) | nothing | **(c)** `ASYNC_COROUTINE_F_EXC_CAUGHT` (bit 18). Not mergeable with bit 17: an `await` that set it and was then aborted would make finalize drop an unobserved exception |
| `ZVAL_RESULT` (3) | set unconditionally before the only notify (`coroutine.c:666`); read by callbacks (Fc:1256, `async_API.c:432`) that run only during that notify or a replay of a finished coroutine | nothing | **(d)**: constant true for a coroutine at every read; the type bit answers it. Critic 2 asked for a bit: rejected for this reason |
| `ZEND_OBJ` (4) | set `coroutine.c:132`; read by refcount macros (F:1158-1178) and the waker GC (`coroutine.c:314`) | `object_offset != 0` (R:166-172) | **(d)**; the type bit plus `ZEND_COROUTINE_OBJECT` answer it |
| `NO_FREE_MEMORY` (5) | set `coroutine.c:133`; no reader for a coroutine (dispose is `OBJ_RELEASE`) | nothing | **(d)** |
| `EXCEPTION_HANDLED` (6) | per-notify: cleared at notify start (Fc:1691, `coroutine.c:667`), set by awaiter callbacks (Fc:1266, `thread_pool.c:1073`), read right after notify and after `extended_dispose` (`coroutine.c:670, 696, 705`) | R:84: a finish handler marks handled by clearing `->exception` | **(c)** `ASYNC_COROUTINE_F_EXCEPTION_HANDLED` (bit 17). Finalize treats "handled" as bit 17 set **or** `->exception` cleared by a core finish handler; the extension's own callbacks set the bit so `getException()` and a later `await` (`coroutine/039`) still see the exception |
| `REFERENCE` (7) | never on a coroutine; but `ZEND_ASYNC_EVENT_IS_REFERENCE` reads the first word of whatever is in the waker (`coroutine.c:314`, F:1142-1148) | nothing | **(d)** for the coroutine; the reading sites dispatch on the type bit first (section 9). Polarity A must give the reference prefix bit 31 (section 1) |
| `OBJ_REF` (8) | not used by the coroutine | `F_OBJ_REF` bit 7 (R:147-150) | **(a)** the RFC flag covers the case; the reference never sets it |
| `CLOSE_FD` (9) | IO only | nothing | **(d)** for the coroutine |
| `HIDDEN` (10) | events only (deadlock accounting, `libuv_reactor.c:1100`) | nothing | **(d)** for the coroutine |
| `BAILOUT` (11) | set `scheduler.c:970, 987` (bailout of all coroutines); read `coroutine.c:1335` (finally handlers destroyed, not run) | the finish handler's `is_bailout` argument reaches C handlers only (R:83-90) | **(c)** `ASYNC_COROUTINE_F_BAILOUT` (bit 19) |

`ZEND_ASYNC_IO_F_MULTISHOT` (bit 13, F:1135) is an IO event bit; it never touches a coroutine
and stays with the IO event (S6).

## 7. Coroutine-level behaviours

| Behaviour | Reference | RFC core | Verdict |
|---|---|---|---|
| Status | waker status + `F_STARTED` + `CLOSED` (section 4 mapping) | status in bits 0-3, scheduler the only writer (R:92-102) | **(a)** status, **(b)** `F_STARTED`. Extra rule from the cancel path: the scheduler sets SUSPENDED when the waker turns WAITING, before `start_waker_events` replays, so the reference's "current but already waiting" case (`coroutine.c:894-905`) is `c == current && status == RUNNING` |
| A coroutine that yields with `Async\suspend()` | enqueues itself, then suspends (`async.c:232-234`) | enqueue covers CREATED/SUSPENDED (R:248-254) | **(a)**: QUEUED, put back by the extension itself (S3.md section 2). One queue entry per coroutine: push only on a transition into QUEUED (`coroutine.c:834-836` keeps it) |
| `protect()` | `async.c:237-294`; cancel defers into `deferred_cancellation` | nothing | **(c)** bit 16 + `deferred_cancellation`; nested protect fixed with a `was_protected` local (section 8.6) |
| Cancel delivery | `async_coroutine_cancel` (`coroutine.c:871-1004`): finished → no-op; current and running → `F_CANCELLED` + error into `->exception`, no throw (`:902-922`); protected → defer; not started → `IGNORED`, enqueue once, body skipped (`:956-980`, `:466-499`); else error into the waker, first cancellation kept (`:994-1001`, Fc:1352-1360), enqueue | slot `cancel` R:262-265; delivery = enqueue with error (R:248-254, RFC.md:164-171) | **(a)** the slot; the per-case behaviour is the reference's, over the new state. The not-started case is QUEUED + `F_CANCELLED` + no `F_STARTED`, and the run path skips the body on exactly that test |
| Cancel re-delivery | after delivery the waker's error is consumed (`scheduler.c:1735-1740`), so a later cancel finds `waker->error == NULL` and delivers again (`coroutine.c:994-1003`); `edge_cases/003` catches the deadlock cancellation and suspends again | ts drops every cancel after the first (ts:1451-1457) | **(c)**: pending = a cancellation object in `waker.error` or in `deferred_cancellation`; a cancel while one is pending is dropped (keeps the first, as the reference); after the throw a new cancel is delivered again. No bit (Critic 7 accepted in this form) |
| Self-cancel | `coroutine.c:902-922`: stored as the coroutine's exception, the body continues | nothing specific | **(a)** `F_CANCELLED` + `exception` |
| Finally handlers | `finally()` at once on a finished coroutine (`coroutine.c:1397-1411`), else queued; run at finalize in a child scope through an iterator (`:1329-1354`, `1275-1318`); destroyed on bailout (`:1335-1339`) | nothing | **(c)** `finally_handlers` + bit 19; without iterator or scope in S3 (S3.md section 5) |
| Finish notification (awaiters, task group, thread pool) | `ZEND_ASYNC_CALLBACKS_NOTIFY` on the coroutine event (`coroutine.c:668`) | slots `add/remove_finish_handler` R:375-378; core caller `zend_gc.c:2134` | **(a)** the slots, stored in **(c)** `callbacks` |
| `await` | `resume_when` on the coroutine event (`async.c:315-371`) | slot `await` R:323-329 | **(a)** the slot, implemented with the reference's `resume_when` on the coroutine's `callbacks` |
| Zombie | section 5 | nothing | **(d)** in S3, **(c)** in S9 |
| Main coroutine | created by the scheduler, `STARTED`+`MAIN` (`scheduler.c:1214-1247`); its exception surfaces eagerly (`coroutine.c:737-741`) | `launch` slot; core sets `F_MAIN` and RUNNING (Ra:737-738) | **(a)** + **(b)**: the core also sets `F_STARTED` there |
| Fiber coroutine | fork core mints it in `Fiber::start` (Ff:1305-1312) | `intercept_fiber` R:341-356; `zend_fiber_adopt` sets `internal_entry`, `extended_data`, `extended_dispose`, `F_FIBER` (Rf:1002-1021) | **(a)**; the deadlock exemption per section 5 (`F_YIELD`); `extended_dispose` must run on every death path (section 2) |
| Switch handlers | fork vector on `zend_coroutine_t`, enter/leave/finish (F:3043-3048) | slots R:371-374, handler `(coroutine, is_enter)` R:81 | **(a)** slots, **(c)** `switch_handlers` storage; finish call dropped |
| Result / exception | `result`, `exception`; replay (`coroutine.c:1040-1070`) | R:120, 122 | **(a)**; replay function **(c)** |
| Spawn location | `filename`/`lineno` | R:124-125 | **(a)** |
| Suspend location | `waker.filename`/`lineno` | nothing | **(c)** (section 4) |
| Awaiting info | `getAwaitingInfo` returns `[]` always: `get_awaiting_info` is a stub (`async_API.c:248-252`); tests check only `is_array` (`coroutine/010`, `026`) | slots `add/remove/get_awaiting_info` R:381-394; no core caller of `ZEND_ASYNC_ADD_AWAITING_INFO` (`git grep` on `834811f2d88`) | **(a)** the slots, no storage field in S3: `get_awaiting_info` describes the waker's events through their `info` (the deadlock report already does, `scheduler.c:719-737`); `add_awaiting_info` returns 0 ("the add did nothing", R:381-386) until a caller exists |
| Parked frame for GC and traces | `fiber_context->execute_data` (`async_API.c:1285-1290`) | slot `coroutine_execute_data`, "NULL when it is not suspended" (R:358-363) | **(a)**. The extension returns the frame for every parked coroutine, QUEUED after a self-yield included (`coroutine/037` traces one); R:358's comment says "suspended" and should say "parked": a wording fix for S3.2 |
| EH_THROW window | saved per switch in the fork core's `zend_fiber_vm_state` (Ff:108-127, 137-138, 156-157) and reset before the jump (Ff:515-519); tests `edge_cases/016`, `017` | not saved (Rf:106-152) | **(b)** decided: S3.2 adds the two fields and the reset to the RFC core as in the fork; no extension fields. Supersedes S3.md section 3 "the scheduler writes the target's saved pair" |

### Coroutine methods over the new state

Notation: `S` = `ZEND_COROUTINE_STATUS(c)`; `STARTED`, `CANCELLED`, `FINISHED` = the core predicates
after S3.2; `PROTECTED` = bit 16.

| Method | Reference (`coroutine.c`) | New formula | Tests that pin it |
|---|---|---|---|
| `isStarted` | `IS_STARTED` (`:1582`) | `STARTED` (bit 8) | `coroutine/005` (false after spawn, true after), `028` |
| `isQueued` | `waker.status == QUEUED` (`:1585-1596`), so a cancelled never-run coroutine (`IGNORED`) is false | `S == QUEUED && !(CANCELLED && !STARTED)` | `coroutine/028` (true before the first run and after `suspend()`); the cancelled-never-run case is not tested |
| `isRunning` | `STARTED && !FINISHED` (`:1598-1607`), so true while suspended too | `STARTED && !FINISHED` | `coroutine/013`, `038`; `S == RUNNING` also passes both (Critic's form); the reference formula is kept by P2.2 |
| `isSuspended` | `waker != NULL && waker.status < RESULT && !FINISHED` (`:1609-1620`, F:1796-1797) | `S == QUEUED || S == SUSPENDED` | `coroutine/028` (true after `suspend()`), `038` (false after finish, true in `delay`), `info/002` (true for five never-run coroutines), `fiber/019` |
| `isCancelled` | `CANCELLED && FINISHED` (`:1622-1628`) | `CANCELLED && FINISHED` | `coroutine/005`, `006`, `028`, `029` (false while protected), `038` |
| `isCancellationRequested` | `(CANCELLED && !FINISHED) || deferred_cancellation != NULL` (`:1630-1639`) | same, `deferred_cancellation` is the extension field | `coroutine/006`, `028`, `029` |
| `isCompleted` | `IS_FINISHED` (`:1641-1646`) | `S == FINISHED` | `coroutine/005`, `028`, `038` |
| `getResult` | `FINISHED ? result : null` (`:1442-1457`) | same | `coroutine/002`, `028` |
| `getException` | `FINISHED && exception ? exception : null` (`:1459-1474`) | same | `coroutine/003`, `004`, `028` |
| `getTrace` | `ZEND_COROUTINE_SUSPENDED` and a frame (`:1476-1515`) | `(S == QUEUED || S == SUSPENDED) && fiber_context && execute_data` | `coroutine/009`, `037` |
| `getSpawnFileAndLine`, `getSpawnLocation` | `filename`, `lineno` | same (R:124-125) | `coroutine/007` |
| `getSuspendFileAndLine`, `getSuspendLocation` | `waker.filename`, `waker.lineno` | same | `coroutine/008` |
| `getAwaitingInfo` | `GET_AWAITING_INFO ?: []` | same, via the RFC slot | `coroutine/010`, `026` |
| `cancel` | `ZEND_ASYNC_CANCEL(c, e, false)` (`:1662-1674`) | same, RFC slot | `coroutine/006`, `028`, `034` |
| `finally` | `FINISHED` → call now, else append (`:1381-1424`) | same | `coroutine/014`-`018`, `033` |
| `getId` | `std.handle` | same | `coroutine/001` |

Every method is answerable from the RFC status, bits 4 and 8, and `deferred_cancellation`; no
YIELD, STARTED or CANCEL_REQUESTED bit in the extension.

## 8. Critic round 0, finding by finding

| # | Finding | Answer |
|---|---|---|
| 1 | Redefining `IS_STARTED` breaks callers that mean "handed to the scheduler" | **Accepted.** Two predicates (section 5, `F_STARTED`): `IS_SCHEDULED` for Rf:770 and ts:1069, 1083, 1166, 1451; `IS_STARTED` = bit 8 for `isStarted` (ts:1774) and the extension. Scenario A (Rf:770 skipping the cancel of a queued, never-run fiber coroutine) is closed by `IS_SCHEDULED` |
| 2 | One flags word, two layouts: event macros applied to a coroutine | **Accepted in part.** Bits for `EXCEPTION_HANDLED` (17), `EXC_CAUGHT` (18), `BAILOUT` (19). **Rejected** for `RESULT_USED` (no reader for a coroutine) and `ZVAL_RESULT` (constant true at every read): both answered by the type bit. The waiter API takes `async_coroutine_t *`, the event API `zend_async_event_t *`, so an event macro on a coroutine does not compile; the dispatch list is section 9 |
| 3 | Polarity "0 = coroutine" makes zeroed memory a coroutine | **Not decided here** (Edmond). Section 1 lists what each polarity costs; nothing below depends on it. No S3 exposure; from S4 polarity A needs the bit at every event init and in the reference prefix |
| 4 | `F_STARTED` "at the first switch" misses in-place run, main, and marks cancelled-before-run as started | **Accepted.** "Immediately before the body's first instruction, by whoever runs it"; main by the core in `zend_async_scheduler_launch`; never on the cancelled-before-run path (reference `coroutine.c:524` after `:466-499`) |
| 5 | `isSuspended` via `F_YIELD` is wrong; the name clashes with the fiber `IS_YIELD` | **Accepted.** `isSuspended = S ∈ {QUEUED, SUSPENDED}` (FINISHED excluded by construction); `F_YIELD` dropped; the fiber exemption reads `context.status` |
| 6 | One PROTECTED bit cannot nest; the reference has the bug (`async.c:251, 276, 289-292`) | **Accepted.** `bool was_protected = IS_PROTECTED(c)` on entry; on exit clear the bit and deliver `deferred_cancellation` only if `!was_protected`. A deliberate fix of a reference defect; needs its own test (no reference test nests `protect`) |
| 7 | `deferred_cancellation` has two readings once unprotected | **Accepted** as in section 7, cancel re-delivery: the first pending cancellation wins (the reference's rule, not ts's replace), a new one after delivery is delivered |
| 8 | Rf:1375 needs only `extended_data == NULL` | **Accepted** (section 5, `F_CANCELLED`). Behaviour change named by the Critic stands: a scheduler-cancelled fiber may call `Fiber::suspend()` from `finally` |
| — | "Holds" paragraph: `isRunning = RUNNING` | **Not taken**: the reference formula `STARTED && !FINISHED` is kept (P2.2); no test separates the two |

## 9. Sites that cast between event and coroutine or read coroutine state through `event.flags`

Every site must change; "S3" marks code the S3 port takes, the stage otherwise.

### 9.1 Object ↔ coroutine through `ZEND_ASYNC_OBJECT_TO_EVENT`

Correct today only because the event sits at offset 0 of `zend_coroutine_t`, which sits at offset
0 of `async_coroutine_t`. Replace with `container_of` on `std` (an `ASYNC_COROUTINE_FROM_OBJ`), or
the RFC `zend_async_coroutine_from_object` (Ra:708-715) where the class is not known.

`coroutine.c:38` (`THIS_COROUTINE`, every method), `166` (destroy), `246` (free), `254` (GC), `422`
(`async_new_coroutine`). All S3.

### 9.2 Event pointer cast to a coroutine

- `coroutine.c:1045` (replay), `1074` (info), `1103` (dispose): the vtable slots go (section 3);
  each becomes a function on `async_coroutine_t *`. S3.
- `async_API.c:759` (`iterator_coroutine_finish_callback`: `((zend_coroutine_t *) event)->extended_data`):
  becomes a finish callback that receives the coroutine. S5.

### 9.3 Coroutine handed out as its embedded event

- `coroutine.c:132-134` (ZEND_OBJ, NO_FREE_MEMORY, offset), `145-153` (vtable fill): replaced by
  `object_offset` and, under polarity B, setting bit 31. S3.
- `coroutine.c:248, 674` (`zend_async_callbacks_free`): on `callbacks`. S3.
- `coroutine.c:430, 492, 503`, `async_API.c:140, 149, 166` (`event.dispose`): `ZEND_COROUTINE_RELEASE`. S3.
- `coroutine.c:666` (SET_ZVAL_RESULT): drop. `668` (NOTIFY): notify on `callbacks`. S3.
- `coroutine.c:224, 671, 697, 720, 740` (EXC_CAUGHT): bit 18. S3.
- `scheduler.c:708-716` (deadlock report: `event->info`): direct call. S3.
- `async_API.c:1103` (`resume_when(coroutine, &iterator_coroutine->event, ...)`): the coroutine
  waiter API. S5.
- `task_group.c:837, 841` (`cb->base.event = &coroutine->coroutine.event`, `event.add_callback`):
  `callbacks` push; `zend_coroutine_event_callback_t.event` (F:862-867) needs an awaitable header
  pointer, not `zend_async_event_t *`. S9.

### 9.4 Coroutine state read or written through `event.flags` (fork macros, F:1808-1849)

Each fork `ZEND_COROUTINE_*` macro is `(coroutine)->event.flags`; in the RFC they are
`(coroutine)->flags` (R:152-198) or the new bits. Mechanical, but every site is listed because
three of the fork's names now mean something else (`IS_STARTED`, `IS_FINISHED` → status,
`IS_EXCEPTION_HANDLED`/`IS_BAILOUT` → extension bits):

- `coroutine.c`: 172 IS_FIBER, 360 IS_FINISHED, 468 IS_CANCELLED, 524 SET_STARTED, 607
  SET_FINISHED, 667 CLR_EXCEPTION_HANDLED, 670/696/705 IS_EXCEPTION_HANDLED, 738 IS_MAIN, 739
  IS_FIBER, 764 IS_ZOMBIE, 881 IS_FINISHED, 907 SET_CANCELLED, 937 IS_PROTECTED, 953 IS_CANCELLED,
  954 SET_CANCELLED, 956 IS_STARTED, 1047 IS_FINISHED, 1079 SUSPENDED, 1335 IS_BAILOUT, 1397/1448/1465
  IS_FINISHED, 1490 SUSPENDED, 1582 IS_STARTED, 1605 IS_STARTED, 1606 IS_FINISHED, 1618 SUSPENDED,
  1619 IS_FINISHED, 1626 IS_CANCELLED, 1627 IS_FINISHED, 1636 IS_CANCELLED, 1637/1645 IS_FINISHED.
- `scheduler.c`: 160 IS_MAIN, 785/802 IS_FIBER + IS_YIELD (→ `context.status`), 867-868
  IS/CLR_PROTECTED, 916 IS_STARTED, 919 SET_CANCELLED, 924-925 IS/CLR_PROTECTED, 970/987
  SET_BAILOUT, 983 IS_FINISHED, 1246 SET_STARTED, 1247 SET_MAIN (the core does it, Ra:737),
  1468 IS_STARTED (→ `IS_SCHEDULED`).
- `async.c`: 251/276 SET/CLR_PROTECTED, 290 SET_CANCELLED, 1763 IS_MAIN (threads/fork, out of S3).
- `scope.c`: 51, 64, 912 IS_ZOMBIE, 914 SET_ZOMBIE (S9).
- `thread_pool.c:1073` SET_EXCEPTION_HANDLED (S9+).
- `ZEND_COROUTINE_SUSPENDED` (F:1796, waker-based) in the fork core's info (Fc:629): the new
  `isSuspended` formula.

### 9.5 Generic awaitable code that meets a coroutine at run time (type-bit dispatch)

These take any `Completable`/`Awaitable` and treat it as an event; a `Coroutine` arrives here.
Each needs `ASYNC_AWAITABLE_IS_COROUTINE` first:

- S3: `async.c:315-371` (`await`: OBJECT_TO_EVENT, SET_RESULT_USED, SET_EXC_CAUGHT, IS_CLOSED,
  `replay`, EXTRACT_RESULT, `resume_when`); `scheduler.c:1135-1147` (`start_waker_events`: IS_CLOSED,
  `replay`, `start`); `coroutine.c:312-318` (waker GC: IS_REFERENCE, IS_ZEND_OBJ, EVENT_TO_OBJECT on
  each waited awaitable: under the new layout bit 4 of a coroutine is `F_CANCELLED`); the moved fork
  core: `zend_async_resume_when` (Fc:1117 IS_CLOSED, 1171 `add_callback`, 1126/1180/1232 `dispose`,
  1215/1221 `del_callback`, 1241 ADD_REF), waker teardown (Fc:795 `del_callback`, 802/816
  EVENT_RELEASE, 829/1026 `stop`, 1038-1039 `del_callback`), `zend_async_waker_callback_resolve`
  (Fc:1256 WILL_ZVAL_RESULT, 1266 SET_EXCEPTION_HANDLED), `zend_async_callbacks_notify` (Fc:1689
  ADD_REF, 1691 CLR_EXCEPTION_HANDLED, 1696-1739 RELEASE), `triggered_events` ADD_REF (Fc:1097;
  dropped with the field).
- S5: `async_API.c:311` (`zval_to_event`, the `await_*` family), `432` (WILL_ZVAL_RESULT), `842`,
  `1007` (SET_RESULT_USED), `1255-1270` (`async_resolve_cancel_token`: a coroutine may be the
  token); cancellation tokens at `async.c:316, 400, 451, 508, 550, 603, 645, 1394, 1462, 1482`,
  `future.c:1348`.
- S8/S9: `channel.c:711`, `thread_channel.c:567, 602, 624`, `scope.c:310-311, 458-459`.

## 10. Core changes this consolidation needs in S3.2

1. `ZEND_COROUTINE_F_STARTED` (bit 8); `ZEND_COROUTINE_IS_STARTED` tests it;
   `ZEND_COROUTINE_IS_SCHEDULED` = `status != CREATED`; Rf:770 uses `IS_SCHEDULED`; the ts callers
   (ts:1069, 1083, 1166, 1451) move to it and ts sets `F_STARTED` before a body runs; the contract
   comment says who sets it; `zend_async_scheduler_launch` sets it on main (Ra:737-738).
2. Rf:1375: drop `ZEND_COROUTINE_IS_CANCELLED(current)`.
3. The flags comment (R:105-106) states bits 0-15 core, 16-31 the scheduler's.
4. EH_THROW window in `zend_fiber_vm_state` plus the reset before the jump, as Ff:108-127,
   137-138, 156-157, 515-519.
5. R:358-363 comment: "parked" instead of "suspended" (a self-yielded QUEUED coroutine has a frame
   the GC must see).

## 11. Decided here without Edmond, and open

- The vector of awaiters is named `callbacks` (the reference's `event.callbacks`), not "waiters"
  (the brief's word), under the naming rule.
- `isRunning` keeps the reference formula `STARTED && !FINISHED`, true while suspended; the
  Critic's `S == RUNNING` is the cleaner reading and no test separates them.
- `isQueued` keeps the reference's exclusion of a cancelled never-run coroutine; untested.
- Nested `protect()` is fixed (`was_protected`), a departure from the reference that needs an own
  test. Edge left open: a deadlock or shutdown clears PROTECTED and cancels through the waker while
  `deferred_cancellation` may still hold an earlier cancel, which the outer `protect` then throws a
  second time (the reference does the same; no test).
- `add_awaiting_info` stores nothing in S3 and returns 0; valid by R:381-386, revisit when a
  caller appears.
- The waker's `status` is kept with a mapping to the RFC status; whether to fold it is the size
  expert's.
- Bit numbers 16-20 are a proposal.
- Not checked: that a suspend location derived from the parked frame equals
  `zend_apply_current_filename_and_line` at the suspend call in every case.
