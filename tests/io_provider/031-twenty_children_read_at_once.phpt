--TEST--
Twenty children read at once, each in its own coroutine: their waits overlap
--FILE--
<?php
use function Async\spawn;
use function Async\await_all;

$readers = [];
for ($i = 0; $i < 20; $i++) {
    $readers[] = spawn(function () use ($i) {
        $process = proc_open([PHP_BINARY, '-r', "usleep(1000000); echo 'child $i';"], [1 => ['pipe', 'w']], $pipes);
        $read_started = hrtime(true);
        $data = stream_get_contents($pipes[1]);
        $read_ended = hrtime(true);
        fclose($pipes[1]);
        proc_close($process);
        return [$data, $read_started, $read_ended];
    });
}

[$results, $errors] = await_all($readers);

$expected = array_map(fn ($i) => "child $i", range(0, 19));
echo "all read: ", var_export(array_column($results, 0) === $expected, true), "\n";
echo "errors: ", count($errors), "\n";
/* A read that blocked the thread would end before the next coroutine's read began */
echo "overlapped: ", var_export(max(array_column($results, 1)) < min(array_column($results, 2)), true), "\n";
?>
--EXPECT--
all read: true
errors: 0
overlapped: true
