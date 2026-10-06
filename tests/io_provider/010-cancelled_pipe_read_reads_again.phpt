--TEST--
A coroutine cancelled while parked in a pipe read can read the pipe again at once: the queue withdrew the read, so the stream is not frozen
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
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }

    echo "read again: ", fread($pipes[1], 10), "\n";
});

spawn(function () use ($reader, $pipes) {
    $reader->cancel();
    Async\delay(10);
    fwrite($pipes[0], "again");
    fclose($pipes[0]);
});

Async\await($reader);
proc_close($process);
?>
--EXPECT--
cancelled
read again: again
