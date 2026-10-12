--TEST--
Scope: cancel() after a member's fast finally handler ended leaves its slow one, still waiting, to finish
--FILE--
<?php

use function Async\suspend;

$scope = new Async\Scope();
$fast_ended = false;
$go = false;
$ended = false;
$scope->spawn(function () use (&$fast_ended, &$go) {
    $self = Async\current_coroutine();
    $self->finally(function () use (&$go) {
        while (!$go) {
            suspend();
        }

        echo "slow ends\n";
        $GLOBALS['ended'] = true;
    });
    $self->finally(function () use (&$fast_ended) {
        $fast_ended = true;
        echo "fast ends\n";
    });
});

while (!$fast_ended) {
    suspend();
}

$scope->cancel();
$go = true;

// A delay could end before the finally handler has run on: a timer due when main parks wakes it in place
while (!$ended) {
    suspend();
}

echo "end\n";
?>
--EXPECT--
fast ends
slow ends
end
