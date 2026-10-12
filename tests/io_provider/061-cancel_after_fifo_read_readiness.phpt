--TEST--
A coroutine cancelled after its FIFO read's readiness arrived but before it resumed gets the cancellation; the bytes stay in the FIFO, since a pipe read waits for readiness and reads only after the resume (review B3)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows' || !function_exists('posix_mkfifo')) echo 'skip Unix-only: a FIFO';
if (!class_exists('Io\\Ring\\Engine') || (new Io\Ring\Engine())->getBackend() !== Io\Ring\Backend::IoUring) echo 'skip the Ring on io_uring: the thread backend orders completions by its workers';
?>
--FILE--
<?php
use function Async\await;
use function Async\spawn;

// The reader may resume first, as the kernel orders the two completions: such an attempt is checked
// and made again (io_provider/009).
for ($attempt = 1; $attempt <= 20; $attempt++) {
    $fifo = sys_get_temp_dir() . '/true_async_io_provider_061_' . getmypid();
    posix_mkfifo($fifo, 0600);
    $r = fopen($fifo, 'r+');
    $w = fopen($fifo, 'w');
    unlink($fifo);
    [$wake_reader, $wake_writer] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);
    $log = [];

    $reader = spawn(function () use (&$log, $r) {
        try {
            $data = fread($r, 10);
            $log[] = "read: " . var_export($data, true);
        } catch (Async\AsyncCancellation $e) {
            $log[] = "cancelled, unread: " . stream_get_meta_data($r)['unread_bytes'];
        }

        stream_set_blocking($r, false);
        $log[] = "left in the FIFO: " . var_export(fread($r, 10), true);
    });

    $canceller = spawn(function () use ($wake_reader, $reader, &$log) {
        fread($wake_reader, 10);
        $log[] = "canceller resumed first";
        $reader->cancel();
    });

    spawn(function () use ($w, $wake_writer) {
        fwrite($wake_writer, "wake");
        fwrite($w, "data");
    });

    await($reader);
    await($canceller);

    if ($log !== ["read: 'data'", "left in the FIFO: ''", "canceller resumed first"]) {
        break;
    }
}

echo implode("\n", $log), "\n";
?>
--EXPECT--
canceller resumed first
cancelled, unread: 0
left in the FIFO: 'data'
