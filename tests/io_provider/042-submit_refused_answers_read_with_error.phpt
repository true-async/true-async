--TEST--
IO provider: a read the IO queue refuses is answered with the queue's error, as the syscall would fail, and the coroutine goes on
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use TrueAsync\Test;

[$reader, $writer] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, STREAM_IPPROTO_IP);

$reading = Async\spawn(function () use ($reader) {
    Test\fail_io('submit');
    var_dump(fread($reader, 10));
});

Async\await($reading);
echo "end\n";
?>
--EXPECT--
bool(false)
end
