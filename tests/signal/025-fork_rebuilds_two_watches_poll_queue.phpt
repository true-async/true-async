--TEST--
Async\signal() on the Poll queue: a child forked while the parent waits for two signals makes both watches again
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

// On Linux an unrenewed watch still works in the child through the inherited signalfd; the renewal
// this guards is a kqueue's, which a child does not inherit (kqueue(2)).
TrueAsync\Test\reactor_use_poll_queue();

$first = signal(Signal::SIGUSR1);
$second = signal(Signal::SIGUSR2);
$sleeper = spawn(fn() => delay(30));
delay(1);

$pid = pcntl_fork();

if ($pid === 0) {
    spawn(function () {
        delay(10);
        posix_kill(getmypid(), SIGUSR2);
        delay(10);
        posix_kill(getmypid(), SIGUSR1);
    });
    echo "child: ", await($second)->name, " ", await($first)->name, "\n";
    exit(0);
}

pcntl_waitpid($pid, $status);
echo "child exited with ", pcntl_wexitstatus($status), "\n";

spawn(function () {
    posix_kill(getmypid(), SIGUSR1);
    posix_kill(getmypid(), SIGUSR2);
});
echo "parent: ", await($first)->name, " ", await($second)->name, "\n";
await($sleeper);
echo "end\n";
?>
--EXPECT--
child: SIGUSR2 SIGUSR1
child exited with 0
parent: SIGUSR1 SIGUSR2
end
