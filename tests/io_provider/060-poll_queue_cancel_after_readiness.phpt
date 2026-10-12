--TEST--
On the Poll queue, a coroutine cancelled after its socket read's readiness arrived but before it resumed gets the cancellation; the bytes stay for the next read (review B3, io_provider/009)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
use function Async\await;
use function Async\spawn;

[$r1, $w1] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);
[$r2, $w2] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

$reader = spawn(function () use ($r1) {
    try {
        $data = fread($r1, 10);
        echo "read: ", var_export($data, true), "\n";
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled, unread: ", stream_get_meta_data($r1)['unread_bytes'], "\n";
    }

    stream_set_blocking($r1, false);
    echo "left in the stream: ", var_export(fread($r1, 10), true), "\n";
});

// The canceller's readiness and the reader's arrive in one poll, the canceller's first: it runs
// before the reader resumes.
$canceller = spawn(function () use ($r2, $reader) {
    fread($r2, 10);
    echo "canceller resumed first\n";
    $reader->cancel();
});

spawn(function () use ($w1, $w2) {
    fwrite($w2, "wake");
    fwrite($w1, "data");
});

await($reader);
await($canceller);
?>
--EXPECT--
canceller resumed first
cancelled, unread: 0
left in the stream: 'data'
