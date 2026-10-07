--TEST--
The iterator core walks an array and a Traversable with joining workers in the order of TrueAsync's iterate()
--FILE--
<?php

use Async\Scope;
use function Async\delay;
use function TrueAsync\Test\iterate;

function run(string $name, Closure $walk): void
{
    echo "--- $name\n";
    $scope = new Scope();
    $scope->spawn($walk);
    $scope->awaitCompletion(Async\timeout(2000));
}

run('array, two workers', function () {
    iterate(['a' => 1, 'b' => 2, 'c' => 3, 'd' => 4], function ($value, $key) {
        echo "start $key=$value\n";
        delay(10);
        echo "end $key\n";
    }, 2);
});

run('false stops the walk', function () {
    iterate([1, 2, 3], function ($value) {
        echo "value $value\n";
        return $value < 2;
    });
});

function numbers(): Generator
{
    for ($i = 0; $i < 3; $i++) {
        echo "generator moves to $i\n";
        delay(5);
        yield "k$i" => $i;
    }
}

run('a generator that suspends while it moves', function () {
    iterate(numbers(), function ($value, $key) {
        echo "start $key=$value\n";
        delay(5);
        echo "end $key\n";
    }, 3);
});

run('an iterator without keys of its own', function () {
    iterate(new ArrayIterator([5, 6]), function ($value, $key) {
        echo "$key=$value\n";
    });
});

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
--- false stops the walk
value 1
value 2
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
--- an iterator without keys of its own
0=5
1=6
