--TEST--
IO provider: an op with neither a stream nor a handle answered while still in flight is not drained, which would wait for every op of the queue, a pending signal wait among them (dev/SECURITY.md, S6.9)
--EXTENSIONS--
pcntl
posix
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') echo "skip Unix-only test";
?>
--FILE--
<?php
use TrueAsync\Test;

// The pending signal wait: a drain with no owner would block until it settles, which only the signal below does.
$signal = Async\signal(Async\Signal::SIGUSR1);
[$reader, $writer] = stream_socket_pair(STREAM_PF_UNIX, STREAM_SOCK_STREAM, STREAM_IPPROTO_IP);

$reading = Async\spawn(function () use ($reader) {
    Test\fail_io('answer_in_flight');

    return Test\read_without_owner($reader, 1);
});

var_dump(Async\await($reading));
posix_kill(getmypid(), SIGUSR1);
var_dump(Async\await($signal));
?>
--EXPECT--
string(7) "timeout"
enum(Async\Signal::SIGUSR1)
