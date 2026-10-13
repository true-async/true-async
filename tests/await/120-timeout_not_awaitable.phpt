--TEST--
A Timeout is a cancellation token only: await() of it throws
--FILE--
<?php

use function Async\await;
use function Async\timeout;

try {
    await(timeout(10));
} catch (Error $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

?>
--EXPECT--
Error: Async\Timeout can only be used as a cancellation token
