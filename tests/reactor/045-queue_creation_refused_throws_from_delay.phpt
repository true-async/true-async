--TEST--
An IO queue that cannot be created: the delay() that needed it throws, and the next one creates the queue
--FILE--
<?php
use TrueAsync\Test;

Test\fail_io('queue_create');

try {
    Async\delay(1);
    echo "not reached\n";
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}

Async\delay(1);
echo "second delay returned\n";
?>
--EXPECT--
Error: Cannot create the IO queue
second delay returned
