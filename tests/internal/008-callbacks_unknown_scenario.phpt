--TEST--
Test hooks: an unknown scenario name is a ValueError
--FILE--
<?php
try {
    TrueAsync\Test\callbacks_scenario('none');
} catch (ValueError $e) {
    echo $e->getMessage(), "\n";
}
?>
--EXPECT--
TrueAsync\Test\callbacks_scenario(): Argument #1 ($name) is not a known scenario
