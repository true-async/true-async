--TEST--
await_*() refuses the waiting coroutine among its triggers, as await() does
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\await_all;
use function Async\current_coroutine;

$coroutine = spawn(function () {
    try {
        await_all([current_coroutine()]);
    } catch (Error $e) {
        echo get_class($e), ": ", $e->getMessage(), "\n";
    }
});

await($coroutine);

?>
--EXPECT--
Error: Cannot await a coroutine from within itself
