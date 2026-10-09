--TEST--
Scope: a second disposeAfterTimeout() stops a member's finally handler that caught the first one's cancellation and waited again
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$in_finally = false;
$caught_first = false;
$scope->spawn(function () use (&$in_finally, &$caught_first) {
    Async\current_coroutine()->finally(function () use (&$in_finally, &$caught_first) {
        $in_finally = true;

        try {
            delay(100000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "first caught\n";
            $caught_first = true;
        }

        try {
            delay(100000);
        } catch (Async\AsyncCancellation $cancellation) {
            echo "second stops it\n";
        }
    });
});

while (!$in_finally) {
    suspend();
}

$scope->disposeAfterTimeout(10);

while (!$caught_first) {
    delay(5);
}

$scope->disposeAfterTimeout(10);
delay(50);
echo "end\n";
?>
--EXPECT--
first caught
second stops it
end
