--TEST--
Pool: close() called by a resource's destructor while close() runs does nothing, and every resource is destroyed once
--FILE--
<?php

$pool = new Async\Pool(
    factory: function () {
        static $id = 0;
        return ++$id;
    },
    destructor: function ($resource) use (&$pool) {
        echo "destroyed $resource\n";
        $pool->close();
    },
    min: 3,
);

$pool->close();
printf("closed=%s count=%d\n", var_export($pool->isClosed(), true), count($pool));
?>
--EXPECT--
destroyed 1
destroyed 2
destroyed 3
closed=true count=0
