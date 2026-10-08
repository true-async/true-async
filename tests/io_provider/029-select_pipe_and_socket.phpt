--TEST--
stream_select() over a pipe and a socket: each is reported when it becomes readable, and both when both are
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\delay;

$process = proc_open([PHP_BINARY, '-r', 'usleep(300000); echo "p";'], [1 => ['pipe', 'w']], $pipes);
[$sock, $peer] = stream_socket_pair(PHP_OS_FAMILY === 'Windows' ? STREAM_PF_INET : STREAM_PF_UNIX,
    STREAM_SOCK_STREAM, 0);

spawn(function () use ($peer) {
    delay(100);
    fwrite($peer, "s");
});

await(spawn(function () use ($pipes, $sock, $peer) {
    $show = function (array $ready) use ($pipes) {
        return implode(',', array_map(fn ($s) => $s === $pipes[1] ? 'pipe' : 'socket', $ready));
    };
    $write = $except = null;

    $read = [$pipes[1], $sock];
    echo "first: ", stream_select($read, $write, $except, 5), " ", $show($read), "\n";

    $read = [$pipes[1]];
    echo "second: ", stream_select($read, $write, $except, 5), " ", $show($read), "\n";

    $read = [$pipes[1], $sock];
    echo "both: ", stream_select($read, $write, $except, 5), " ", $show($read), "\n";

    echo "data: ", fread($pipes[1], 10), fread($sock, 10), "\n";
}));

fclose($pipes[1]);
fclose($sock);
fclose($peer);
echo "exit: ", proc_close($process), "\n";
?>
--EXPECT--
first: 1 socket
second: 1 pipe
both: 2 pipe,socket
data: ps
exit: 0
