--TEST--
Wake descriptors that cannot be made: the trigger creation that needed them throws, and the next one makes them
--FILE--
<?php
use TrueAsync\Test;

Test\fail_io('wake_pair');

try {
    Test\trigger_new();
    echo "not reached\n";
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}

Test\trigger_new();
echo "second trigger made\n";
?>
--EXPECTF--
Error: Cannot create the wake descriptors: %s
second trigger made
