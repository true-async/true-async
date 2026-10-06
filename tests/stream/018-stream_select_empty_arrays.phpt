--TEST--
stream_select with empty arrays
--XFAIL--
By design: the core keeps PHP's argument errors of stream_select() (a TypeError per non-stream, then a ValueError when no stream is left), where TrueAsync's fork, in a coroutine, throws no ValueError and returns 0 for no stream at all (dev/plans/S6.md section 11)
--FILE--
<?php

require_once __DIR__ . '/stream_helper.php';
use function Async\spawn;
use function Async\await;

echo "Testing stream_select with empty arrays\n";

$coroutine = spawn(function() {
    $read = $write = $except = [];
    $result = stream_select($read, $write, $except, 1);
    echo "Result: $result\n";
    
    return "empty arrays test completed";
});

$result = await($coroutine);
echo "Result: $result\n";

?>
--EXPECT--
Testing stream_select with empty arrays
Result: 0
Result: empty arrays test completed