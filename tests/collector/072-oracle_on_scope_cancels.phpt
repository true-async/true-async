--TEST--
The fuzz oracle and a scope's cancels: a found member of a parent scope still aborts the process when the parent is cancelled after a child scope's handler took an error, or by a hook of a SpawnStrategy that provides the parent; a hook given the current scope through a null provideScope() is excused
--SKIPIF--
<?php
if (PHP_OS_FAMILY !== 'Linux') {
    die("skip the child repeats this process's command line from /proc/self/cmdline");
}
?>
--FILE--
<?php
use Async\Scope;
use Async\Coroutine;
use Async\SpawnStrategy;
use Async\Future;
use Async\FutureState;
use function Async\spawn_with;
use function Async\suspend;
use TrueAsync\Test;

final class CancellingStrategy implements SpawnStrategy
{
    public function __construct(private ?Scope $scope) {}

    public function provideScope(): ?Scope
    {
        return $this->scope;
    }

    public function beforeCoroutineEnqueue(Coroutine $coroutine, Scope $scope): array
    {
        $scope->cancel();
        return [];
    }

    public function afterCoroutineEnqueue(Coroutine $coroutine, Scope $scope): void
    {
    }
}

if (($mode = getenv('ORACLE_MODE')) !== false) {
    // run-tests' timeout kills only the test, not this child, should a flag below never be set
    set_time_limit(30);
    if (function_exists('posix_setrlimit')) {
        posix_setrlimit(POSIX_RLIMIT_CORE, 0, 0);
    }

    $parent = new Scope();
    $member = $parent->spawn(function () {
        (new Future(new FutureState()))->await();
    });
    while ($member->getAwaitingInfo() === []) {
        suspend();
    }

    Test\mark_found($member);

    if ($mode === 'child handler') {
        $child = Scope::inherit($parent);
        $child->setExceptionHandler(function () {
            $GLOBALS['handled'] = true;
        });
        $child->spawn(function () {
            throw new RuntimeException("boom");
        });
        // A delay could end before the child ran: a timer already due when main parks wakes it in
        // that tick, ahead of the queued child
        while (!isset($handled)) {
            suspend();
        }
        $parent->cancel();
    } elseif ($mode === 'provided scope') {
        spawn_with(new CancellingStrategy($parent), function () {
        });
    } else {
        $parent->spawn(function () {
            spawn_with(new CancellingStrategy(null), function () {
            });
            $GLOBALS['spawned'] = true;
        });
        while (!isset($spawned)) {
            suspend();
        }
    }

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
$environment = getenv();
unset($environment['TRUE_ASYNC_SCHED']);

foreach (['child handler', 'provided scope', 'current scope'] as $mode) {
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
--EXPECT--
child handler: aborted: code that holds it cancelled it
provided scope: aborted: code that holds it cancelled it
current scope: no abort
