--TEST--
await() with a timeout() whose timer cannot be armed for want of an IO queue throws the failure and does not wait
--FILE--
<?php
use TrueAsync\Test;

$sleeper = Async\spawn(function () {
    Async\suspend();

    return "sleeper done";
});

Test\fail_io('queue_create');

try {
    Async\await($sleeper, Async\timeout(10));
    echo "not reached\n";
} catch (Error $error) {
    echo get_class($error), ": ", $error->getMessage(), "\n";
}

var_dump(Async\await($sleeper));
?>
--EXPECT--
Error: Cannot create the IO queue
string(12) "sleeper done"
