--TEST--
Async\signal(): a child forked while the parent waits for a signal and in delay() rebuilds its queue and its watches
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use Async\Signal;
use function Async\await;
use function Async\delay;
use function Async\signal;
use function Async\spawn;

$parent = signal(Signal::SIGUSR1);
$sleeper = spawn(fn() => delay(30));
delay(1);

$pid = pcntl_fork();

if ($pid === 0) {
    $own = signal(Signal::SIGUSR2);
    spawn(function () {
        delay(10);
        posix_kill(getmypid(), SIGUSR1);
        posix_kill(getmypid(), SIGUSR2);
    });
    echo "child: ", await($parent)->name, " ", await($own)->name, "\n";
    exit(0);
}

pcntl_waitpid($pid, $status);
echo "child exited with ", pcntl_wexitstatus($status), "\n";

spawn(fn() => posix_kill(getmypid(), SIGUSR1));
echo "parent: ", await($parent)->name, "\n";
await($sleeper);
echo "end\n";
?>
--EXPECT--
child: SIGUSR1 SIGUSR2
child exited with 0
parent: SIGUSR1
end
