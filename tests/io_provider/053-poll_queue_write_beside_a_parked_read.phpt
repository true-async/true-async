--TEST--
On the Poll queue, a write to a socket another coroutine is parked reading completes, and the reader gets the reply (review B1, dev/RFC-CHANGES.md 12)
--SKIPIF--
<?php if (PHP_OS_FAMILY === 'Windows') echo 'skip Unix-only: Unix sockets';
?>
--FILE--
<?php
TrueAsync\Test\reactor_use_poll_queue();
use function Async\spawn;
use function Async\await;

[$near, $far] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, 0);

/* The reader runs first and parks: the pair holds no data. */
$reader = spawn(fn() => fread($near, 100));

$writer = spawn(fn() => fwrite($near, "request"));

echo "written: ", await($writer), "\n";
echo "peer got: ", fread($far, 100), "\n";
fwrite($far, "reply");
echo "read: ", await($reader), "\n";
?>
--EXPECT--
written: 7
peer got: request
read: reply
