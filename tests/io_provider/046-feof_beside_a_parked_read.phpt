--TEST--
feof() and stream_get_meta_data() of a socket another coroutine is parked reading answer without waiting (review B1, dev/RFC-CHANGES.md 12)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
?>
--FILE--
<?php
use function Async\spawn;
use function Async\await;

[$near, $far] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

$reader = spawn(function () use ($near) {
    $data = fread($near, 10);
    echo "read: $data\n";
});

/* The reader runs first and parks: the pair holds no data. */
spawn(function () use ($near, $far) {
    echo "feof: ";
    var_dump(feof($near));
    echo "eof in metadata: ";
    var_dump(stream_get_meta_data($near)['eof']);
    fwrite($far, "data");
});

await($reader);
?>
--EXPECT--
feof: bool(false)
eof in metadata: bool(false)
read: data
