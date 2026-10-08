--TEST--
proc_open() given a pipe another coroutine is parked reading from throws "Concurrent access to a stream", and the reader still gets its data
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Windows') echo 'skip Windows-only: a POSIX descriptor is handed out under a read';
?>
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$process = proc_open([PHP_BINARY, '-r', 'usleep(300000); echo "data";'], [1 => ['pipe', 'w']], $pipes);

$reader = spawn(fn () => fread($pipes[1], 100));

spawn(function () use ($pipes) {
    try {
        proc_open([PHP_BINARY, '-r', 'exit(0);'], [0 => $pipes[1]], $unused);
    } catch (Error $e) {
        echo get_class($e), ": ", $e->getMessage(), "\n";
    }
});

$data = await($reader);
echo "read: $data\n";
fclose($pipes[1]);
echo "exit: ", proc_close($process), "\n";
?>
--EXPECTF--
Warning: proc_open(): Cannot represent a stream of type STDIO as a File Descriptor in %s on line %d
Error: Concurrent access to a stream
read: data
exit: 0
