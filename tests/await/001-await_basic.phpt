--TEST--
await() - basic usage with coroutine
--XFAIL--
Not implemented yet: S3.7 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use function Async\await;

echo "start\n";

$coroutine = spawn(function() {
    echo "coroutine running\n";
    return "result";
});

$result = await($coroutine);
echo "awaited result: $result\n";

echo "end\n";
?>
--EXPECT--
start
coroutine running
awaited result: result
end