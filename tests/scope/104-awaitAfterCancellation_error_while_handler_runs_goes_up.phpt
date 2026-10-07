--TEST--
Scope: an error that comes while awaitAfterCancellation()'s handler runs goes up the scopes as if nobody waited: the parent's child scope handler gets it, and the wait goes on
--DESCRIPTION--
The waiter's wait is linked again only after its handler returns (S9-scope.md 9, item 17).
--FILE--
<?php
use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\delay;

function sleep_through(int $ms): void
{
    $end = hrtime(true) + $ms * 1000000;

    while (($left = $end - hrtime(true)) > 0) {
        try {
            delay(max(1, intdiv($left, 1000000)));
        } catch (Async\AsyncCancellation $e) {
        }
    }
}

$parent = Scope::inherit();
$parent->setChildScopeExceptionHandler(function (Scope $s, Async\Coroutine $c, Throwable $e) {
    echo "parent's handler: ", $e->getMessage(), "\n";
});
$scope = Scope::inherit($parent)->asNotSafely();
foreach ([10, 30] as $ms) {
    $scope->spawn(function () use ($ms) {
        sleep_through($ms);
        throw new RuntimeException("error after $ms ms");
    });
}
$scope->spawn(function () {
    sleep_through(100);
    echo "quiet member done\n";
});
Async\suspend();

await(spawn(function () use ($scope) {
    $scope->cancel();
    $scope->awaitAfterCancellation(function (Throwable $e) {
        delay(50);
        echo "waiter handler: ", $e->getMessage(), "\n";
    });
    echo "waiter done\n";
}));
echo "end\n";
?>
--EXPECT--
parent's handler: error after 30 ms
waiter handler: error after 10 ms
quiet member done
waiter done
end
