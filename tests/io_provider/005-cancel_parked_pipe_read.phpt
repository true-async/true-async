--TEST--
A coroutine cancelled while parked in a pipe read gets the cancellation, and the pipe stays usable
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat, sh and Unix sockets';
?>
--FILE--
<?php
use function Async\spawn;

$process = proc_open(['cat'], [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);

$reader = spawn(function () use ($pipes) {
    try {
        fread($pipes[1], 10);
        echo "read returned\n";
    } catch (Async\AsyncCancellation $e) {
        echo get_class($e), "\n";
    }
});

spawn(function () use ($reader) { $reader->cancel(); });
Async\delay(10);

fwrite($pipes[0], "after");
fclose($pipes[0]);
echo "main reads: ", fread($pipes[1], 10), "\n";
proc_close($process);
?>
--EXPECT--
Async\AsyncCancellation
main reads: after
