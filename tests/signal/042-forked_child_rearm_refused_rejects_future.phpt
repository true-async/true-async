--TEST--
Async\signal() across pcntl_fork(): a child whose IO queue refuses the signal wait it arms again rejects the Future it inherited with the refusal
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
$pid = pcntl_fork();

if ($pid === 0) {
    Test\fail_io('submit');
    // The child's first reactor call rebuilds the queue and arms the watch again.
    Async\delay(1);

    try {
        Async\await($future);
        echo "child: not reached\n";
    } catch (Error $error) {
        echo "child: ", get_class($error), ": ", $error->getMessage(), "\n";
    }

    exit(0);
}

pcntl_waitpid($pid, $status);
echo "parent: child exited with ", pcntl_wexitstatus($status), "\n";
posix_kill(getmypid(), SIGUSR1);
var_dump(Async\await($future));
?>
--EXPECTF--
child: Error: Cannot submit an IO operation: %s
parent: child exited with 0
enum(Async\Signal::SIGUSR1)
