--TEST--
Pool: an Error thrown by beforeAcquire destroys the resource and passes to the caller of acquire()
--FILE--
<?php

$pool = new Async\Pool(
    factory: fn() => 'r',
    destructor: function ($resource) {
        echo "destroyed $resource\n";
    },
    beforeAcquire: fn($resource) => $resource->ping(),
    min: 1,
);

try {
    $pool->acquire();
    echo "acquired\n";
} catch (Error $e) {
    echo $e::class, ": ", $e->getMessage(), "\n";
}

printf("count=%d\n", count($pool));
?>
--EXPECT--
destroyed r
Error: Call to a member function ping() on string
count=0
