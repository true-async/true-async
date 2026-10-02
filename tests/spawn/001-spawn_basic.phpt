--TEST--
Future: spawn() - basic usage
--XFAIL--
Not implemented yet: S3.5 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;

echo "start\n";

spawn(function() {
    echo "coroutine\n";
});

echo "end\n";
?>
--EXPECT--
start
end
coroutine