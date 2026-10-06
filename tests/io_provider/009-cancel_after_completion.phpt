--TEST--
A coroutine cancelled after its read completed but before it resumed gets the cancellation; the bytes the read took stay in the stream (TrueAsync drops them)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat, sh and Unix sockets';
?>
--FILE--
<?php
use function Async\spawn;

[$r1, $w1] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);
[$r2, $w2] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

$reader = spawn(function () use ($r1) {
    try {
        $data = fread($r1, 10);
        echo "read: ", var_export($data, true), "\n";
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }

    stream_set_blocking($r1, false);
    echo "left in the stream: ", var_export(fread($r1, 10), true), "\n";
});

// Both reads complete in one tick, the canceller's first: it runs before the reader resumes.
spawn(function () use ($r2, $reader) {
    fread($r2, 10);
    echo "canceller resumed first\n";
    $reader->cancel();
});

spawn(function () use ($w1, $w2) {
    fwrite($w2, "wake");
    fwrite($w1, "data");
});
?>
--EXPECT--
canceller resumed first
cancelled
left in the stream: 'data'
