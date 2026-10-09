--TEST--
Async\signal() on Windows: once no Future waits for SIGBREAK, Ctrl+Break ends the process as it does without the extension
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Windows') echo "skip Windows-only test";
?>
--FILE--
<?php
$child = __DIR__ . '/037-windows_ctrl_break_without_watch_ends_process.child.php';
file_put_contents($child, <<<'CHILD'
<?php
use Async\Signal;
use Async\TimeoutException;
use function Async\await;
use function Async\delay;
use function Async\signal;
use function Async\timeout;

try {
    await(signal(Signal::SIGBREAK, timeout(10)));
} catch (TimeoutException) {
    echo "ready\n";
}

delay(10000);
echo "not ended\n";
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
/* STATUS_CONTROL_C_EXIT: the default handler ended the process, not a crash */
$status = proc_close($process) & 0xFFFFFFFF;
echo $status === 0xC000013A ? "ended by the event\n" : sprintf("exit: 0x%X\n", $status);
?>
--CLEAN--
<?php
@unlink(__DIR__ . '/037-windows_ctrl_break_without_watch_ends_process.child.php');
?>
--EXPECT--
ready
bool(true)
ended by the event
