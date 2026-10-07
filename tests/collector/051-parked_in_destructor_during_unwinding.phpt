--TEST--
get_deadlocked_coroutines(): a coroutine parked on a dead Future in a destructor that the unwinding of an exception called is found
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\get_deadlocked_coroutines;

final class Waits
{
    public function __destruct()
    {
        (new Future(new FutureState()))->await();
    }
}

function unwind(): void
{
    $waits = new Waits();
    throw new RuntimeException("unwinding");
}

spawn(function () {
    try {
        unwind();
    } catch (RuntimeException $e) {
        echo "caught: ", $e->getMessage(), "\n";
    }
});

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = get_deadlocked_coroutines();
echo count($found), " found\n";

foreach ($found as $coroutine) {
    $coroutine->cancel();
}
?>
--EXPECT--
1 found
