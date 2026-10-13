--TEST--
The private constructor of a Timeout refuses a call through reflection
--FILE--
<?php
try {
    (new ReflectionMethod(Async\Timeout::class, '__construct'))->invoke(Async\timeout(10));
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}
?>
--EXPECT--
Error: Timeout cannot be constructed directly
