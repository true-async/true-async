--TEST--
The automatic run: a fatal error in the warning's error handler ends the request
--INI--
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
        suspend();
        await($b);
    });
    $b = spawn(function () use (&$a) {
        suspend();
        await($a);
    });
}

set_error_handler(function (int $type, string $message) {
    echo "handler: ", $message, "\n";
    eval('final class Twice {} final class Twice {}');
});

register_shutdown_function(function () {
    echo "shutdown\n";
});

start_pair();
wait_for_idle_run();
echo "not reached\n";
// A delay can end before the scheduler goes idle: a timer due when main parks wakes it in place
function wait_for_idle_run(): void
{
    $runs = TrueAsync\Test\collector_idle_runs();

    while (TrueAsync\Test\collector_idle_runs() === $runs) {
        delay(1);
    }
}
?>
--EXPECTF--
handler: Partial deadlock: coroutine #%d spawned at %s:11 can never wake (await: coroutine #%d)

Fatal error: Cannot redeclare class Twice %a
shutdown
