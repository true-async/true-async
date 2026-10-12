--TEST--
The automatic run: exit() in the warning's error handler ends the request as in a coroutine
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
        try {
            suspend();
            await($b);
        } finally {
            echo "a ends\n";
        }
    });
    $b = spawn(function () use (&$a) {
        try {
            suspend();
            await($a);
        } finally {
            echo "b ends\n";
        }
    });
}

set_error_handler(function (int $type, string $message) {
    echo "handler: ", $message, "\n";
    exit(3);
});

register_shutdown_function(function () {
    echo "shutdown\n";
});

start_pair();

try {
    wait_for_idle_run();
    echo "main resumed\n";
} finally {
    echo "main ends\n";
}

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
main ends
a ends
b ends
shutdown
