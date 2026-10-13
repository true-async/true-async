--TEST--
A socket that waited before the reactor's queue existed registers with the queue at its next wait
--SKIPIF--
<?php
if (PHP_OS_FAMILY === 'Windows') die('skip the IOCP Ring takes no registrations');
?>
--FILE--
<?php
use function Async\spawn;
use TrueAsync\Test;

$server = stream_socket_server('tcp://127.0.0.1:0');
$client = stream_socket_client('tcp://' . stream_socket_get_name($server, false));
$peer = stream_socket_accept($server);

$reads_started = 0;

$reader = spawn(function () use ($peer, &$reads_started) {
    echo "queue before the first wait: ", var_export(Test\reactor_state()['queue'], true), "\n";

    for ($i = 1; $i <= 3; $i++) {
        $reads_started = $i;
        $data = fread($peer, 10);
        echo "read $i: $data, registrations: ", Test\stream_queue_registrations($peer), "\n";
    }
});

spawn(function () use ($client, &$reads_started) {
    for ($i = 1; $i <= 3; $i++) {
        // One write per read: the reader has taken the last one when it starts the next.
        while ($reads_started < $i) {
            Async\suspend();
        }

        // The first timer creates the reactor's queue, which the reads after it register with.
        Async\delay(5);
        fwrite($client, "data $i");
    }
});

Async\await($reader);
?>
--EXPECT--
queue before the first wait: false
read 1: data 1, registrations: 0
read 2: data 2, registrations: 1
read 3: data 3, registrations: 1
