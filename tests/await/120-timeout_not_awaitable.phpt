--TEST--
A Timeout is a cancellation token only: await() of it and a Timeout among await_* triggers throw
--FILE--
<?php

use function Async\await;
use function Async\await_all;
use function Async\timeout;

try {
    await(timeout(10));
} catch (Error $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

try {
    await_all([timeout(10)]);
} catch (Error $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

try {
    new Async\Timeout();
} catch (Error $e) {
    echo get_class($e), "\n";
}

?>
--EXPECT--
Error: Async\Timeout can only be used as a cancellation token
Error: Async\Timeout can only be used as a cancellation token
Error
