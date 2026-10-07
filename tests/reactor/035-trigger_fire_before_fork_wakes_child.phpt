--TEST--
A fire made before fork() and not yet walked wakes the child's waiter: the rebuild raises the child's new wake descriptors once
--EXTENSIONS--
pcntl
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

Test\trigger_new();
Test\trigger_fire();
$pid = pcntl_fork();

if ($pid === 0) {
    /* The wait links before the child's first poll: a walk with no waiter drops the fire. */
    $guard = spawn(function () {
        delay(1000);
        echo "child: not woken\n";
        Test\trigger_fire();
    });

    Test\trigger_wait();
    echo "child: woken by the fire before fork()\n";
    $guard->cancel();

    return;
}

pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";
?>
--EXPECT--
child: woken by the fire before fork()
parent: child exit status 0
