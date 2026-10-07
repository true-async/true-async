--TEST--
Async\signal(): the last Future of a watch closes its signal source, and a forked child's watch closes the one it inherited
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') echo "skip Linux-only test (/proc)";
?>
--FILE--
<?php
use Async\Signal;
use function Async\await;
use function Async\delay;
use function Async\signal;
use function Async\spawn;

function sources(): int
{
    $count = 0;

    foreach (scandir('/proc/self/fd') as $fd) {
        $count += @readlink("/proc/self/fd/$fd") === 'anon_inode:[signalfd]' ? 1 : 0;
    }

    return $count;
}

$future = signal(Signal::SIGUSR1);
$future->ignore();
delay(1);
$single = sources();
unset($future);
delay(1);
echo "after the last Future: ", sources(), "\n";

$first = signal(Signal::SIGUSR1);
$second = signal(Signal::SIGUSR2);
$second->ignore();
delay(1);
echo "two watches: ", sources() === 2 * $single ? "twice one" : sources(), "\n";
$pid = pcntl_fork();

if ($pid === 0) {
    spawn(fn() => posix_kill(getmypid(), SIGUSR1));
    await($first);
    delay(1);
    echo "child after one delivery: ", sources() === $single ? "one watch" : sources(), "\n";
    exit(0);
}

pcntl_waitpid($pid, $status);
spawn(fn() => posix_kill(getmypid(), SIGUSR1));
await($first);
unset($first, $second);
delay(1);
echo "parent: ", sources(), "\n";
?>
--EXPECT--
after the last Future: 0
two watches: twice one
child after one delivery: one watch
parent: 0
