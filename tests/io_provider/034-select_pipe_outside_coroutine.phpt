--TEST--
stream_select() on a pipe outside any coroutine: an idle pipe waits the timeout out, then the data is reported
--FILE--
<?php
$process = proc_open([PHP_BINARY, '-r', 'usleep(300000); echo "late";'], [1 => ['pipe', 'w']], $pipes);

$write = $except = null;
$read = [$pipes[1]];
$started = hrtime(true);
echo "idle: ", stream_select($read, $write, $except, 0, 100000), "\n";
echo "waited: ", var_export((hrtime(true) - $started) / 1e6 >= 50, true), "\n";

$read = [$pipes[1]];
echo "later: ", stream_select($read, $write, $except, 5), "\n";
echo "data: ", fread($pipes[1], 10), "\n";
fclose($pipes[1]);
echo "exit: ", proc_close($process), "\n";
?>
--EXPECT--
idle: 0
waited: true
later: 1
data: late
exit: 0
