--TEST--
Scope: a child scope's free with a GC run due inside the close of its channel reads no parent that another coroutine freed
--FILE--
<?php

use Async\Channel;
use Async\Scope;

// The close rejects the pending recvAsync() Future and releases its ChannelException. One root short of the
// threshold puts that release inside the child's free, where a wait for the collection would let the queued
// coroutine drop the parent before the child's free reads it.
$parent = new Scope();
$child = Scope::inherit($parent);
$channel = Async\await($child->spawn(fn() => new Channel()));
$future = $channel->recvAsync();

Async\spawn(function () use (&$parent) {
    $parent = null;
    echo "parent dropped\n";
});

$held = [];
$status = gc_status();

for ($i = $status['threshold'] - $status['roots'] - 1; $i > 0; $i--) {
    $object = new stdClass();
    $held[] = $object;
    unset($object);
}

$runs = gc_status()['runs'];
$child = null;

Async\suspend();

try {
    $future->await();
} catch (Async\ChannelException $exception) {
    echo "recvAsync(): ", $exception->getMessage(), "\n";
}

echo "collection after: ", gc_status()['runs'] > $runs ? "yes" : "no", "\n";
?>
--EXPECT--
parent dropped
recvAsync(): Channel closed: owner scope disposed
collection after: yes
