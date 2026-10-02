--TEST--
Future: spawn() - AsyncCancellation handling (special case)
--XFAIL--
Not implemented yet: S3.8 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use Async\AsyncCancellation;

echo "start\n";

spawn(function() {
    echo "coroutine start\n";
    throw new AsyncCancellation("Cancelled");
    echo "coroutine end (should not print)\n";
});

echo "end\n";
?>
--EXPECT--
start
end
coroutine start