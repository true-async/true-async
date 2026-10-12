--TEST--
On the Poll queue, proc_close() of a process whose stdout another coroutine is parked reading closes the pipe and the parked read returns false (review B1, dev/RFC-CHANGES.md 12)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat';
?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
use function Async\spawn;
use function Async\await;

$process = proc_open(['cat'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

/* The reader runs first and parks: cat writes nothing until its stdin closes. */
$reader = spawn(fn() => fread($pipes[1], 10));

/* proc_close() closes cat's stdin and the read pipe in one pass, then waits for cat. */
$closer = spawn(fn() => proc_close($process));

echo "proc_close: ";
var_dump(await($closer));
echo "read: ";
var_dump(await($reader));
?>
--EXPECT--
proc_close: int(0)
read: bool(false)
