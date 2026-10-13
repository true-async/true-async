--TEST--
Scope: the error route cancels every child scope, not only the first
--FILE--
<?php
use Async\Scope;
use Async\Future;
use Async\FutureState;
use function Async\suspend;

$outer = new Scope();
$outer->setChildScopeExceptionHandler(function (Scope $scope, Async\Coroutine $coroutine, Throwable $error) {
    echo "outer: took ", $error->getMessage(), "\n";
});
$failing = Scope::inherit($outer)->asNotSafely();
$log = [];
$members = [];
$child_scopes = [Scope::inherit($failing), Scope::inherit($failing)];
foreach (['first', 'second'] as $index => $name) {
    $members[] = $child_scopes[$index]->spawn(function () use ($name, &$log) {
        try {
            (new Future(new FutureState()))->await();
        } catch (Async\AsyncCancellation $e) {
            $log[] = "$name child scope's member cancelled";
        }
    });
}
while ($members[0]->getAwaitingInfo() === [] || $members[1]->getAwaitingInfo() === []) {
    suspend();
}
$failing->spawn(function () {
    throw new RuntimeException("failed");
});
while (!$members[0]->isCompleted() || !$members[1]->isCompleted()) {
    suspend();
}
sort($log);
echo implode("\n", $log), "\n";
?>
--EXPECT--
outer: took failed
first child scope's member cancelled
second child scope's member cancelled
