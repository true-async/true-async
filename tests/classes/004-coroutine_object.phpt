--TEST--
Async\Coroutine is final and comes only from spawn: new and reflection cannot build one
--FILE--
<?php
$class = new ReflectionClass(Async\Coroutine::class);
var_dump($class->isFinal(), $class->implementsInterface(Async\Completable::class));

try {
    new Async\Coroutine();
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

try {
    $class->newInstanceWithoutConstructor();
} catch (ReflectionException $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
bool(true)
bool(true)
Instantiation of class Async\Coroutine is not allowed, use Async\spawn()
Class Async\Coroutine is an internal class marked as final that cannot be instantiated without invoking its constructor
