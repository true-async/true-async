--TEST--
A Timeout is a cancellation token only: a Timeout among await_all() triggers throws
--FILE--
<?php

use function Async\await_all;
use function Async\timeout;

try {
    await_all([timeout(10)]);
} catch (Error $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}

?>
--EXPECT--
Error: Async\Timeout can only be used as a cancellation token
