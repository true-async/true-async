--TEST--
On the Poll queue, fclose() of a socket another coroutine is parked reading returns true and the parked read returns false (review B1, dev/RFC-CHANGES.md 12)
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
$closer = spawn(fn() => fclose($near));

echo "fclose: ";
var_dump(await($closer));
echo "read: ";
var_dump(await($reader));
?>
--EXPECT--
fclose: bool(true)
read: bool(false)
