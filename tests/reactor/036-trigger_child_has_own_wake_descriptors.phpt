--TEST--
A forked child polls wake descriptors of its own: a parent that waits on its own trigger meanwhile does not drain the fire a thread of the child made
--EXTENSIONS--
pcntl
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

// The Poll queue sees readiness only when it waits; a Ring's armed poll would complete in the child
// even after the parent drained a shared descriptor.
Test\reactor_use_poll_queue();
Test\trigger_new();
$pid = pcntl_fork();

if ($pid === 0) {
    $waiter = spawn(function () {
        Test\trigger_wait();
        echo "child: woken\n";
    });

    Async\suspend();
    Test\trigger_fire(50);
    $until = hrtime(true) + 300 * 1000000;

    // Busy, so the child polls only after the parent had its chance at the descriptor.
    while (hrtime(true) < $until) {
    }

    spawn(function () use ($waiter) {
        delay(1000);

        if (!$waiter->isCompleted()) {
            echo "child: not woken\n";
            $waiter->cancel();
        }
    });

    return;
}

$parent_waiter = spawn(function () {
    try {
        Test\trigger_wait();
        echo "parent: woken\n";
    } catch (Async\AsyncCancellation $e) {
        echo "parent: cancelled\n";
    }
});

delay(600);
$parent_waiter->cancel();
await($parent_waiter);
pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";
?>
--EXPECT--
child: woken
parent: cancelled
parent: child exit status 0
