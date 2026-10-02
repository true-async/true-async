--TEST--
S3.7 item 7: main and spawned coroutines awaiting the same coroutine each get its result
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;

$target = spawn(function() {
    echo "target runs\n";
    suspend();
    echo "target returns\n";
    return "shared";
});
$w1 = spawn(function() use ($target) {
    echo "w1 awaits\n";
    return "w1 got " . await($target);
});
$w2 = spawn(function() use ($target) {
    echo "w2 awaits\n";
    return "w2 got " . await($target);
});

// Queue [target, w1, w2]; target's suspend() lets w1 and w2 park on it before it returns.
// The order in which the three waiters continue is not specified, so only main prints after.
echo "main awaits\n";
$result = await($target);
echo "main got $result\n";
echo await($w1), "\n";
echo await($w2), "\n";
?>
--EXPECT--
main awaits
target runs
w1 awaits
w2 awaits
target returns
main got shared
w1 got shared
w2 got shared
