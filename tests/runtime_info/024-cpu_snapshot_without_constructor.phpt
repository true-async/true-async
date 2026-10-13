--TEST--
CpuSnapshot: an object made without its constructor has uninitialised properties
--FILE--
<?php

$snapshot = (new ReflectionClass(Async\CpuSnapshot::class))->newInstanceWithoutConstructor();

try {
    var_dump($snapshot->wallNs);
} catch (Error $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
Typed property Async\CpuSnapshot::$wallNs must not be accessed before initialization
