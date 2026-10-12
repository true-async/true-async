--TEST--
A wakeup poll the IO queue refuses: the trigger creation that armed it throws, and the next one arms it
--FILE--
<?php
use TrueAsync\Test;

Test\fail_io('submit');

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
Error: Cannot submit an IO operation: %s
second trigger made
