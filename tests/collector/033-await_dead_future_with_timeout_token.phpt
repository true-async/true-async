--TEST--
get_deadlocked_coroutines(): a coroutine awaiting a dead Future with a Timeout as its token is not reported, and the Timeout ends its wait
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\delay;
use function Async\timeout;
use function Async\get_deadlocked_coroutines;

function start(): void
{
    $future = new Future(new FutureState());
    spawn(function () use ($future) {
        try {
            $future->await(timeout(30));
        } catch (Throwable $e) {
            $GLOBALS['error_class'] = $e::class;
        }
    });
}

$error_class = null;
start();

for ($i = 0; $i < 4; $i++) {
    suspend();
}

echo count(get_deadlocked_coroutines()), " found\n";

// Main prints the end of the wait: the timeout can fire before the check on a loaded machine
while ($error_class === null) {
    delay(1);
}

echo "wait ended: ", $error_class, "\n";
echo "end\n";
?>
--EXPECT--
0 found
wait ended: Async\OperationCanceledException
end
