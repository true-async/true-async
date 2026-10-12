--TEST--
On the Poll queue, a coroutine reads a stream another coroutine is parked on in stream_select(); both see the data (review B1, dev/RFC-CHANGES.md 12)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
use function Async\spawn;
use function Async\await;

[$near, $far] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

$selector = spawn(function () use ($near) {
    $read = [$near];
    $write = null;
    $except = null;

    return stream_select($read, $write, $except, 5);
});

/* The selector runs first and parks, then the reader parks beside it. */
$reader = spawn(fn() => fread($near, 10));

spawn(fn() => fwrite($far, "data"));

echo "select: ", await($selector), "\n";
echo "read: ", await($reader), "\n";
?>
--EXPECT--
select: 1
read: data
