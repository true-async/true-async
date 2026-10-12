--TEST--
On the Ring, a socket_read() cancelled after its data arrived but before it resumed loses the bytes, where TrueAsync leaves them for the next read (review B3, M1, PLAN Open questions)
--EXTENSIONS--
sockets
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
if (!class_exists('Io\\Ring\\Engine') || (new Io\Ring\Engine())->getBackend() !== Io\Ring\Backend::IoUring) echo 'skip the Ring on io_uring: the thread backend orders completions by its workers';
?>
--FILE--
<?php
use function Async\await;
use function Async\spawn;

// The reader may resume first, as the kernel orders the two completions: such an attempt is checked
// and made again (io_provider/009).
for ($attempt = 1; $attempt <= 20; $attempt++) {
    socket_create_pair(AF_UNIX, SOCK_STREAM, 0, $pair);
    [$r, $w] = $pair;
    [$wake_reader, $wake_writer] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);
    $log = [];

    $reader = spawn(function () use (&$log, $r) {
        try {
            $data = socket_read($r, 10);
            $log[] = "read: " . var_export($data, true);
        } catch (Async\AsyncCancellation $e) {
            $log[] = "cancelled";
        }

        socket_set_nonblock($r);
        $log[] = "left in the socket: " . var_export(@socket_read($r, 10), true);
    });

    $canceller = spawn(function () use ($wake_reader, $reader, &$log) {
        fread($wake_reader, 10);
        $log[] = "canceller resumed first";
        $reader->cancel();
    });

    spawn(function () use ($w, $wake_writer) {
        fwrite($wake_writer, "wake");
        socket_write($w, "data");
    });

    await($reader);
    await($canceller);

    if ($log !== ["read: 'data'", "left in the socket: false", "canceller resumed first"]) {
        break;
    }
}

echo implode("\n", $log), "\n";
?>
--EXPECT--
canceller resumed first
cancelled
left in the socket: false
