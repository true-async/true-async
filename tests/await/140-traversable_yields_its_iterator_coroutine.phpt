--TEST--
A Traversable whose items hold the coroutine walking it, directly, as a result or as a Future's value, leaves nothing behind: the walk lets go of the wait at its end
--FILE--
<?php
use Async\Future;
use Async\FutureState;
use function Async\spawn;
use function Async\await;
use function Async\await_all;
use function Async\current_coroutine;

$itself = await(spawn(fn() => await_all((function () {
    yield 'other' => spawn(fn() => 1);
    yield 'itself' => current_coroutine();
})())));
var_dump(array_keys($itself[0]), $itself[0]['itself']);

$returns = await(spawn(fn() => await_all((function () {
    $walker = current_coroutine();
    yield spawn(fn() => $walker);
})())));
var_dump($returns[0][0] instanceof Async\Coroutine);

$completes = await(spawn(fn() => await_all((function () {
    $state = new FutureState();
    yield new Future($state);
    $state->complete(current_coroutine());
})())));
var_dump($completes[0][0] instanceof Async\Coroutine);
echo "end\n";
?>
--EXPECT--
array(2) {
  [0]=>
  string(5) "other"
  [1]=>
  string(6) "itself"
}
NULL
bool(true)
bool(true)
end
