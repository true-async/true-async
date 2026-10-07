--TEST--
Collector oracle: a fatal error ends the members and the waiter in awaitCompletion() that a run found, and the members' end does not abort the oracle
--INI--
true_async.partial_deadlock_interval=0
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;
use function Async\spawn;
use function Async\delay;

$scope = new Scope();
$scope->spawn(function () {
    await(new Future(new FutureState()));
});
spawn(function () use ($scope) {
    $scope->awaitCompletion(new Future(new FutureState()));
    echo "waiter: completed\n";
});
unset($scope);
delay(20);
eval("function f() {} function f() {}");
?>
--EXPECTF--

Warning: Partial deadlock: coroutine #%d spawned at %s:10 can never wake (await: future) in %s on line 11

Warning: Partial deadlock: coroutine #%d spawned at %s:13 can never wake (await: scope created at %s:9) in %s on line 14

Fatal error: Cannot redeclare function f() (previously declared in %s(19) : eval()'d code:1) in %s(19) : eval()'d code on line 1
