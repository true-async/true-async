--TEST--
A coroutine whose body returns by reference gives await() and getResult() a value, not the reference
--FILE--
<?php
$coroutine = Async\spawn(function &() {
    static $values = [1];
    return $values;
});

/* A reference to the body's static would make these writes visible through getResult(). */
$awaited = &Async\await($coroutine);
$awaited[] = 2;
$result = &$coroutine->getResult();
$result[] = 3;
var_dump($coroutine->getResult());
?>
--EXPECTF--
Notice: Only variables should be assigned by reference in %s on line %d

Notice: Only variables should be assigned by reference in %s on line %d
array(1) {
  [0]=>
  int(1)
}
