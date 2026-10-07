--TEST--
The fuzz oracle: a coroutine marked as found, not handed out, aborts the process when a future, an await_all() item, a future token, a Timeout or a coroutine it awaits wakes it; handed out afterwards, it may wake
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
use function Async\await;
use function Async\await_all;
use function Async\suspend;
use function Async\delay;
use function Async\timeout;
use function Async\get_coroutines;
use TrueAsync\Test;

if (($mode = getenv('ORACLE_MODE')) !== false) {
    if (function_exists('posix_setrlimit')) {
        posix_setrlimit(POSIX_RLIMIT_CORE, 0, 0);
    }

    $state = new FutureState();
    $target = spawn(function () use ($state) {
        (new Future($state))->await();
    });
    $waiter = spawn(match ($mode) {
        'future', 'handed out' => function () use ($state) {
            (new Future($state))->await();
        },
        'await_all item' => function () use ($state) {
            await_all([new Future($state)]);
        },
        'future token' => function () use ($state) {
            (new Future(new FutureState()))->await(new Future($state));
        },
        'timeout token' => function () {
            (new Future(new FutureState()))->await(timeout(100));
        },
        'coroutine' => function () use ($target) {
            await($target);
        },
    });
    while ($target->getAwaitingInfo() === [] || $waiter->getAwaitingInfo() === []) {
        suspend();
    }

    Test\mark_found($waiter);

    if ($mode === 'handed out') {
        get_coroutines();
    }

    $state->complete(1);
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

foreach (['future', 'await_all item', 'future token', 'timeout token', 'coroutine', 'handed out'] as $mode) {
    $child = proc_open($command, [1 => ['pipe', 'w'], 2 => ['pipe', 'w']], $pipes, null, ['ORACLE_MODE' => $mode] + $environment);
    $output = stream_get_contents($pipes[1]);
    $errors = stream_get_contents($pipes[2]);
    proc_close($child);

    if (preg_match('/^true_async collector: coroutine #\d+ was found never to wake, and (.*)$/m', $errors, $match)) {
        echo $mode, ": aborted: ", $match[1], "\n";
    } else {
        echo $mode, ": ", trim($output), "\n";
    }
}
?>
--EXPECTF--
future: aborted: an event it waits for completed
await_all item: aborted: an event it waits for completed
future token: aborted: an event it waits for completed
timeout token: aborted: an event it waits for completed
coroutine: aborted: coroutine #%d woke it
handed out: no abort
