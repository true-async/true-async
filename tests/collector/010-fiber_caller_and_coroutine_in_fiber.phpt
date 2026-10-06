--TEST--
get_deadlocked_coroutines(): a coroutine parked inside a Fiber, the coroutine awaiting it and the Fiber's caller parked in start() are not reported
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\current_coroutine;
use function Async\get_deadlocked_coroutines;

final class Box
{
    public static ?stdClass $holder = null;
}

function start(): void
{
    Box::$holder = new stdClass();
    spawn(function () {
        $fiber = new Fiber(function () {
            $holder = Box::$holder;
            Box::$holder = null;
            $holder->fiber_coroutine = current_coroutine();
            await($holder->other);
        });
        $fiber->start();
    });
    Box::$holder->other = spawn(function () {
        $holder = Box::$holder;
        suspend();
        suspend();
        await($holder->fiber_coroutine);
    });
}

start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";

foreach (Async\get_coroutines() as $coroutine) {
    if ($coroutine !== current_coroutine()) {
        $coroutine->cancel();
    }
}

suspend();
echo "end\n";
?>
--EXPECT--
0 found
end
