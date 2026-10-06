--TEST--
stream_select() on a listener sees every connection waiting, after a parked accept and between accepts
--FILE--
<?php
use function Async\spawn;

$server = stream_socket_server('tcp://127.0.0.1:0');
$address = 'tcp://' . stream_socket_get_name($server, false);

/* An accept in a coroutine first */
$acceptor = spawn(fn () => stream_socket_accept($server, 10));
$clients[] = stream_socket_client($address);
var_dump(is_resource(Async\await($acceptor)));

/* Two connections at once, then the usual loop: select, then a non-blocking accept */
stream_set_blocking($server, false);
$clients[] = stream_socket_client($address);
$clients[] = stream_socket_client($address);
Async\delay(10);

for ($i = 1; $i <= 2; $i++) {
    $read = [$server];
    $write = $except = null;
    $start = hrtime(true);
    $ready = stream_select($read, $write, $except, 5);
    $waited = (hrtime(true) - $start) / 1e9;
    $accepted = stream_socket_accept($server, 0);
    echo "select $i: $ready, at once: ", var_export($waited < 1, true), ", accepted: ",
        var_export(is_resource($accepted), true), "\n";
}
?>
--EXPECT--
bool(true)
select 1: 1, at once: true, accepted: true
select 2: 1, at once: true, accepted: true
