--TEST--
A coroutine cancelled while its TLS handshake waits for the server's answer gets the cancellation, with no spin and no warning (dev/RFC-CHANGES.md 37)
--EXTENSIONS--
openssl
--FILE--
<?php
use function Async\await;
use function Async\spawn;

/* The server takes the ClientHello and never answers */
$server = stream_socket_server('tcp://127.0.0.1:0');
$address = stream_socket_get_name($server, false);

$client = spawn(function () use ($address) {
    try {
        var_dump(stream_socket_client("tls://$address", $errno, $errstr, 30, STREAM_CLIENT_CONNECT,
            stream_context_create(['ssl' => ['verify_peer' => false, 'verify_peer_name' => false]])));
    } catch (Async\AsyncCancellation $e) {
        echo "cancelled\n";
    }
});

$peer = stream_socket_accept($server, 10);
echo "ClientHello: ", strlen(fread($peer, 1)), " byte read\n";
$client->cancel();
await($client);
echo "done\n";
?>
--EXPECT--
ClientHello: 1 byte read
cancelled
done
