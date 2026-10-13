--TEST--
Pool: a cycle through the second idle resource is collected
--FILE--
<?php

use Async\Pool;

$holder = new stdClass();
$made = 0;
$holder->pool = new Pool(
    factory: function () use (&$made) {
        $resource = new stdClass();
        $resource->number = ++$made;
        return $resource;
    },
    min: 2,
);

$first = $holder->pool->acquire();
$second = $holder->pool->acquire();
$second->owner = $holder;
$holder->pool->release($first);
$holder->pool->release($second);

$watch = WeakReference::create($holder);
unset($holder, $first, $second);
gc_collect_cycles();

echo "holder alive: ", $watch->get() === null ? "no" : "yes", "\n";
?>
--EXPECT--
holder alive: no
