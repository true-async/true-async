--TEST--
Scope: disposeAfterTimeout() - basic usage
--FILE--
<?php

use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\suspend;

$started = false;
$scope = new Scope();

// Only the deadline can end this member.
$member = $scope->spawn(function () use (&$started) {
    $started = true;
    try {
        await(new Future(new FutureState()));
    } catch (Async\AsyncCancellation) {
        return "the deadline cancelled the member";
    }
});

// A member the deadline finds unstarted would never run its body.
while (!$started) {
    suspend();
}

$scope->disposeAfterTimeout(20);

echo "Dispose after timeout executed successfully\n";
echo await($member), "\n";

?>
--EXPECT--
Dispose after timeout executed successfully
the deadline cancelled the member