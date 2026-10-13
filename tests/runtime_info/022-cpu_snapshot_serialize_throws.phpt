--TEST--
CpuSnapshot: serialize() throws
--FILE--
<?php

try {
    serialize(Async\CpuSnapshot::now());
} catch (Exception $exception) {
    echo $exception->getMessage(), "\n";
}
?>
--EXPECT--
Serialization of 'Async\CpuSnapshot' is not allowed
