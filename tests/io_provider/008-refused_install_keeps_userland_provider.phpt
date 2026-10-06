--TEST--
A userland provider set before the first coroutine stays in place: the install is refused, and the userland run() keeps getting the ops; once it is removed, the next coroutine installs the provider
--FILE--
<?php
final class Recorder implements Io\Hooks\Hooks
{
    public array $ops = [];

    public function getCapabilities(): array
    {
        return [];
    }

    public function run(Io\Operation $op): Io\Completion
    {
        $this->ops[] = get_class($op);

        return $op->complete(Io\CompletionStatus::Unsupported);
    }

    public function add(Io\Registration $registration): void {}

    public function remove(Io\Registration $registration): void {}
}

$recorder = new Recorder();
Io\Hooks\set_hooks($recorder);

Async\spawn(function () {
    usleep(1000);
    echo "spawned slept\n";
});
Async\suspend();

var_dump(Io\Hooks\get_hooks() === $recorder);
var_dump(count($recorder->ops) > 0);
Io\Hooks\set_hooks(null);
var_dump(Io\Hooks\is_active());
Async\spawn(function () {});
var_dump(Io\Hooks\is_active(), Io\Hooks\get_hooks());
?>
--EXPECT--
spawned slept
bool(true)
bool(true)
bool(false)
bool(true)
NULL
