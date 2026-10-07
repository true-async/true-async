--TEST--
Scope: awaitAfterCancellation()'s handler gets each member's error in the waiting coroutine, may suspend there, and the wait goes on
--DESCRIPTION--
The route of each error cancels the scope's members again, as TrueAsync's catch_or_cancel, so the members
here sleep through cancellations to throw later.
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

$scope = Scope::inherit()->asNotSafely();
foreach ([10, 40] as $ms) {
    $scope->spawn(function () use ($ms) {
        sleep_through($ms);
        throw new RuntimeException("error after $ms ms");
    });
}
$scope->spawn(function () {
    sleep_through(80);
    echo "quiet member done\n";
});
Async\suspend();

await(spawn(function () use ($scope) {
    $scope->cancel();
    $scope->awaitAfterCancellation(function (Throwable $e, Scope $s) use ($scope) {
        delay(1);
        echo "handler: ", $e->getMessage(), ", same scope: ", var_export($s === $scope, true), "\n";
    });
    echo "waiter done\n";
}));
echo "end\n";
?>
--EXPECT--
handler: error after 10 ms, same scope: true
handler: error after 40 ms, same scope: true
quiet member done
waiter done
end
