--TEST--
await() - with timeout cancellation
--XFAIL--
Not implemented yet: S5.4 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\delay;
use function Async\timeout;

echo "start\n";

$coroutine = spawn(function() {
    delay(100);
    return "result";
});

$timeout_obj = timeout(1);

try {
    $result = await($coroutine, $timeout_obj);
    echo "awaited result: $result\n";
} catch (Async\OperationCanceledException $e) {
    echo "caught timeout exception\n";
}

echo "end\n";
?>
--EXPECT--
start
caught timeout exception
end