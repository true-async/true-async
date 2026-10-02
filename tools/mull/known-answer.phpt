--TEST--
Mutation known-answer check: the tested planted function, on both sides of the limit
--FILE--
<?php
var_dump(true_async_known_answer_tested(1, 2));
var_dump(true_async_known_answer_tested(2, 2));
var_dump(true_async_known_answer_tested(3, 2));
?>
--EXPECT--
int(3)
int(0)
int(1)
