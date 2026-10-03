--TEST--
An enqueue with an error of a finished coroutine is refused with an Error, and an error handed over with it is released
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$coroutine = spawn(function () {
    return 1;
});
await($coroutine);

foreach ([false, true] as $transfer) {
    try {
        TrueAsync\Test\enqueue_with_error($coroutine, new Exception("late"), $transfer);
    } catch (Error $e) {
        echo $e->getMessage(), "\n";
    }
}

var_dump(await($coroutine));
?>
--EXPECT--
Cannot enqueue a finished coroutine
Cannot enqueue a finished coroutine
int(1)
