--TEST--
Scope: closing a scope withdraws its disposeAfterTimeout() timer, so a script that ends does not wait for it; awaitAfterCancellation() on a cancelled scope, closed since, still waits for its zombie
--EXTENSIONS--
pcntl
--FILE--
<?php
use Async\Scope;
use function Async\spawn;
use function Async\await;
use function Async\delay;

$start = hrtime(true);
$pid = pcntl_fork();

if ($pid === 0) {
    $idle = Scope::inherit();
    $idle->spawn(fn() => 1);
    $idle->disposeAfterTimeout(30000);
    Async\suspend();
    $idle->dispose();
    printf("child: idle scope closed %d\n", $idle->isClosed());

    return;
}

pcntl_waitpid($pid, $status);
printf("parent: the child ended well before the timer: %d\n", (hrtime(true) - $start) < 10000000000);

$scope = Scope::inherit()->allowZombies();
$scope->spawn(function () {
    try {
        delay(1000);
    } catch (Async\AsyncCancellation) {
    }
    delay(50);
    echo "zombie done\n";
});
Async\suspend();
$scope->cancel();
$scope->dispose();
printf("scope closed %d\n", $scope->isClosed());

await(spawn(function () use ($scope) {
    $scope->awaitAfterCancellation();
    echo "waiter returned\n";
}));
?>
--EXPECT--
child: idle scope closed 1
parent: the child ended well before the timer: 1
scope closed 1
zombie done
waiter returned
