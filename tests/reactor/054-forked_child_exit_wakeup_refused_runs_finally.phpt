--TEST--
exit() in a forked child's main script whose rebuild cannot create the IO queue for the wakeup: the rebuild throws nothing with no frame, and the child's coroutines are unwound with their finally blocks
--EXTENSIONS--
pcntl
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use TrueAsync\Test;

Test\trigger_new();
$channel = new Async\Channel(0);
$parent = getmypid();

// Started before the fault: a coroutine the shutdown finds unstarted never runs its finally.
$started = new Async\FutureState();
$receiver = Async\spawn(function () use ($channel, $parent, $started) {
    $started->complete(null);

    try {
        $channel->recv();
        echo "not reached\n";
    } catch (Async\AsyncCancellation $cancellation) {
        echo (getmypid() === $parent ? "parent" : "child"), " receiver: ", $cancellation->getMessage(), "\n";
    } finally {
        echo (getmypid() === $parent ? "parent" : "child"), " receiver: finally\n";
    }
});

(new Async\Future($started))->await();
$pid = pcntl_fork();

if ($pid === 0) {
    Test\fail_io('queue_create');
    exit("child: exit\n");
}

pcntl_waitpid($pid, $status);
echo "parent: child exited with ", pcntl_wexitstatus($status), "\n";
$receiver->cancel();
?>
--EXPECT--
child: exit
child receiver: Graceful shutdown
child receiver: finally
parent: child exited with 0
parent receiver: Coroutine cancelled
parent receiver: finally
