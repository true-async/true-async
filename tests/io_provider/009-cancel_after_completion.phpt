--TEST--
A coroutine cancelled after its read completed but before it resumed gets the cancellation; the bytes the read took stay in the stream (TrueAsync drops them)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: cat, sh and Unix sockets';
?>
--FILE--
<?php
use function Async\await;
use function Async\spawn;

// Both reads are meant to complete in one tick, with the canceller resuming first. Which of two
// sockets' reads the Ring completes first is the kernel's order, not the test's: an attempt where
// the reader resumed first is checked (its read returns the bytes) and made again.
for ($attempt = 1; $attempt <= 20; $attempt++) {
    [$r1, $w1] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);
    [$r2, $w2] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);
    $log = [];

    $reader = spawn(function () use ($r1, &$log) {
        try {
            $data = fread($r1, 10);
            $log[] = "read: " . var_export($data, true);
        } catch (Async\AsyncCancellation $e) {
            // The completed read has put its bytes into the stream's buffer.
            $log[] = "cancelled, unread: " . stream_get_meta_data($r1)['unread_bytes'];
        }

        stream_set_blocking($r1, false);
        $log[] = "left in the stream: " . var_export(fread($r1, 10), true);
    });

    $canceller = spawn(function () use ($r2, $reader, &$log) {
        fread($r2, 10);
        $log[] = "canceller resumed first";
        $reader->cancel();
    });

    spawn(function () use ($w1, $w2) {
        fwrite($w2, "wake");
        fwrite($w1, "data");
    });

    await($reader);
    await($canceller);

    if ($log !== ["read: 'data'", "left in the stream: ''", "canceller resumed first"]) {
        break;
    }
}

echo implode("\n", $log), "\n";
?>
--EXPECT--
canceller resumed first
cancelled, unread: 4
left in the stream: 'data'
