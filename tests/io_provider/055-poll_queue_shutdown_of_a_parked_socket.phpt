--TEST--
On the Poll queue, stream_socket_shutdown() of a socket another coroutine is parked reading returns true at once and the parked read ends with an empty string (review B1, dev/RFC-CHANGES.md 12)
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
$reader = spawn(fn() => fread($near, 10));
$closer = spawn(fn() => stream_socket_shutdown($near, STREAM_SHUT_RDWR));

echo "shutdown: ";
var_dump(await($closer));
echo "read: ";
var_dump(await($reader));
?>
--EXPECT--
shutdown: bool(true)
read: string(0) ""
