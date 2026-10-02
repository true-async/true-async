--TEST--
Basic suspend functionality - verify suspend yields control properly
--XFAIL--
Not implemented yet: S3.6 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use function Async\suspend;

echo "Before spawn\n";

spawn(function() {
    echo "Coroutine 1 start\n";
    suspend();
    echo "Coroutine 1 after suspend\n";
});

spawn(function() {
    echo "Coroutine 2 start\n";
    echo "Coroutine 2 end\n";
});

echo "After spawn\n";

?>
--EXPECT--
Before spawn
After spawn
Coroutine 1 start
Coroutine 2 start
Coroutine 2 end
Coroutine 1 after suspend