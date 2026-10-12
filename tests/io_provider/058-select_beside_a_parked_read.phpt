--TEST--
stream_select() beside a parked read answers at once with the ready member, and the read still gets its data (review B1, dev/RFC-CHANGES.md 12)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
?>
--FILE--
<?php
use function Async\spawn;
use function Async\await;

[$near, $far] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);
[$other, $other_peer] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);
fwrite($other_peer, "ready");

/* The reader runs first and parks: the pair holds no data. */
$reader = spawn(fn() => fread($near, 10));

$selector = spawn(function () use ($near, $other) {
    $read = [$near, $other];
    $write = null;
    $except = null;
    $count = stream_select($read, $write, $except, 0);

    return [$count, $read === [1 => $other]];
});

[$count, $only_other] = await($selector);
echo "select: $count\n";
echo "only the ready member: ";
var_dump($only_other);

fwrite($far, "data");
echo "read: ", await($reader), "\n";
?>
--EXPECT--
select: 1
only the ready member: bool(true)
read: data
