--TEST--
get_deadlocked_coroutines(): a parked coroutine holding one of a pair through an SplObjectStorage, an ArrayObject, a WeakMap value, a closure's bound $this, a suspended generator or an exception's trace is reported with the pair, each holder counted once
--INI--
zend.exception_ignore_args=0
--FILE--
<?php
use function Async\spawn;
use function Async\await;
use function Async\suspend;
use function Async\current_coroutine;
use function Async\get_deadlocked_coroutines;

final class Holder
{
    public function __construct(public Async\Coroutine $coroutine)
    {
    }
}

function make_holder(string $kind, Async\Coroutine $b): mixed
{
    switch ($kind) {
        case 'SplObjectStorage':
            $storage = new SplObjectStorage();
            $storage[$b] = null;
            return $storage;
        case 'ArrayObject':
            return new ArrayObject([$b]);
        case 'WeakMap value':
            $map = new WeakMap();
            $key = new stdClass();
            $map[$key] = $b;
            return [$map, $key];
        case 'bound $this':
            return Closure::bind(function () {
                return $this->coroutine;
            }, new Holder($b), Holder::class);
        case 'suspended generator':
            $generator = (function () use ($b) {
                yield 1;
                yield $b;
            })();
            $generator->current();
            return $generator;
        case 'exception trace':
            return (function (Async\Coroutine $argument) {
                return new Exception("held");
            })($b);
    }
}

function start_group(string $kind): array
{
    $a = null;
    $b = null;
    $a = spawn(function () use (&$b) {
        suspend();
        await($b);
    });
    $b = spawn(function () use (&$a) {
        suspend();
        await($a);
    });
    $holder = make_holder($kind, $b);
    $c = spawn(function () use ($a, $holder) {
        await($a);
    });
    $holder = null;

    return [spl_object_id($a), spl_object_id($b), spl_object_id($c)];
}

$kinds = ['SplObjectStorage', 'ArrayObject', 'WeakMap value', 'bound $this', 'suspended generator', 'exception trace'];
$groups = [];

foreach ($kinds as $kind) {
    $groups[$kind] = start_group($kind);
}

for ($i = 0; $i < 4; $i++) {
    suspend();
}

$found = array_map(spl_object_id(...), get_deadlocked_coroutines());

foreach ($groups as $kind => $ids) {
    echo $kind, ": ", count(array_intersect($ids, $found)), " found\n";
}

foreach (Async\get_coroutines() as $coroutine) {
    if ($coroutine !== current_coroutine()) {
        $coroutine->cancel();
    }
}

suspend();
echo "end\n";
?>
--EXPECT--
SplObjectStorage: 3 found
ArrayObject: 3 found
WeakMap value: 3 found
bound $this: 3 found
suspended generator: 3 found
exception trace: 3 found
end
