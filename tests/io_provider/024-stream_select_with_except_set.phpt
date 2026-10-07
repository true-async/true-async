--TEST--
stream_select() with an except set parks its coroutine: another coroutine runs and makes the stream readable
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
?>
--FILE--
<?php
use function Async\spawn;

[$reader_end, $writer_end] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

spawn(function () use ($writer_end) {
    Async\delay(10);
    echo "writer ran\n";
    fwrite($writer_end, "x");
});

$read = [$reader_end];
$write = null;
$except = [$reader_end];
var_dump(stream_select($read, $write, $except, 5));
var_dump(count($read));
?>
--EXPECT--
writer ran
int(1)
int(1)
