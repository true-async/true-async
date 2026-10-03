--TEST--
A wake with an error of the running coroutine itself is refused, as a wake of any coroutine that is not suspended: it finishes once and the queue keeps no entry of it
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;

$coroutine = spawn(function () {
    try {
        TrueAsync\Test\enqueue_with_error(Async\current_coroutine(), new Exception("woken"), true);
    } catch (Error $e) {
        echo "coroutine: ", $e->getMessage(), "\n";
    }

    echo "coroutine returns\n";
});

suspend();
suspend();
var_dump($coroutine->isCompleted());

try {
    TrueAsync\Test\enqueue_with_error(Async\current_coroutine(), new Exception("woken"));
} catch (Error $e) {
    echo "main: ", $e->getMessage(), "\n";
}

echo "main end\n";
?>
--EXPECT--
coroutine: Cannot resume a coroutine that has not been suspended
coroutine returns
bool(true)
main: Cannot resume a coroutine that has not been suspended
main end
