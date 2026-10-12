--TEST--
Pool: an OperationCanceledException of beforeAcquire's own token is an Error: the resource is destroyed and acquire() throws it
--FILE--
<?php

$never = new Async\Future(new Async\FutureState());

$pool = new Async\Pool(
    factory: fn() => 'r',
    destructor: function ($resource) {
        echo "destroyed $resource\n";
    },
    beforeAcquire: function ($resource) use ($never) {
        Async\await($never, Async\timeout(1));

        return true;
    },
    min: 1,
);

try {
    $pool->acquire();
} catch (Async\OperationCanceledException $e) {
    echo "acquire: ", $e::class, "\n";
}

echo "count: ", $pool->count(), "\n";
?>
--EXPECT--
destroyed r
acquire: Async\OperationCanceledException
count: 0
