--TEST--
S3.7 item 6: await() in main of an unfinished coroutine parks main while the queued coroutines run
--FILE--
<?php

use function Async\spawn;
use function Async\await;
use function Async\suspend;

$x = spawn(function() {
    echo "x runs\n";
});
$target = spawn(function() {
    echo "target starts\n";
    suspend();
    echo "target ends\n";
    return "target result";
});
$y = spawn(function() {
    echo "y runs\n";
});

// Queue [x, target, y]; target's suspend() puts it behind y.
echo "main awaits\n";
var_dump(await($target));
echo "main continues\n";
?>
--EXPECT--
main awaits
x runs
target starts
y runs
target ends
string(13) "target result"
main continues
