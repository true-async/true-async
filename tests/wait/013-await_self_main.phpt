--TEST--
S3.7 item 9: main awaiting itself gets an Error and goes on running
--FILE--
<?php

use function Async\await;
use function Async\current_coroutine;

try {
    await(current_coroutine());
    echo "no exception\n";
} catch (Throwable $e) {
    var_dump($e instanceof Error);
    echo $e->getMessage(), "\n";
}

echo "main goes on\n";
?>
--EXPECT--
bool(true)
Cannot await a coroutine from within itself
main goes on
