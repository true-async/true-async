--TEST--
Pool: an object its holder dropped leaves the pool's count of handed out objects, so a new object is refused
--FILE--
<?php

$pool = new Async\Pool(factory: fn() => new stdClass(), max: 2);

$dropped_id = spl_object_id($pool->tryAcquire());
$new = new stdClass();
$kept = $pool->tryAcquire();

echo spl_object_id($new) === $dropped_id ? "the new object took the dropped one's id\n" : "other id\n";

try {
    $pool->release($new);
    echo "released\n";
} catch (Async\PoolException $e) {
    echo $e->getMessage(), "\n";
}

$pool->release($kept);
echo "kept one released\n";
?>
--EXPECT--
the new object took the dropped one's id
The resource was not acquired from this pool, or was released already
kept one released
