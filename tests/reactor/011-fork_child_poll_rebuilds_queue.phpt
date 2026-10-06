--TEST--
A child after fork() that submits nothing gets EPERM from the parent's queue at the scheduler loop's first poll and rebuilds: the parent's waiter is named in the child's deadlock report
--EXTENSIONS--
pcntl
--INI--
true_async.debug_deadlock=1
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use TrueAsync\Test;

$left = spawn(fn() => Test\reactor_wait(60000));
Async\suspend();
$pid = pcntl_fork();

if ($pid === 0) {
    try {
        await($left);
    } catch (Async\AsyncCancellation $e) {
        echo "child: ", $e->getMessage(), ", waits: ", Test\reactor_state()['waits'], "\n";
    }

    return;
}

pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";
$left->cancel();
?>
--EXPECTF--
=== DEADLOCK REPORT START ===
Coroutines waiting: 2

%A
=== DEADLOCK REPORT END   ===

child: Deadlock detected, waits: 0

Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 2 coroutines in waiting in %s
Stack trace:
#0 {main}
  thrown in %s
parent: child exit status 255
