--TEST--
Pool: exit() inside beforeAcquire still calls the destructor with the resource
--FILE--
<?php

$pool = new Async\Pool(
    factory: fn() => 'r',
    destructor: function ($resource) {
        echo "destroyed $resource\n";
    },
    beforeAcquire: function ($resource) {
        exit("exit in hook\n");
    },
    min: 1,
);

$pool->acquire();
echo "not reached\n";
?>
--EXPECT--
exit in hook
destroyed r
