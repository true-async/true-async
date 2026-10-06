--TEST--
stream_select with invalid stream types
--XFAIL--
By design: the core keeps PHP's argument errors of stream_select() (a TypeError per non-stream, then a ValueError when no stream is left), where TrueAsync's fork, in a coroutine, throws no ValueError and returns 0 for no stream at all (dev/plans/S6.md section 11)
--FILE--
<?php

require_once __DIR__ . '/stream_helper.php';
use function Async\spawn;
use function Async\await;

echo "Testing stream_select with invalid streams\n";

$coroutine = spawn(function() {
    $invalid = ["not a stream", 123, null];
    $write = $except = null;
    
    try {
        $result = stream_select($invalid, $write, $except, 1);
        echo "Result: $result\n";
    } catch (TypeError $e) {
        echo "Exception: " . get_class($e) . "\n";
    }
    
    return "invalid streams test completed";
});

$result = await($coroutine);
echo "Result: $result\n";

?>
--EXPECTF--
Testing stream_select with invalid streams
%a
Result: invalid streams test completed