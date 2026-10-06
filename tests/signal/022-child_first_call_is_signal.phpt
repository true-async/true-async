--TEST--
Async\signal(): a child whose first async call is Async\signal() rebuilds the reactor the parent left before it watches
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

delay(1);

$pid = pcntl_fork();

if ($pid === 0) {
    $future = signal(Signal::SIGUSR1);
    spawn(fn() => posix_kill(getmypid(), SIGUSR1));
    echo "child: ", await($future)->name, "\n";
    exit(0);
}

pcntl_waitpid($pid, $status);
echo "child exited with ", pcntl_wexitstatus($status), "\n";
?>
--EXPECT--
child: SIGUSR1
child exited with 0
