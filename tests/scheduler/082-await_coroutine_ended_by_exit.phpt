--TEST--
A coroutine ended by exit() finishes with neither a result nor an exception: await() in a shutdown function returns null
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$coroutine = spawn(function () {
    echo "coroutine exits\n";
    exit();
});

register_shutdown_function(function () use ($coroutine) {
    var_dump($coroutine->isCompleted(), $coroutine->getException());
    var_dump(await($coroutine));
});

echo "main end\n";
?>
--EXPECT--
main end
coroutine exits
bool(true)
NULL
NULL
