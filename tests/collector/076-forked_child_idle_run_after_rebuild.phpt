--TEST--
A forked child whose first reactor entry is the idle point rebuilds before the collector's run reads the parent's waits: the parent's trigger waiter is cancelled and runs, and the child's main is then cancelled by the deadlock
--EXTENSIONS--
pcntl
--INI--
true_async.partial_deadlock_interval=0
error_reporting=E_ALL & ~E_WARNING
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\{spawn, await};
use TrueAsync\Test;

Test\trigger_new();
$left = spawn(function () {
    try {
        Test\trigger_wait();
        echo "parent: left waiter woken\n";
    } catch (Async\AsyncCancellation $e) {
        echo "child: left waiter: ", $e->getMessage(), "\n";
    }
});
while ($left->getAwaitingInfo() === []) {
    Async\suspend();
}
$pid = pcntl_fork();

if ($pid === 0) {
    try {
        await(new Future(new FutureState()));
    } catch (Throwable $e) {
        echo "child: main: ", get_class($e), "\n";
    }
    exit(0);
}

pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";
Test\trigger_fire(10);
await($left);
?>
--EXPECTF--
child: left waiter: The wait was started before fork() and cannot end in the child

=== DEADLOCK REPORT START ===
Coroutines waiting: 1

Coroutine 1 spawned at :0, suspended at %s076-forked_child_idle_run_after_rebuild.php:23
  waiting for:
    - await: future

=== DEADLOCK REPORT END   ===

child: main: Async\AsyncCancellation

Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting in [no active file]:0
Stack trace:
#0 {main}
  thrown in [no active file] on line 0
parent: child exit status 255
parent: left waiter woken
