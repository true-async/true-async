--TEST--
On the Poll queue, two coroutines write one pipe at the same time and both writes complete, as two loggers on one php://stderr pipe (review B1, dev/RFC-CHANGES.md 12)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat and sh';
?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
use function Async\spawn;
use function Async\await;

$process = proc_open(['sh', '-c', 'cat > /dev/null'], [0 => ['pipe', 'r']], $pipes);
$large = str_repeat('x', 1024 * 1024);

$first = spawn(fn() => fwrite($pipes[0], $large));
$second = spawn(fn() => fwrite($pipes[0], "line\n"));

echo "first wrote: ", await($first), "\n";
echo "second wrote: ", await($second), "\n";

fclose($pipes[0]);
proc_close($process);
?>
--EXPECT--
first wrote: 1048576
second wrote: 5
