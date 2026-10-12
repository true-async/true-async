--TEST--
On the Poll queue, a coroutine cancelled after its FIFO read's readiness arrived but before it resumed gets the cancellation; the bytes stay in the FIFO (review B3)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows' || !function_exists('posix_mkfifo')) echo 'skip Unix-only: a FIFO';
?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
use function Async\await;
use function Async\spawn;

$fifo = sys_get_temp_dir() . '/true_async_io_provider_062_' . getmypid();
posix_mkfifo($fifo, 0600);
$reader_end = fopen($fifo, 'r+');
$writer_end = fopen($fifo, 'w');
unlink($fifo);
[$wake_reader, $wake_writer] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

$reader = spawn(function () use ($reader_end) {
    try {
        $data = fread($reader_end, 10);
        echo "read: ", var_export($data, true), "\n";
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled, unread: ", stream_get_meta_data($reader_end)['unread_bytes'], "\n";
    }

    stream_set_blocking($reader_end, false);
    echo "left in the FIFO: ", var_export(fread($reader_end, 10), true), "\n";
});

// The canceller's read and the reader's readiness complete in one tick, the canceller's first: it
// runs before the reader resumes.
$canceller = spawn(function () use ($wake_reader, $reader) {
    fread($wake_reader, 10);
    echo "canceller resumed first\n";
    $reader->cancel();
});

spawn(function () use ($writer_end, $wake_writer) {
    fwrite($wake_writer, "wake");
    fwrite($writer_end, "data");
});

await($reader);
await($canceller);
?>
--EXPECT--
canceller resumed first
cancelled, unread: 0
left in the FIFO: 'data'
