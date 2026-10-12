--TEST--
On the Ring, a connect to localhost whose first address refuses and whose second accepts leaves the stream's waits working (review M4)
--EXTENSIONS--
sockets
--SKIPIF--
<?php
// The order php_network_getaddresses() tries: IPv6 only where an IPv6 socket can be made.
function localhost_addresses(): array
{
    $ipv6 = @socket_create(AF_INET6, SOCK_DGRAM, SOL_UDP) !== false;
    $addresses = [];
    foreach (socket_addrinfo_lookup('localhost', null, ['ai_socktype' => SOCK_STREAM]) ?: [] as $info) {
        $explained = socket_addrinfo_explain($info);
        if ($explained['ai_family'] === AF_INET) {
            $addresses[] = $explained['ai_addr']['sin_addr'];
        } elseif ($ipv6) {
            $addresses[] = '[' . $explained['ai_addr']['sin6_addr'] . ']';
        }
    }

    return array_values(array_unique($addresses));
}

// A listener on each of the first two addresses, on one port.
function listen_on_both(array $addresses): ?array
{
    $second = @stream_socket_server("tcp://{$addresses[1]}:0");
    if ($second === false) {
        return null;
    }
    $port = (int) substr(strrchr(stream_socket_get_name($second, false), ':'), 1);
    $first = @stream_socket_server("tcp://{$addresses[0]}:$port");

    return $first === false ? null : [$first, $second, $port];
}

$addresses = localhost_addresses();
if (count($addresses) < 2) echo 'skip localhost resolves to one address';
elseif (listen_on_both($addresses) === null) echo 'skip cannot listen on both of localhost\'s first addresses';
?>
--FILE--
<?php
use function Async\await;
use function Async\spawn;
use function Async\suspend;

// The order php_network_getaddresses() tries: IPv6 only where an IPv6 socket can be made.
function localhost_addresses(): array
{
    $ipv6 = @socket_create(AF_INET6, SOCK_DGRAM, SOL_UDP) !== false;
    $addresses = [];
    foreach (socket_addrinfo_lookup('localhost', null, ['ai_socktype' => SOCK_STREAM]) ?: [] as $info) {
        $explained = socket_addrinfo_explain($info);
        if ($explained['ai_family'] === AF_INET) {
            $addresses[] = $explained['ai_addr']['sin_addr'];
        } elseif ($ipv6) {
            $addresses[] = '[' . $explained['ai_addr']['sin6_addr'] . ']';
        }
    }

    return array_values(array_unique($addresses));
}

// A listener on each of the first two addresses, on one port.
function listen_on_both(array $addresses): ?array
{
    $second = @stream_socket_server("tcp://{$addresses[1]}:0");
    if ($second === false) {
        return null;
    }
    $port = (int) substr(strrchr(stream_socket_get_name($second, false), ':'), 1);
    $first = @stream_socket_server("tcp://{$addresses[0]}:$port");

    return $first === false ? null : [$first, $second, $port];
}

[$first, $second, $port] = listen_on_both(localhost_addresses());

// With both listening, the connect takes the first address: the order above is the one tried.
$client = await(spawn(fn () => stream_socket_client("tcp://localhost:$port")));
echo "first address tried first: ", var_export(stream_socket_get_name($client, true) === stream_socket_get_name($first, false), true), "\n";
fclose($client);
fclose($first);

// With the first listener closed, the first attempt is refused and its socket closed before the second is tried.
$client = await(spawn(fn () => stream_socket_client("tcp://localhost:$port")));
$peer = stream_socket_accept($second);
echo "connected to the second address: ", var_export(stream_socket_get_name($client, true) === stream_socket_get_name($second, false), true), "\n";

$parked = false;
$reader = spawn(function () use ($client, &$parked) {
    $parked = true;
    return fread($client, 4);
});
while (!$parked) {
    suspend();
}
fwrite($peer, "pong");
$data = await($reader);
echo "client read: ", $data, "\n";

fwrite($client, "ping");
echo "peer read: ", fread($peer, 4), "\n";
?>
--EXPECT--
first address tried first: true
connected to the second address: true
client read: pong
peer read: ping
