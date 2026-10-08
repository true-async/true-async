--TEST--
Context: a value released at the teardown's last step whose destructor maps a Future starts no drain, as the scopes are gone
--FILE--
<?php

use Async\Future;
use function Async\root_context;
use function Async\spawn;

class Mapper
{
    public function __destruct()
    {
        Future::completed(1)->map(fn($value) => $value)->ignore();
        echo "mapper destructor\n";
    }
}

class Starter
{
    public function __destruct()
    {
        TrueAsync\Test\print_at_teardown();
        spawn(fn() => root_context()->set('mapper', new Mapper()));
    }
}

$starter = new Starter();
echo "end\n";

?>
--EXPECT--
end
teardown: done
mapper destructor
