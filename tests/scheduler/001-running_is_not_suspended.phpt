--TEST--
State methods of main, of a queued coroutine, of the running one and of a finished one (D18)
--FILE--
<?php
use function Async\spawn;
use function Async\current_coroutine;

function state(string $name, Async\Coroutine $coroutine): void
{
    printf("%s: started=%d queued=%d running=%d suspended=%d completed=%d\n", $name,
        $coroutine->isStarted(), $coroutine->isQueued(), $coroutine->isRunning(),
        $coroutine->isSuspended(), $coroutine->isCompleted());
}

state('main', current_coroutine());

$first = spawn(function () use (&$first) {
    var_dump(current_coroutine() === $first);
    state('first inside', $first);
    return 42;
});

state('first queued', $first);

spawn(function () use ($first) {
    state('first finished', $first);
    var_dump($first->getResult());
});
?>
--EXPECT--
main: started=1 queued=0 running=1 suspended=0 completed=0
first queued: started=0 queued=1 running=0 suspended=1 completed=0
bool(true)
first inside: started=1 queued=0 running=1 suspended=0 completed=0
first finished: started=1 queued=0 running=0 suspended=0 completed=1
int(42)
