--TEST--
Collector oracle: a found waiter in awaitCompletion() of a scope that another scope's error route cancels is excused, as the route is left out
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
use function Async\delay;

$s = Scope::inherit();
$s->spawn(function () {
    await(new Future(new FutureState()));
});
$r = new Scope();
$r->spawn(function () use ($s, $r) {
    try {
        $s->awaitCompletion(new Future(new FutureState()));
        echo "waiter: completed\n";
    } catch (Throwable $e) {
        echo "waiter: ", get_class($e), ": ", $e->getMessage(), "\n";
    }
});
unset($s, $r);
spawn(function () {
    delay(50);
    throw new RuntimeException("other failed");
});
delay(100);
echo "end\n";
?>
--EXPECTF--

Warning: Partial deadlock: coroutine #%d spawned at %s:10 can never wake (await: future) in %s on line 11

Warning: Partial deadlock: coroutine #%d spawned at %s:14 can never wake (await: scope created at %s:9) in %s on line 16
waiter: Async\AsyncCancellation: Graceful shutdown

Fatal error: Uncaught RuntimeException: other failed in %s:25
Stack trace:
#0 [internal function]: {closure:%s:23}()
#1 {main}
  thrown in %s on line 25
