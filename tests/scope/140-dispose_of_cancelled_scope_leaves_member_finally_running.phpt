--TEST--
Scope: dispose() of a cancelled scope leaves its finished member's waiting finally handler to finish
--FILE--
<?php

use function Async\delay;

$scope = new Async\Scope();
$started = false;
$in_finally = false;
$ended = false;
$scope->spawn(function () use (&$started, &$in_finally) {
    Async\current_coroutine()->finally(function () use (&$in_finally) {
        $in_finally = true;
        delay(10);
        echo "finally ends\n";
        $GLOBALS['ended'] = true;
    });
    $started = true;
    delay(1000);
});

while (!$started) {
    Async\suspend();
}

$scope->cancel();

while (!$in_finally) {
    Async\suspend();
}

$scope->dispose();

// A longer delay could end first: a timer due when main parks wakes it in place, ahead of the queue
while (!$ended) {
    delay(1);
}

echo "end\n";
?>
--EXPECT--
finally ends
end
