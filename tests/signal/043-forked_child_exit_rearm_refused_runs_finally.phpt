--TEST--
Async\signal() across pcntl_fork(): exit() in the child's main script whose rebuild cannot arm the signal wait again: the rebuild throws nothing with no frame, and the child's coroutines are unwound with their finally blocks
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use TrueAsync\Test;

$future = Async\signal(Async\Signal::SIGUSR1);
$parent = getmypid();

// Started before the fault: a coroutine the shutdown finds unstarted never runs its finally.
$started = new Async\FutureState();
$waiter = Async\spawn(function () use ($future, $parent, $started) {
    $started->complete(null);

    try {
        var_dump(Async\await($future));
    } catch (Throwable $throwable) {
        echo (getmypid() === $parent ? "parent" : "child"), " waiter: ", get_class($throwable), ": ", $throwable->getMessage(), "\n";
    } finally {
        echo (getmypid() === $parent ? "parent" : "child"), " waiter: finally\n";
    }
});

(new Async\Future($started))->await();
$pid = pcntl_fork();

if ($pid === 0) {
    Test\fail_io('submit');
    exit("child: exit\n");
}

pcntl_waitpid($pid, $status);
echo "parent: child exited with ", pcntl_wexitstatus($status), "\n";
posix_kill(getmypid(), SIGUSR1);
Async\await($waiter);
?>
--EXPECT--
child: exit
child waiter: Async\AsyncCancellation: Graceful shutdown
child waiter: finally
parent: child exited with 0
enum(Async\Signal::SIGUSR1)
parent waiter: finally
