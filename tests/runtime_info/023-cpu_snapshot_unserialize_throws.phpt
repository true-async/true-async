--TEST--
CpuSnapshot: unserialize() of a crafted string throws
--FILE--
<?php

try {
    unserialize('O:17:"Async\CpuSnapshot":0:{}');
} catch (Exception $exception) {
    echo $exception->getMessage(), "\n";
}
?>
--EXPECT--
Unserialization of 'Async\CpuSnapshot' is not allowed
