--TEST--
Pool: a cancellation reaching a coroutine suspended inside beforeAcquire passes to it and destroys the resource
--FILE--
<?php

use Async\AsyncCancellation;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$inHook = false;

$pool = new Async\Pool(
    factory: fn() => 'r',
    destructor: function ($resource) {
        echo "destroyed $resource\n";
    },
    beforeAcquire: function ($resource) use (&$inHook) {
        $inHook = true;
        while (true) {
            suspend();
        }
    },
    min: 1,
);

$user = spawn(function () use ($pool) {
    try {
        $pool->acquire();
        echo "acquired\n";
    } catch (AsyncCancellation $e) {
        echo "acquire: ", $e::class, "\n";
    }
});

while (!$inHook) {
    suspend();
}

$user->cancel();
await($user);
printf("count=%d\n", count($pool));
?>
--EXPECT--
destroyed r
acquire: Async\AsyncCancellation
count=0
