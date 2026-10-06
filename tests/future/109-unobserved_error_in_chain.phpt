--TEST--
Future::map() - an error nobody observed is reported once, by the last link of the chain
--FILE--
<?php

use Async\FutureState;
use Async\Future;

$state = new FutureState();
$future = new Future($state);
$tail = $future->map(fn($value) => $value)->map(fn($value) => $value);

$state->error(new Exception("lost"));
unset($future, $state, $tail);

echo "Done\n";

?>
--EXPECTF--
Done

Warning: Future was never used; call await(), map(), catch(), finally() or ignore() to suppress this warning. Created at %s:%d in %s on line %d

Warning: Unhandled exception in Future: lost; use catch() or ignore() to handle. Created at %s:%d in %s on line %d
