--TEST--
A spawned Async\Coroutine is not cloneable and not serializable
--FILE--
<?php
$coroutine = Async\spawn(fn() => 1);

try {
    clone $coroutine;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

try {
    serialize($coroutine);
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}

try {
    unserialize('O:15:"Async\Coroutine":0:{}');
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
Trying to clone an uncloneable object of class Async\Coroutine
Serialization of 'Async\Coroutine' is not allowed
Unserialization of 'Async\Coroutine' is not allowed
