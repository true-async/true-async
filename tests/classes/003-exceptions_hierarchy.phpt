--TEST--
The S3 exception classes: AsyncCancellation and DeadlockError are Errors, CompositeException is final
--FILE--
<?php
foreach ([Async\AsyncCancellation::class, Async\AsyncException::class, Async\DeadlockError::class, Async\CompositeException::class] as $class) {
    echo $class, ' extends ', get_parent_class($class), (new ReflectionClass($class))->isFinal() ? ', final' : '', "\n";
}
?>
--EXPECT--
Async\AsyncCancellation extends Error
Async\AsyncException extends Exception
Async\DeadlockError extends Error
Async\CompositeException extends Exception, final
