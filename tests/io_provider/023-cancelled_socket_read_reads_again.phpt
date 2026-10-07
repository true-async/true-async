--TEST--
A coroutine cancelled while parked in a socket read can read the socket again at once: a read the Ring keeps after the cancel settles before the next one starts
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
?>
--FILE--
<?php
use function Async\spawn;

[$reader_end, $writer_end] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

$reader = spawn(function () use ($reader_end) {
    try {
        fread($reader_end, 10);
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }

    echo "read again: ", fread($reader_end, 10), "\n";
});

spawn(function () use ($reader, $writer_end) {
    $reader->cancel();
    fwrite($writer_end, "again");
});

Async\await($reader);
?>
--EXPECT--
cancelled
read again: again
