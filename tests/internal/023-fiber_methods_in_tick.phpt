--TEST--
Fiber::start(), resume(), throw() and suspend() from a destructor run in the tick refuse, and leave the fibers as they were (S3.md 4.6)
--FILE--
<?php
use function Async\suspend;
use TrueAsync\Test;

class Probe
{
    public function __construct(private Fiber $suspended, private Fiber $fresh)
    {
    }

    public function __destruct()
    {
        $calls = [
            'start' => fn() => $this->fresh->start(),
            'resume' => fn() => $this->suspended->resume(),
            'throw' => fn() => $this->suspended->throw(new Exception("thrown")),
            'suspend' => fn() => Fiber::suspend(),
        ];

        foreach ($calls as $name => $call) {
            try {
                $call();
                echo "$name: no error\n";
            } catch (FiberError $error) {
                echo "$name: ", $error->getMessage(), "\n";
            }
        }
    }
}

$suspended = new Fiber(function () {
    Fiber::suspend();
    echo "suspended fiber resumed\n";
});
$suspended->start();
$fresh = new Fiber(function () {
    echo "fresh fiber ran\n";
});

/* The tick runs on the runner fiber's stack: the destructor sees a fiber coroutine as current. */
$runner = new Fiber(function () use ($suspended, $fresh) {
    $probe = new Probe($suspended, $fresh);
    Test\defer('A', null, function () use ($probe) {
    });
    unset($probe);
    suspend();
    echo "runner after the tick\n";
});

$runner->start();
$suspended->resume();
$fresh->start();
echo "end\n";
?>
--EXPECT--
microtask A sched=1
released A
start: Cannot switch fibers in current execution context
resume: Cannot switch fibers in current execution context
throw: Cannot switch fibers in current execution context
suspend: Cannot switch fibers in current execution context
runner after the tick
suspended fiber resumed
fresh fiber ran
end
