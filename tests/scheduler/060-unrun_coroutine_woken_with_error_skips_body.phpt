--TEST--
A coroutine woken with an error before it ran (the core's enqueue with an error) does not run its body: the error is its outcome
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$coroutine = spawn(function () {
    echo "body\n";

    return 1;
});

var_dump(TrueAsync\Test\enqueue_with_error($coroutine, new Exception("woken with an error")));

try {
    await($coroutine);
} catch (Exception $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

var_dump($coroutine->isStarted(), $coroutine->isCompleted());
?>
--EXPECT--
bool(true)
Exception: woken with an error
bool(false)
bool(true)
