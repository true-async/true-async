--TEST--
Future: spawn() - multiple coroutines execution order
--XFAIL--
Not implemented yet: S3.5 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;

echo "start\n";

spawn(function() {
    echo "coroutine 1\n";
});

spawn(function() {
    echo "coroutine 2\n";
});

spawn(function() {
    echo "coroutine 3\n";
});

echo "end\n";
?>
--EXPECT--
start
end
coroutine 1
coroutine 2
coroutine 3