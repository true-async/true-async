--TEST--
A cancelled stream_socket_accept() on a listener in the stream exception mode throws the cancellation, not a StreamException (dev/RFC-CHANGES.md 27)
--FILE--
<?php
use function Async\await;
use function Async\spawn;
use function Async\suspend;

$context = stream_context_create(['stream' => ['error_mode' => StreamErrorMode::Exception]]);
$server = stream_socket_server('tcp://127.0.0.1:0', $errno, $errstr, STREAM_SERVER_BIND | STREAM_SERVER_LISTEN, $context);

$acceptor = spawn(function () use ($server) {
    try {
        var_dump(stream_socket_accept($server, 5));
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    } catch (Throwable $e) {
        echo get_class($e), ": ", $e->getMessage(), "\n";
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
