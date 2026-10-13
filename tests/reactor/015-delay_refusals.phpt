--TEST--
delay(): a negative delay throws ValueError
--FILE--
<?php
use function Async\delay;

try {
    delay(-1);
} catch (ValueError $e) {
    echo get_class($e), ": ", $e->getMessage(), "\n";
}
?>
--EXPECT--
ValueError: Async\delay(): Argument #1 ($ms) must be greater than or equal to 0
