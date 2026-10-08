--TEST--
A pipe passed to a second child after a stream_select() on it timed out: the child reads all of it
--FILE--
<?php
use function Async\spawn;
use function Async\await;

$writer = proc_open([PHP_BINARY, '-r', 'usleep(200000); echo "hello";'], [1 => ['pipe', 'w']], $out);

await(spawn(function () use ($out) {
    $read = [$out[1]];
    $write = $except = null;
    echo "select before data: ", stream_select($read, $write, $except, 0, 50000), "\n";

    $copier = proc_open([PHP_BINARY, '-r', 'echo strtoupper(stream_get_contents(STDIN));'],
        [0 => $out[1], 1 => ['pipe', 'w']], $copied);
    echo "second child got: ", stream_get_contents($copied[1]), "\n";
    fclose($copied[1]);
    echo "second child exit: ", proc_close($copier), "\n";
}));

fclose($out[1]);
echo "first child exit: ", proc_close($writer), "\n";
?>
--EXPECT--
select before data: 0
second child got: HELLO
second child exit: 0
first child exit: 0
