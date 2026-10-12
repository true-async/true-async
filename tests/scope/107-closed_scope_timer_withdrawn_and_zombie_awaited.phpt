--TEST--
Scope: closing a scope withdraws its disposeAfterTimeout() timer, so a script that ends does not wait for it; awaitAfterCancellation() on a cancelled scope, closed since, still waits for its zombie
--EXTENSIONS--
pcntl
--FILE--
<?php
use Async\Scope;
use TrueAsync\Test;
use function Async\spawn;
use function Async\await;
use function Async\delay;

$pid = pcntl_fork();

if ($pid === 0) {
    $idle = Scope::inherit();
    $idle->spawn(fn() => 1);
    $idle->disposeAfterTimeout(30000);
    Async\suspend();
    $idle->dispose();
    printf("child: idle scope closed %d, timers armed %d\n", $idle->isClosed(), Test\reactor_state()['timers']);

    return;
}

pcntl_waitpid($pid, $status);
printf("parent: the child exited with %d\n", pcntl_wexitstatus($status));

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
child: idle scope closed 1, timers armed 0
parent: the child exited with 0
scope closed 1
zombie done
waiter returned
