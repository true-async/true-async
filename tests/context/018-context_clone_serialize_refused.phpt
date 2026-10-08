--TEST--
Context: clone, serialize(), unserialize() and dynamic properties are refused
--FILE--
<?php

$context = new Async\Context();

try {
    clone $context;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

try {
    serialize($context);
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}

try {
    unserialize('O:13:"Async\Context":0:{}');
} catch (Exception $e) {
    echo $e->getMessage(), "\n";
}

try {
    $context->value = 1;
} catch (Error $e) {
    echo $e->getMessage(), "\n";
}

?>
--EXPECT--
Trying to clone an uncloneable object of class Async\Context
Serialization of 'Async\Context' is not allowed
Unserialization of 'Async\Context' is not allowed
Cannot create dynamic property Async\Context::$value
