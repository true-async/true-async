--TEST--
Pool: beforeRelease that drops the last outside reference to its pool: the call keeps the pool, which closes once the release ends
--FILE--
<?php

$pool = new Async\Pool(
    factory: fn() => 'r',
    destructor: function ($resource) {
        echo "destroyed $resource\n";
    },
    beforeRelease: function ($resource) use (&$pool) {
        $pool = null;
        echo "reference dropped\n";
        return true;
    },
);

$resource = $pool->tryAcquire();
$pool->release($resource);
echo "after release\n";
?>
--EXPECT--
reference dropped
destroyed r
after release
