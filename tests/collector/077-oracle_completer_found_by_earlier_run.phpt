--TEST--
The fuzz oracle: a completer found by an earlier run than its waiter does not excuse the wake, so a coroutine marked as found and woken by a found coroutine that ran since aborts the process
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') {
    die("skip the child repeats this process's command line from /proc/self/cmdline");
}
?>
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\suspend;
use function Async\delay;
use function Async\get_coroutines;
use TrueAsync\Test;

if (getenv('ORACLE_MODE') !== false) {
    if (function_exists('posix_setrlimit')) {
        posix_setrlimit(POSIX_RLIMIT_CORE, 0, 0);
    }

    $state = new FutureState();
    $gate = new FutureState();
    $completer = spawn(function () use ($state, $gate) {
        (new Future($gate))->await();
        $state->complete(1);
    });
    while ($completer->getAwaitingInfo() === []) {
        suspend();
    }

    /* Found and handed out: the gate's completion below may wake it. */
    Test\mark_found($completer);
    get_coroutines();

    $waiter = spawn(function () use ($state) {
        (new Future($state))->await();
    });
    while ($waiter->getAwaitingInfo() === []) {
        suspend();
    }

    Test\mark_found($waiter);
    $gate->complete(1);
    delay(300);
    echo "no abort\n";
    exit(0);
}

$arguments = explode("\0", rtrim(file_get_contents('/proc/self/cmdline'), "\0"));
$script = null;

foreach ($arguments as $index => $argument) {
    if (realpath($argument) === __FILE__) {
        $script = $index;
    }
}

$command = array_slice($arguments, 0, $script);

if (end($command) === '-f') {
    array_pop($command);
}

$command[] = __FILE__;
/* The child tests the oracle, not an interleaving: the fuzz's order could run the waiter after the
 * completion. */
$environment = getenv();
unset($environment['TRUE_ASYNC_SCHED']);

$child = proc_open($command, [1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes, null, ['ORACLE_MODE' => 'found earlier'] + $environment);
$output = stream_get_contents($pipes[1]);
$errors = stream_get_contents($pipes[2]);
proc_close($child);

if (preg_match('/^true_async collector: coroutine #\d+ was found never to wake, and (.*)$/m', $errors, $match)) {
    echo "aborted: ", $match[1], "\n";
} else {
    echo trim($output), "\n";
}
?>
--EXPECT--
aborted: an event it waits for completed
