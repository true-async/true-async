--TEST--
A Timeout never cancelled is collected in a garbage cycle, and its private constructor refuses a call through reflection
--FILE--
<?php
$cycle = new stdClass();
$cycle->self = $cycle;
$cycle->timeout = Async\timeout(1000);
unset($cycle);
var_dump(gc_collect_cycles());

try {
    (new ReflectionMethod(Async\Timeout::class, '__construct'))->invoke(Async\timeout(10));
} catch (Throwable $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}
?>
--EXPECT--
int(2)
Error: Timeout cannot be constructed directly
