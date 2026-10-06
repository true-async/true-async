--TEST--
A child after fork() submits to a new Poll queue: its own wait works, though the parent's Poll queue takes a Timer op in the child, and a waiter the parent left is named in its deadlock report
--EXTENSIONS--
pcntl
--INI--
true_async.debug_deadlock=1
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;

Test\reactor_use_poll_queue();

$left = spawn(fn() => Test\reactor_wait(60000));
Test\reactor_wait(10);
$pid = pcntl_fork();

if ($pid === 0) {
    echo "child, waits: ", Test\reactor_state()['waits'], "\n";
    Test\reactor_wait(10);
    echo "child woken, waits: ", Test\reactor_state()['waits'], "\n";
    return;
}

pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";
$left->cancel();
?>
--EXPECTF--
child, waits: 1
child woken, waits: 0

=== DEADLOCK REPORT START ===
Coroutines waiting: 1

Coroutine %d spawned at %s:%d, suspended at %s:%d
  waiting for:
    - reactor wait: timer

=== DEADLOCK REPORT END   ===


Fatal error: Uncaught Async\DeadlockError: Deadlock detected: no active coroutines, 1 coroutines in waiting in %s
Stack trace:
#0 {main}
  thrown in %s
parent: child exit status 255
