--TEST--
Channel: a recvAsync() Future being freed is out of the queue before its children's destructors run
--FILE--
<?php

use Async\Channel;

final class SendsOnDestruct
{
    public function __construct(private Channel $channel)
    {
    }

    public function __destruct()
    {
        echo "destructor sends: ", var_export($this->channel->sendAsync('value'), true), "\n";
    }
}

$channel = new Channel(1);
$future = $channel->recvAsync();
$hook = new SendsOnDestruct($channel);
// The child holds the only reference to the hook, and the pending source holds the only one to the child.
$future->map(function ($value) use ($hook) {
    return $value;
})->ignore();
unset($hook);
$future->ignore();
unset($future);

echo "count: ", count($channel), "\n";
echo "recv: ", $channel->recv(), "\n";
?>
--EXPECT--
destructor sends: true
count: 1
recv: value
