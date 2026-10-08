--TEST--
Context: set() with replace releases the old value after the write; its destructor reads the key, grows the table and unsets the key
--FILE--
<?php

class OldValue
{
    public function __destruct()
    {
        $context = Async\coroutine_context();
        echo "destructor reads: ", $context->find('key'), "\n";

        for ($i = 0; $i < 8; $i++) {
            $context->set("key$i", $i);
        }

        $context->unset('key');
    }
}

$context = Async\coroutine_context();
$context->set('key', new OldValue());
$context->set('key', 'new value', true);

var_dump($context->has('key'));
var_dump($context->get('key7'));

?>
--EXPECT--
destructor reads: new value
bool(false)
int(7)
