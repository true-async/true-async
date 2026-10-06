--TEST--
Future::map() - a pending chain 200000 deep is released without nested frees
--INI--
memory_limit=-1
--FILE--
<?php

use Async\FutureState;
use Async\Future;

$state = new FutureState();
$tail = new Future($state);

for ($i = 0; $i < 200000; $i++) {
    $tail = $tail->map(fn($value) => $value);
}

unset($tail, $state);
echo "released\n";

?>
--EXPECTF--
Warning: Future was never used; call await(), map(), catch(), finally() or ignore() to suppress this warning. Created at %s:%d in %s on line %d
released
