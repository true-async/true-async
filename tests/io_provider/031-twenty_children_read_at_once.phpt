--TEST--
Twenty children read at once, each in its own coroutine: their waits overlap
--FILE--
<?php
use function Async\spawn;
use function Async\suspend;
use function Async\await_all;

$inputs = [];
$readers = [];
for ($i = 0; $i < 20; $i++) {
    $readers[] = spawn(function () use ($i, &$inputs) {
        $process = proc_open([PHP_BINARY, '-r', 'echo stream_get_contents(STDIN);'],
            [0 => ['pipe', 'r'], 1 => ['pipe', 'w']], $pipes);
        $inputs[$i] = $pipes[0];
        $data = stream_get_contents($pipes[1]);
        fclose($pipes[1]);
        proc_close($process);
        return $data;
    });
}

/* The children get their input only once all twenty reads wait: a read that blocked the thread
 * would never end */
$writer = spawn(function () use (&$inputs) {
    while (count($inputs) < 20) {
        suspend();
    }

    foreach ($inputs as $i => $input) {
        fwrite($input, "child $i");
        fclose($input);
    }
});

[$results, $errors] = await_all([...$readers, $writer]);

$expected = array_map(fn ($i) => "child $i", range(0, 19));
echo "all read: ", var_export(array_slice($results, 0, 20) === $expected, true), "\n";
echo "errors: ", count($errors), "\n";
?>
--EXPECT--
all read: true
errors: 0
