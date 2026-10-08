--TEST--
Context: a destructor that runs at shutdown after the Context's own destructor phase reads its values
--FILE--
<?php

class Reader
{
    public Async\Context $context;
    public Reader $self;

    public function __destruct()
    {
        var_dump($this->context->find('key'));
    }
}

$context = new Async\Context();
$context->set('key', 'value');
$context->set('self', $context);

$reader = new Reader();
$reader->context = $context;
$reader->self = $reader;

echo "end\n";

?>
--EXPECT--
end
string(5) "value"
