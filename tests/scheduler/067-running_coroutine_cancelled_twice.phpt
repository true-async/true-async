--TEST--
A running coroutine that cancels itself twice keeps the first cancellation as its outcome
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use Async\AsyncCancellation;

$coroutine = spawn(function () {
    Async\current_coroutine()->cancel(new AsyncCancellation("first"));
    Async\current_coroutine()->cancel(new AsyncCancellation("second"));

    return "ran on";
});

try {
    await($coroutine);
} catch (AsyncCancellation $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
first
