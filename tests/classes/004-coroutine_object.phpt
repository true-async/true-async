--TEST--
Async\Coroutine is final, not cloneable and not serializable; its methods wait for their steps
--FILE--
<?php
$class = new ReflectionClass(Async\Coroutine::class);
var_dump($class->isFinal(), $class->implementsInterface(Async\Completable::class));

$coroutine = new Async\Coroutine();

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
    $coroutine->isStarted();
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
bool(true)
bool(true)
Trying to clone an uncloneable object of class Async\Coroutine
Serialization of 'Async\Coroutine' is not allowed
Async\Coroutine::isStarted() is not implemented yet
