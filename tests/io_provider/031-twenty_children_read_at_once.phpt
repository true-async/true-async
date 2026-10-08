--TEST--
Twenty children read at once, each in its own coroutine: their waits overlap
--FILE--
<?php
use function Async\spawn;
use function Async\await_all;

$started = hrtime(true);
$readers = [];
for ($i = 0; $i < 20; $i++) {
    $readers[] = spawn(function () use ($i) {
        $process = proc_open([PHP_BINARY, '-r', "usleep(300000); echo 'child $i';"], [1 => ['pipe', 'w']], $pipes);
        $data = stream_get_contents($pipes[1]);
        fclose($pipes[1]);
        proc_close($process);
        return $data;
    });
}

[$results, $errors] = await_all($readers);
$elapsed = (hrtime(true) - $started) / 1e9;

$expected = array_map(fn ($i) => "child $i", range(0, 19));
echo "all read: ", var_export($results === $expected, true), "\n";
echo "errors: ", count($errors), "\n";
/* One after another the sleeps alone take 6 s */
echo "overlapped: ", var_export($elapsed < 4, true), "\n";
?>
--EXPECT--
all read: true
errors: 0
overlapped: true
