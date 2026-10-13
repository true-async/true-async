--TEST--
TrueAsync\Test\cpu_limits(): an affinity count below 1 is refused
--FILE--
<?php

try {
    TrueAsync\Test\cpu_limits(null, 0);
} catch (ValueError $error) {
    echo $error->getMessage(), "\n";
}
?>
--EXPECT--
TrueAsync\Test\cpu_limits(): Argument #2 ($affinity) must be greater than 0 or null
