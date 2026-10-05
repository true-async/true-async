--TEST--
Async\CompositeException::addException() throws as `$array[] =` does when the list's next key is taken
--FILE--
<?php
$composite = new Async\CompositeException();
(new ReflectionProperty($composite, 'exceptions'))->setValue($composite, [PHP_INT_MAX => new Exception('a')]);

try {
    $composite->addException(new RuntimeException('b'));
} catch (Error $error) {
    echo get_class($error), ': ', $error->getMessage(), "\n";
}

var_dump(array_map(fn ($exception) => $exception->getMessage(), $composite->getExceptions()));
?>
--EXPECT--
Error: Cannot add element to the array as the next element is already occupied
array(1) {
  [9223372036854775807]=>
  string(1) "a"
}
