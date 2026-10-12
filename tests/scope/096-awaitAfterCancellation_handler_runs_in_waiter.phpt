--TEST--
Scope: awaitAfterCancellation()'s handler gets each member's error in the waiting coroutine, may suspend there, and the wait goes on
--DESCRIPTION--
The route of each error cancels the scope's members again, as TrueAsync's catch_or_cancel, so the members
here wait through cancellations for their turn. The handler gives the next member its turn.
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\suspend;

$started = 0;

function wait_through_cancellations(FutureState $turn): void
{
    global $started;

    $started++;

    while (true) {
        try {
            await(new Future($turn));

            return;
        } catch (Async\AsyncCancellation $e) {
        }
    }
}

$turns = [new FutureState(), new FutureState(), new FutureState()];
$scope = Scope::inherit()->asNotSafely();
foreach (['first', 'second'] as $index => $name) {
    $scope->spawn(function () use ($turns, $index, $name) {
        wait_through_cancellations($turns[$index]);
        throw new RuntimeException("error of the $name member");
    });
}
$scope->spawn(function () use ($turns) {
    wait_through_cancellations($turns[2]);
    echo "quiet member done\n";
});

// A member the cancel finds unstarted would never run its body.
while ($started < 3) {
    suspend();
}

await(spawn(function () use ($scope, $turns) {
    $scope->cancel();
    $turns[0]->complete(null);
    $next = 1;
    $scope->awaitAfterCancellation(function (Throwable $e, Scope $s) use ($scope, $turns, &$next) {
        suspend();
        echo "handler: ", $e->getMessage(), ", same scope: ", var_export($s === $scope, true), "\n";
        $turns[$next++]->complete(null);
    });
    echo "waiter done\n";
}));
echo "end\n";
?>
--EXPECT--
handler: error of the first member, same scope: true
handler: error of the second member, same scope: true
quiet member done
waiter done
end
