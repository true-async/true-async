--TEST--
Scope: cancel() after a member's fast finally handler ended leaves its slow one, still waiting, to finish
--FILE--
<?php

use function Async\delay;
use function Async\suspend;

$scope = new Async\Scope();
$fast_ended = false;
$go = false;
$scope->spawn(function () use (&$fast_ended, &$go) {
    $self = Async\current_coroutine();
    $self->finally(function () use (&$go) {
        while (!$go) {
            suspend();
        }

        echo "slow ends\n";
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
delay(10);
echo "end\n";
?>
--EXPECT--
fast ends
slow ends
end
