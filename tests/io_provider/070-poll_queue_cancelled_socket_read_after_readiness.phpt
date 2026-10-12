--TEST--
On the Poll queue, a socket_read() cancelled after its readiness arrived but before it resumed leaves the bytes for the next read (review B3)
--EXTENSIONS--
sockets
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
use function Async\await;
use function Async\spawn;

socket_create_pair(AF_UNIX, SOCK_STREAM, 0, $pair);
[$wake_reader, $wake_writer] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

$reader = spawn(function () use ($pair) {
    try {
        $data = socket_read($pair[0], 10);
        echo "read: ", var_export($data, true), "\n";
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }

    socket_set_nonblock($pair[0]);
    echo "left in the socket: ", var_export(@socket_read($pair[0], 10), true), "\n";
});

// The canceller's read and the reader's complete in one tick, the canceller's first: it runs before
// the reader resumes.
$canceller = spawn(function () use ($wake_reader, $reader) {
    fread($wake_reader, 10);
    echo "canceller resumed first\n";
    $reader->cancel();
});

spawn(function () use ($pair, $wake_writer) {
    fwrite($wake_writer, "wake");
    socket_write($pair[1], "data");
});

await($reader);
await($canceller);
?>
--EXPECT--
canceller resumed first
cancelled
left in the socket: 'data'
