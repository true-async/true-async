--TEST--
get_deadlocked_coroutines(): main awaiting a coroutine kept in a global, which awaits main, is not reported: the global scope is a root
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\current_coroutine;
use function Async\get_deadlocked_coroutines;

$main = current_coroutine();
$other = spawn(function () use ($main) {
    await($main);
});

spawn(function () {
    for ($i = 0; $i < 4; $i++) {
        suspend();
    }

    echo count(get_deadlocked_coroutines()), " found\n";
    $GLOBALS['other']->cancel();
});

try {
    await($other);
} catch (Async\AsyncCancellation $cancellation) {
    echo "main: ", $cancellation->getMessage(), "\n";
}
?>
--EXPECT--
0 found
main: Coroutine cancelled
