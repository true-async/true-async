--TEST--
Coroutine::finally() on a finished coroutine calls the handler at once, and what it throws reaches the caller
--FILE--
<?php

use function Async\await;
use function Async\spawn;

$coroutine = spawn(fn() => 1);
await($coroutine);

try {
    $coroutine->finally(function (Async\Coroutine $finished) {
        echo "called, same coroutine: ", var_export($finished === $GLOBALS['coroutine'], true), "\n";
        throw new Exception('late');
    });
} catch (Exception $e) {
    echo "caught: ", $e->getMessage(), "\n";
}

echo "end\n";

?>
--EXPECT--
called, same coroutine: true
caught: late
end
