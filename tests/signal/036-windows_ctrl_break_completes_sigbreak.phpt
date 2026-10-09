--TEST--
Async\signal() on Windows: Ctrl+Break sent to the process completes its SIGBREAK Future
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Windows') echo "skip Windows-only test";
?>
--FILE--
<?php
$child = __DIR__ . '/036-windows_ctrl_break_completes_sigbreak.child.php';
file_put_contents($child, <<<'CHILD'
<?php
use Async\Signal;
use function Async\await;
use function Async\signal;
use function Async\timeout;

$future = signal(Signal::SIGBREAK, timeout(5000));
echo "ready\n";
echo "received: ", await($future)->name, "\n";
CHILD);

/* A process group of its own, so the event reaches the child only. */
$process = proc_open(
    getenv('TEST_PHP_EXECUTABLE_ESCAPED') . ' ' . getenv('TEST_PHP_EXTRA_ARGS') . ' ' . escapeshellarg($child),
    [1 => ['pipe', 'w'], 2 => ['pipe', 'w']],
    $pipes,
    null,
    null,
    ['bypass_shell' => true, 'create_process_group' => true]
);

echo fgets($pipes[1]);
var_dump(sapi_windows_generate_ctrl_event(PHP_WINDOWS_EVENT_CTRL_BREAK, proc_get_status($process)['pid']));
echo stream_get_contents($pipes[1]);
echo stream_get_contents($pipes[2]);
echo "exit: ", proc_close($process), "\n";
?>
--CLEAN--
<?php
@unlink(__DIR__ . '/036-windows_ctrl_break_completes_sigbreak.child.php');
?>
--EXPECT--
ready
bool(true)
received: SIGBREAK
exit: 0
