--TEST--
Pool: a factory that returns by reference: the object it returns is handed out and taken back
--FILE--
<?php

$pool = new Async\Pool(factory: function &() {
    $connection = new stdClass();

    return $connection;
});

$resource = $pool->acquire();
var_dump($resource instanceof stdClass);

$pool->release($resource);
echo "idle: ", $pool->idleCount(), ", active: ", $pool->activeCount(), "\n";
?>
--EXPECT--
bool(true)
idle: 1, active: 0
