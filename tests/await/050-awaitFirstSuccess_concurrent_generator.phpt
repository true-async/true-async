--TEST--
await_first_success() - With concurrent generator using suspend() in body
--XFAIL--
Not implemented yet: S5.3 of dev/PLAN.md
--FILE--
<?php

use function Async\spawn;
use function Async\await_first_success;
use function Async\suspend;

function concurrentGenerator($functions) {
    foreach ($functions as $func) {
        // Suspend before yielding each coroutine
        suspend();
        yield spawn($func);
    }
}

echo "start\n";

$functions = [
    function() { suspend(); throw new RuntimeException("error"); },
    function() { return "success"; },
    function() { return "another success"; },
];

$generator = concurrentGenerator($functions);

$result = await_first_success($generator);

echo "Result: {$result[0]}\n";
echo "end\n";

?>
--EXPECT--
start
Result: success
end