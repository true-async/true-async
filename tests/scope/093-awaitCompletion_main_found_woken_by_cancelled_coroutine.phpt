--TEST--
true_async.partial_deadlock=cancel: main found in awaitCompletion() is not cancelled, and a cancelled coroutine that cancels the scope in its finally wakes it
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
use function Async\delay;

function run(): void
{
    spawn(fn() => delay(50));
    $s = new Scope();
    spawn(function () use ($s) {
        try {
            await(new Future(new FutureState()));
        } finally {
            echo "w: finally\n";
            $s->cancel();
        }
    });
    $s->spawn(fn() => await(new Future(new FutureState())));
    try {
        $s->awaitCompletion(new Future(new FutureState()));
        echo "main: completed\n";
    } catch (Throwable $e) {
        echo "main: ", $e->getMessage(), "\n";
    }
}
run();
?>
--EXPECTF--

Warning: Partial deadlock: coroutine #%d can never wake (await: scope created at %s:12) in %s on line 23

Warning: Partial deadlock: coroutine #%d spawned at %s:13 can never wake (await: future) in %s on line 15

Warning: Partial deadlock: coroutine #%d spawned at %s:21 can never wake (await: future) in %s on line 21
w: finally
main: Scope was cancelled
