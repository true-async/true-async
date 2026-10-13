--TEST--
proc_open() multiple async processes
--SKIPIF--
<?php
if (!function_exists("proc_open")) echo "skip proc_open() is not available";
?>
--FILE--
<?php

use function Async\spawn;
use function Async\suspend;

$results = [];
$started = 0;
// The children wait for this file: none ends before the other task has run
$release = __DIR__ . '/002-proc_open_multiple.release';
@unlink($release);

echo "Main start\n";

spawn(function () use (&$results, &$started, $release) {
    echo "Process 1 starting\n";
    
    $descriptorspec = [
        0 => ["pipe", "r"],
        1 => ["pipe", "w"],
        2 => ["pipe", "w"]
    ];
    
    $php = getenv('TEST_PHP_EXECUTABLE');
    if ($php === false) {
        die("skip no php executable defined");
    }
    
    $process = proc_open(
        [$php, "-r", "while (!file_exists(\$argv[1])) usleep(1000); echo 'Process 1 done';", $release],
        $descriptorspec,
        $pipes
    );
    $started++;
    
    fclose($pipes[0]);
    // Read before closing: a child that writes to a closed pipe exits with 255.
    $output = stream_get_contents($pipes[1]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    
    $exit_code = proc_close($process);
    $results[] = "$output, exit: $exit_code";
    echo "Process 1 completed\n";
});

spawn(function () use (&$results, &$started, $release) {
    echo "Process 2 starting\n";
    
    $descriptorspec = [
        0 => ["pipe", "r"],
        1 => ["pipe", "w"],
        2 => ["pipe", "w"]
    ];
    
    $php = getenv('TEST_PHP_EXECUTABLE');
    if ($php === false) {
        die("skip no php executable defined");
    }
    
    $process = proc_open(
        [$php, "-r", "while (!file_exists(\$argv[1])) usleep(1000); echo 'Process 2 done';", $release],
        $descriptorspec,
        $pipes
    );
    $started++;
    
    fclose($pipes[0]);
    // Read before closing: a child that writes to a closed pipe exits with 255.
    $output = stream_get_contents($pipes[1]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    
    $exit_code = proc_close($process);
    $results[] = "$output, exit: $exit_code";
    echo "Process 2 completed\n";
});

spawn(function() use (&$started, $release) {
    while ($started < 2) {
        suspend();
    }

    echo "Other task executing\n";
    touch($release);
});

spawn(function() use (&$results) {
    while (count($results) < 2) {
        suspend();
    }

    sort($results);
    echo implode("\n", $results), "\n";
});

echo "Main end\n";
?>
--CLEAN--
<?php
@unlink(__DIR__ . '/002-proc_open_multiple.release');
?>
--EXPECTF--
Main start
Main end
%a
Other task executing
%a
Process 1 done, exit: 0
Process 2 done, exit: 0