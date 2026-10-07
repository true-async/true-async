--TEST--
A coroutine cancelled after its FIFO write's readiness arrived but before it resumed writes nothing: the readiness is no result the caller owns
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows' || !function_exists('posix_mkfifo')) echo 'skip Unix-only: a FIFO';
?>
--FILE--
<?php
use function Async\spawn;

$fifo = sys_get_temp_dir() . '/true_async_io_provider_020_' . getmypid();
posix_mkfifo($fifo, 0600);
$reader_end = fopen($fifo, 'r+');
$writer_end = fopen($fifo, 'w');
[$wake_reader, $wake_writer] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

// The FIFO is full: the next write parks on its readiness.
stream_set_blocking($writer_end, false);
$written = 0;

while (($chunk = fwrite($writer_end, str_repeat('a', 4096))) > 0) {
    $written += $chunk;
}

stream_set_blocking($writer_end, true);

$writer = spawn(function () use ($writer_end) {
    try {
        fwrite($writer_end, 'X');
        echo "written\n";
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }
});

// The canceller's read and the writer's readiness complete in one tick, the canceller's first: it
// runs before the writer resumes.
spawn(function () use ($wake_reader, $writer) {
    fread($wake_reader, 10);
    echo "canceller resumed first\n";
    $writer->cancel();
});

spawn(function () use ($reader_end, $wake_writer, $written) {
    fwrite($wake_writer, "wake");
    stream_set_blocking($reader_end, false);
    $read = 0;

    while ($read < $written) {
        $read += strlen(fread($reader_end, 65536));
    }
});

Async\await($writer);
Async\delay(10);
stream_set_blocking($reader_end, false);
echo "after the cancel: ", var_export(fread($reader_end, 10), true), "\n";
unlink($fifo);
?>
--EXPECT--
canceller resumed first
cancelled
after the cancel: ''
