--TEST--
A cancelled stream_socket_accept() throws the cancellation and prints no warning (dev/RFC-CHANGES.md 27)
--FILE--
<?php
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$server = stream_socket_server('tcp://127.0.0.1:0');

$acceptor = spawn(function () use ($server) {
    try {
        var_dump(stream_socket_accept($server, 5));
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }
});

suspend();
$acceptor->cancel();
await($acceptor);
echo "done\n";
?>
--EXPECT--
cancelled
done
