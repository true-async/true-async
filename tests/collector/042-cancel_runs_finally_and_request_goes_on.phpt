--TEST--
The automatic run with cancel: each stuck coroutine is warned about once and cancelled with AsyncCancellation("Deadlock detected"), its finally runs, and the request goes on
--INI--
true_async.partial_deadlock=cancel
true_async.partial_deadlock_interval=0
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\delay;

function start_pair(): void
{
    $a = null;
    $b = null;
    $a = spawn(function () use (&$b) {
        try {
            suspend();
            await($b);
        } catch (Async\AsyncCancellation $e) {
            echo "a: ", $e::class, ": ", $e->getMessage(), "\n";
        } finally {
            echo "a: finally\n";
        }
    });
    $b = spawn(function () use (&$a) {
        try {
            suspend();
            await($a);
        } catch (Async\AsyncCancellation $e) {
            echo "b: ", $e::class, ": ", $e->getMessage(), "\n";
        } finally {
            echo "b: finally\n";
        }
    });
}

start_pair();
delay(10);
echo "main goes on\n";
delay(10);
echo "end\n";
?>
--EXPECTF--
Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: coroutine #%d) in %s on line %d

Warning: Partial deadlock: coroutine #%d spawned at %s:%d can never wake (await: coroutine #%d) in %s on line %d
a: Async\AsyncCancellation: Deadlock detected
a: finally
b: Async\AsyncCancellation: Deadlock detected
b: finally
main goes on
end
