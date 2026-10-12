--TEST--
Pool: serialize() is refused
--FILE--
<?php

try {
    serialize(new Async\Pool(factory: fn() => 1));
    echo "serialized\n";
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
Serialization of 'Async\Pool' is not allowed
