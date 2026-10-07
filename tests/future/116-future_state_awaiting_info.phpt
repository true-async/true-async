--TEST--
FutureState::getAwaitingInfo() names the state as pending, then as completed
--FILE--
<?php
$state = new Async\FutureState();
var_dump($state->getAwaitingInfo());

$state->complete(1);
$state->ignore();
var_dump($state->getAwaitingInfo());
?>
--EXPECT--
array(1) {
  [0]=>
  string(20) "FutureState(pending)"
}
array(1) {
  [0]=>
  string(22) "FutureState(completed)"
}
