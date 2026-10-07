--TEST--
The core's coroutine count leaves zombies out; a coroutine that never started is cancelled even safely
--FILE--
<?php
use Async\Scope;
use function Async\await;
use function Async\delay;
use function Async\suspend;
use TrueAsync\Test;

$scope = Scope::inherit();
$first = $scope->spawn(function () {
    delay(20);
    echo "first finished\n";
});
$second = $scope->spawn(function () {
    delay(20);
    echo "second finished\n";
});
suspend();
$queued = $scope->spawn(fn() => print("queued ran\n"));

echo "before cancel: ", Test\coroutine_count(), "\n";
$scope->cancel();
echo "after cancel: ", Test\coroutine_count(), "\n";
var_dump($first->isCancellationRequested(), $scope->isCancelled(), $scope->isFinished());

suspend();
echo "queued cancelled: ", var_export($queued->isCancelled(), true), "\n";
echo "after the queued one: ", Test\coroutine_count(), "\n";

await($first);
await($second);
echo "at the end: ", Test\coroutine_count(), "\n";
var_dump($first->isCancelled(), $scope->isClosed());

?>
--EXPECT--
before cancel: 4
after cancel: 2
bool(true)
bool(true)
bool(true)
queued cancelled: true
after the queued one: 1
first finished
second finished
at the end: 1
bool(true)
bool(true)
