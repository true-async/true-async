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

// A select that saw the connections only at its time limit would end after this delay.
$late = spawn(function () {
    Async\delay(10000);
    echo "the 10 s delay ended first\n";
});

for ($i = 1; $i <= 2; $i++) {
    $read = [$server];
    $write = $except = null;
    $ready = stream_select($read, $write, $except, 30);
    $accepted = stream_socket_accept($server, 0);
    echo "select $i: $ready, accepted: ", var_export(is_resource($accepted), true), "\n";
}

$late->cancel();
?>
--EXPECT--
bool(true)
select 1: 1, accepted: true
select 2: 1, accepted: true
