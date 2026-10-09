--TEST--
await_*() refuses an item that is not Completable, in an array and in a Traversable
--FILE--
<?php

use function Async\await_all;
use function Async\await_any_or_fail;

try {
    await_all([1]);
} catch (Async\AsyncException $e) {
    echo "array: ", $e->getMessage(), "\n";
}

function items(): Generator
{
    yield "a" => "not a future";
}

try {
    await_any_or_fail(items());
} catch (Async\AsyncException $e) {
    echo "traversable: ", $e->getMessage(), "\n";
}

?>
--EXPECT--
array: Expected item to be an Async\Completable object
traversable: Expected item to be an Async\Completable object
