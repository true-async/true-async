--TEST--
A running coroutine cancelled by itself runs on: its own exception keeps the cancellation as its previous, and a second cancellation keeps the first as the outcome
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use Async\AsyncCancellation;

$throws = spawn(function () {
    Async\current_coroutine()->cancel(new AsyncCancellation("first"));
    throw new Exception("second");
});

try {
    await($throws);
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), ", previous: ", $e->getPrevious()?->getMessage(), "\n";
}

$cancelled = spawn(function () {
    Async\current_coroutine()->cancel(new AsyncCancellation("first"));
    throw new AsyncCancellation("second");
});

try {
    await($cancelled);
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

echo "main end\n";
?>
--EXPECT--
Exception: second, previous: first
Async\AsyncCancellation: first
main end
