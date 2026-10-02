--TEST--
S3.7 item 6: await() in a spawned coroutine of an unfinished coroutine parks the spawned coroutine
--FILE--
<?php

use function Async\spawn;
use function Async\await;

$target = null;

$waiter = spawn(function() use (&$target) {
    echo "waiter awaits\n";
    $result = await($target);
    echo "waiter got $result\n";
    return $result;
});
$x = spawn(function() {
    echo "x runs\n";
});
$target = spawn(function() {
    echo "target runs\n";
    return 5;
});

// Queue [waiter, x, target]: the waiter parks, x and target run, then the waiter continues.
echo "main awaits waiter\n";
var_dump(await($waiter));
echo "end\n";
?>
--EXPECT--
main awaits waiter
waiter awaits
x runs
target runs
waiter got 5
int(5)
end
