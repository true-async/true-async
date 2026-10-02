--TEST--
S3.7 item 9: a spawned coroutine awaiting itself gets an Error and goes on running
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\current_coroutine;

$coroutine = spawn(function() {
    try {
        await(current_coroutine());
        echo "no exception\n";
    } catch (Throwable $e) {
        var_dump($e instanceof Error);
        echo $e->getMessage(), "\n";
    }
    echo "coroutine goes on\n";
    return "done";
});

var_dump(await($coroutine));
?>
--EXPECT--
bool(true)
Cannot await a coroutine from within itself
coroutine goes on
string(4) "done"
