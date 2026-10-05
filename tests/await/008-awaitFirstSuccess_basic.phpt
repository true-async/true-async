--TEST--
await_first_success() - basic usage with mixed success and error
--XFAIL--
Not implemented yet: S5.3 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use function Async\await_first_success;
use function Async\suspend;

echo "start\n";

$coroutines = [
    spawn(function() {
        //suspend();
        throw new RuntimeException("first error");
    }),
    spawn(function() {
        return "success";
    }),
    spawn(function() {
        suspend();
        return "another success";
    }),
];

$result = await_first_success($coroutines);
echo "Result: {$result[0]}\n";
echo "end\n";
?>
--EXPECTF--
start
Result: success
end