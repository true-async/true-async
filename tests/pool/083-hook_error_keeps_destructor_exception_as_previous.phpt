--TEST--
Pool: when the destructor throws after a hook's Error, the caller gets the Error with the destructor's exception as its previous
--FILE--
<?php

$pool = new Async\Pool(
    factory: fn() => 'r',
    destructor: function ($resource) {
        throw new RuntimeException("destructor failed");
    },
    beforeAcquire: fn($resource) => throw new TypeError("hook bug"),
    min: 1,
);

try {
    $pool->acquire();
    echo "acquired\n";
} catch (Throwable $e) {
    echo $e::class, ": ", $e->getMessage(), "\n";
    echo "previous: ", $e->getPrevious()::class, ": ", $e->getPrevious()->getMessage(), "\n";
}
?>
--EXPECT--
TypeError: hook bug
previous: RuntimeException: destructor failed
