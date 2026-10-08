--TEST--
A pipe read cancelled while the child keeps writing loses no bytes: what the cancelled read took is read next
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\delay;

$process = proc_open([PHP_BINARY, '-r', 'for ($i = 0; $i < 200; $i++) { printf("%05d\n", $i); usleep(2000); }'],
    [1 => ['pipe', 'w']], $pipes);

$reader = spawn(function () use ($pipes) {
    $data = '';
    $cancelled = 0;
    while (!feof($pipes[1])) {
        try {
            $data .= fread($pipes[1], 7);
        } catch (Async\AsyncCancellation) {
            $cancelled++;
        }
    }
    return [$data, $cancelled];
});

spawn(function () use ($reader) {
    delay(100);
    $reader->cancel();
});

[$data, $cancelled] = await($reader);
$expected = implode('', array_map(fn ($i) => sprintf("%05d\n", $i), range(0, 199)));
echo "cancelled: $cancelled\n";
echo "complete: ", var_export($data === $expected, true), "\n";
fclose($pipes[1]);
echo "exit: ", proc_close($process), "\n";
?>
--EXPECT--
cancelled: 1
complete: true
exit: 0
