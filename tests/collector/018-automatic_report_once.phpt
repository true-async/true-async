--TEST--
The automatic run: one warning per stuck coroutine across runs at every idle point, an error handler that throws or calls get_deadlocked_coroutines() is contained
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;
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

set_error_handler(function (int $type, string $message, string $file, int $line) {
    echo basename($file), ":", $line, ": ", $message, "\n";

    try {
        get_deadlocked_coroutines();
    } catch (Error $error) {
        echo "handler: ", $error->getMessage(), "\n";
    }

    throw new ErrorException($message);
});

start_pair();
delay(10);
echo "after the first idle\n";
delay(10);
echo "after the second idle\n";

$found = get_deadlocked_coroutines();

foreach ($found as $coroutine) {
    $coroutine->cancel();
}

echo "end\n";
?>
--EXPECTF--
%s:14: Partial deadlock: coroutine #%d spawned at %s:12 can never wake (await: coroutine #%d)
handler: The operation cannot be executed in the scheduler context
%s:18: Partial deadlock: coroutine #%d spawned at %s:16 can never wake (await: coroutine #%d)
handler: The operation cannot be executed in the scheduler context
after the first idle
after the second idle
end
