--TEST--
Scope: an error that comes while awaitAfterCancellation()'s handler runs goes up the scopes as if nobody waited: the parent's child scope handler gets it, and the wait goes on
--DESCRIPTION--
The waiter's wait is linked again only after its handler returns (S9-scope.md 9, item 17).
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
$went_up = new FutureState();
$parent = Scope::inherit();
$parent->setChildScopeExceptionHandler(function (Scope $s, Async\Coroutine $c, Throwable $e) use ($went_up) {
    echo "parent's handler: ", $e->getMessage(), "\n";
    $went_up->complete(null);
});
$scope = Scope::inherit($parent)->asNotSafely();
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

await(spawn(function () use ($scope, $turns, $went_up) {
    $scope->cancel();
    $turns[0]->complete(null);
    $scope->awaitAfterCancellation(function (Throwable $e) use ($turns, $went_up) {
        // The second member's error comes while this handler waits.
        $turns[1]->complete(null);
        await(new Future($went_up));
        echo "waiter handler: ", $e->getMessage(), "\n";
        $turns[2]->complete(null);
    });
    echo "waiter done\n";
}));
echo "end\n";
?>
--EXPECT--
parent's handler: error of the second member
waiter handler: error of the first member
quiet member done
waiter done
end
