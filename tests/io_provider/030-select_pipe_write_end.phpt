--TEST--
stream_select() on the parent's write end of a pipe: always writable, and readable in the read set so that the read reports the error
--SKIPIF--
<?php if (PHP_OS_FAMILY !== 'Windows') echo 'skip Windows-only: a pipe has no write readiness there';
?>
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$process = proc_open([PHP_BINARY, '-r', 'fgets(STDIN);'], [0 => ['pipe', 'r']], $pipes);

await(spawn(function () use ($pipes) {
    $except = null;
    $read = [$pipes[0]];
    $write = [$pipes[0]];
    echo "ready: ", stream_select($read, $write, $except, 1), " read ", count($read), " write ", count($write), "\n";

    $read = null;
    $write = [$pipes[0]];
    echo "write set: ", stream_select($read, $write, $except, null), "\n";
    fwrite($pipes[0], "\n");
}));

fclose($pipes[0]);
echo "exit: ", proc_close($process), "\n";
?>
--EXPECT--
ready: 2 read 1 write 1
write set: 1
exit: 0
