--TEST--
Future: a second __construct() call replaces the state of a Future
--FILE--
<?php

use Async\FutureState;
use Async\Future;

$first = new FutureState();
$first->ignore();
$second = new FutureState();
$reconstructed = new Future($first);
$reconstructed->__construct($second);
$second->complete("second state");
echo $reconstructed->await(), "\n";

?>
--EXPECT--
second state
