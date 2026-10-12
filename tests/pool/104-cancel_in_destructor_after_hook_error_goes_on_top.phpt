--TEST--
Pool: a cancellation that reaches the destructor after a hook's Error goes on top, with the Error beneath it
--FILE--
<?php

$parked = false;

$pool = new Async\Pool(
    factory: fn() => 'r',
    destructor: function ($resource) use (&$parked) {
        $parked = true;
        Async\delay(10000);
    },
    beforeAcquire: fn($resource) => throw new TypeError("hook bug"),
    min: 1,
);

$caller = Async\spawn(function () use ($pool) {
    try {
        $pool->acquire();
    } catch (Throwable $e) {
        echo $e::class, ", previous: ", $e->getPrevious()::class, "\n";
    }
});

while (!$parked) {
    Async\suspend();
}

$caller->cancel();
Async\await($caller);
?>
--EXPECT--
Async\AsyncCancellation, previous: TypeError
