--TEST--
The S3 exception classes: AsyncCancellation and DeadlockError are Errors, CompositeException is final
--FILE--
<?php
foreach ([Async\AsyncCancellation::class, Async\AsyncException::class, Async\DeadlockError::class, Async\CompositeException::class] as $class) {
    echo $class, ' extends ', get_parent_class($class), (new ReflectionClass($class))->isFinal() ? ', final' : '', "\n";
}

try {
    throw new Async\AsyncCancellation('cancelled');
} catch (Exception $e) {
    echo "caught as Exception\n";
} catch (Error $e) {
    echo "caught as Error: ", $e->getMessage(), "\n";
}

// A list read before an add is not changed by the add.
$composite = new Async\CompositeException('composite');
$composite->addException(new LogicException('first'));
$before = $composite->getExceptions();
$composite->addException(new RuntimeException('second'));
var_dump(count($before), count($composite->getExceptions()));
?>
--EXPECT--
Async\AsyncCancellation extends Error
Async\AsyncException extends Exception
Async\DeadlockError extends Error
Async\CompositeException extends Exception, final
caught as Error: cancelled
int(1)
int(2)
