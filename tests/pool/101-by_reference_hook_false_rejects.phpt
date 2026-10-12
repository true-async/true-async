--TEST--
Pool: a beforeAcquire that returns false by reference rejects the resource
--FILE--
<?php

$made = 0;

$pool = new Async\Pool(
    factory: function () use (&$made) {
        return ++$made;
    },
    destructor: function ($resource) {
        echo "destroyed $resource\n";
    },
    beforeAcquire: function &($resource) {
        $verdict = $resource !== 1;

        return $verdict;
    },
    min: 1,
);

$resource = $pool->acquire();
echo "acquired $resource\n";
?>
--EXPECT--
destroyed 1
acquired 2
