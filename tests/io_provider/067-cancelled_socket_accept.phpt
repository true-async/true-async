--TEST--
A cancelled socket_accept() throws the cancellation and returns no freed Socket (dev/RFC-CHANGES.md 26)
--EXTENSIONS--
sockets
--FILE--
<?php
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$server = socket_create(AF_INET, SOCK_STREAM, SOL_TCP);
socket_bind($server, '127.0.0.1', 0);
socket_listen($server);

$acceptor = spawn(function () use ($server) {
    try {
        var_dump(socket_accept($server));
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
