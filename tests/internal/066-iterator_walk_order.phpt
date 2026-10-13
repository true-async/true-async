--TEST--
The iterator core walks an array with joining workers in the order of TrueAsync's iterate()
--FILE--
<?php

use Async\Future;
use Async\FutureState;
use Async\Scope;
use function Async\await;
use function Async\spawn;
use function TrueAsync\Test\iterate;

// Each wait of a walk parks on a gate of its name. The driver opens a gate once the waits named with it
// are parked, as a timer that fires at the idle point finds them, so the order needs no wall clock.
final class Gates
{
    private array $parked = [];
    private array $opened = [];

    public function wait(string $name): void
    {
        $this->state($this->parked, $name)->complete(null);
        await(new Future($this->state($this->opened, $name)));
    }

    public function open(string $name, string ...$also_parked): void
    {
        foreach ([$name, ...$also_parked] as $waiting) {
            await(new Future($this->state($this->parked, $waiting)));
        }

        $this->state($this->opened, $name)->complete(null);
    }

    private function state(array &$states, string $name): FutureState
    {
        return $states[$name] ??= new FutureState();
    }
}

function run(string $name, Closure $walk, array $schedule = []): void
{
    echo "--- $name\n";
    $gates = new Gates();
    spawn(function () use ($gates, $schedule) {
        foreach ($schedule as $step) {
            $gates->open(...$step);
        }
    });
    $scope = new Scope();
    $scope->spawn($walk, $gates);
    $scope->awaitCompletion(new Future(new FutureState()));
}

run('array, two workers', function (Gates $gates) {
    iterate(['a' => 1, 'b' => 2, 'c' => 3, 'd' => 4], function ($value, $key) use ($gates) {
        echo "start $key=$value\n";
        $gates->wait($key);
        echo "end $key\n";
    }, 2);
}, [['a', 'b'], ['b', 'c'], ['c', 'd'], ['d']]);

?>
--EXPECT--
--- array, two workers
start a=1
start b=2
end a
start c=3
end b
start d=4
end c
end d
