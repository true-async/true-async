--TEST--
Scope: the deadlock report names a wait in awaitCompletion() by where the scope was made
--FILE--
<?php

use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\await;

$scope = new Scope();
$scope->spawn(function () {
    await(new Future(new FutureState()));
});

$scope->awaitCompletion(new Future(new FutureState()));
echo "not reached\n";

?>
--EXPECTF--

=== DEADLOCK REPORT START ===
Coroutines waiting: 2

Coroutine %d spawned at :0, suspended at %s:%d
  waiting for:
    - await: scope created at %s:%d
    - cancellation: future

Coroutine %d spawned at %s:%d, suspended at %s:%d
  waiting for:
    - await: future

=== DEADLOCK REPORT END   ===


Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 2 coroutines in waiting in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
