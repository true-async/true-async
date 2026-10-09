--TEST--
A pipe read cancelled while the child keeps writing: the reader gets the cancellation, keeps what earlier reads returned and closes the pipe
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\delay;

$process = proc_open([PHP_BINARY, '-r', 'for ($i = 0; $i < 200; $i++) { printf("%05d\n", $i); usleep(2000); }'],
    [1 => ['pipe', 'w']], $pipes);

$received = 0;

$reader = spawn(function () use ($pipes, &$received) {
    $data = '';
    $cancelled = false;

    try {
        while (!feof($pipes[1])) {
            $data .= fread($pipes[1], 7);
            $received = strlen($data);
        }
    } catch (Async\AsyncCancellation) {
        $cancelled = true;
    }

    // A cancelled read may lose the bytes it took from the stream's buffer, so the stream is closed.
    fclose($pipes[1]);

    return [$data, $cancelled];
});

spawn(function () use ($reader, &$received) {
    while ($received < 60 && !$reader->isCompleted()) {
        delay(1);
    }

    $reader->cancel();
});

[$data, $cancelled] = await($reader);
$expected = implode('', array_map(fn ($i) => sprintf("%05d\n", $i), range(0, 199)));
echo "cancelled: ", var_export($cancelled, true), "\n";
echo "read before the cancel: ", var_export(strlen($data) >= 60, true), "\n";
echo "prefix: ", var_export(str_starts_with($expected, $data), true), "\n";
// The child dies writing into the closed pipe; its exit code is not this test's business.
echo "reaped: ", var_export(proc_close($process) !== -1, true), "\n";
?>
--EXPECT--
cancelled: true
read before the cancel: true
prefix: true
reaped: true
