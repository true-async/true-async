--TEST--
The automatic run: an error handler that waits gets the scheduler-context Error, which is released, and the request goes on
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\delay;
use function Async\get_deadlocked_coroutines;

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
    echo "handler waits\n";
    delay(1);
    echo "never here\n";
});

start_pair();
delay(10);
delay(10);
echo "main goes on\n";

foreach (get_deadlocked_coroutines() as $coroutine) {
    $coroutine->cancel();
}

echo "end\n";
?>
--EXPECT--
handler waits
handler waits
main goes on
end
