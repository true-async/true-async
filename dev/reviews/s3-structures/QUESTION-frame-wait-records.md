# Question: wait records in the waiting frame, no events in the waker (2026-10-02)

Edmond's direction (not decided): the waker stores no events. `await_any` is a special case with its own logic.

Sketch:

```c
/* await($x), simplified */
async_wait_record_t rec = { .callback = ..., .waiter = current };  /* on the coroutine's C stack */
add(x, &rec.callback);   /* into x's waiters vector */
suspend();               /* returns on result, cancellation or timeout */
del(x, &rec.callback);   /* the same frame unsubscribes */
```

- Unsubscription: whoever subscribed unsubscribes after the wake; `await_any` keeps an array of records in its
  own frame.
- No allocation per wait, even with several targets: records live on the coroutine's stack.
- The waker keeps error and result, plus one pointer to the current wait record, so the S7 collector and the
  deadlock report still see what the coroutine waits for (the wait-graph edge of DECISIONS 2026-10-01 lives on
  the stack).
- Main risk: a bailout longjmps over `del`, leaving a pointer to a dead stack in the event's vector; the
  bailout path must clean the records through the waker's pointer; the same when a fiber stack is destroyed.
- Edmond: with this, the event may need no methods at all. He also disputes that every event has a PHP class
  (EDMOND-DECISIONS 25): some events have none.
