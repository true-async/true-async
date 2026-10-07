--TEST--
After fork(), a coroutine the parent left waiting on a trigger is cancelled in the child (a holder's callback beside it stays), while the child's own wait on the trigger is woken by a thread the child starts; the parent's waiter is woken in the parent
--EXTENSIONS--
pcntl
--FILE--
<?php
use function Async\{spawn, await, delay};
use TrueAsync\Test;

Test\trigger_new();

$left = spawn(function () {
    try {
        Test\trigger_wait();
        echo getmypid() === $GLOBALS['parent'] ? "parent" : "child", ": left waiter woken\n";
    } catch (Async\AsyncCancellation $e) {
        echo "child: left waiter: ", $e->getMessage(), "\n";
    }
});

$parent = getmypid();
$relayed = new Test\Event();
Test\trigger_relay($relayed);
Async\suspend();
$pid = pcntl_fork();

if ($pid === 0) {
    await(spawn(function () {
        Test\trigger_fire(30);
        Test\trigger_wait();
        echo "child: own waiter woken\n";
    }));
    await($left);
    return;
}

pcntl_waitpid($pid, $status);
echo "parent: child exit status ", pcntl_wexitstatus($status), "\n";
Test\trigger_fire(10);
await($left);
?>
--EXPECT--
child: left waiter: The wait was started before fork() and cannot end in the child
child: own waiter woken
parent: child exit status 0
parent: left waiter woken
