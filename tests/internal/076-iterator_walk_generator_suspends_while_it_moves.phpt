--TEST--
The iterator core walks a generator that suspends while it moves, with joining workers, in the order of TrueAsync's iterate()
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

function numbers(Gates $gates): Generator
{
    for ($i = 0; $i < 3; $i++) {
        echo "generator moves to $i\n";
        $gates->wait("move $i");
        yield "k$i" => $i;
    }
}

run('a generator that suspends while it moves', function (Gates $gates) {
    iterate(numbers($gates), function ($value, $key) use ($gates) {
        echo "start $key=$value\n";
        $gates->wait($key);
        echo "end $key\n";
    }, 3);
}, [['move 0'], ['move 1'], ['k0', 'move 2'], ['move 2'], ['k1', 'k2'], ['k2']]);

?>
--EXPECT--
--- a generator that suspends while it moves
generator moves to 0
generator moves to 1
start k0=0
generator moves to 2
end k0
start k1=1
start k2=2
end k1
end k2
