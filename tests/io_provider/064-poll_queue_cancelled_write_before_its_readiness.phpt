--TEST--
On the Poll queue, a socket write cancelled before its readiness arrived throws the cancellation and sends nothing (review M1)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
use function Async\await;
use function Async\spawn;
use function Async\suspend;

[$writer_end, $peer] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

// The socket is full: the next write parks.
stream_set_blocking($writer_end, false);
$filled = 0;

while (($chunk = fwrite($writer_end, str_repeat('a', 65536))) > 0) {
    $filled += $chunk;
}

stream_set_blocking($writer_end, true);

$writer = spawn(function () use ($writer_end) {
    try {
        $written = fwrite($writer_end, 'XYZ');
        echo "written: $written\n";
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }
});

suspend();

// The peer drains the socket and the writer is cancelled before the scheduler runs again.
stream_set_blocking($peer, false);
$received = '';

while (strlen($received) < $filled) {
    $received .= fread($peer, 1 << 20);
}

$writer->cancel();
await($writer);

echo "the peer got: ", var_export(substr($received, $filled) . fread($peer, 10), true), "\n";
?>
--EXPECT--
cancelled
the peer got: ''
