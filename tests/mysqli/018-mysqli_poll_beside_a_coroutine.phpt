--TEST--
mysqli_poll() parks its coroutine while it waits, so another coroutine runs (review M9, dev/RFC-CHANGES.md 29)
--EXTENSIONS--
mysqli
--SKIPIF--
<?php
if (getenv('MYSQL_TEST_HOST') === false) {
    die('skip no MySQL fixture: tools/test.py names one in MYSQL_TEST_HOST');
}
?>
--FILE--
<?php
use function Async\await;
use function Async\spawn;
use function Async\suspend;

mysqli_report(MYSQLI_REPORT_OFF);
$connect = fn () => new mysqli(getenv('MYSQL_TEST_HOST'), getenv('MYSQL_TEST_USER'), getenv('MYSQL_TEST_PASSWD'),
    getenv('MYSQL_TEST_DB'), (int) getenv('MYSQL_TEST_PORT'));
$mysqli = $connect();

// A second connection holds a lock the query waits for, so the server answers only once it is released.
$holder = $connect();
$lock = 'true_async_018_' . bin2hex(random_bytes(8));
$holder->query("SELECT GET_LOCK('$lock', 10)");

$polling = false;
$poller = spawn(function () use ($mysqli, $lock, &$polling) {
    $mysqli->query("SELECT GET_LOCK('$lock', 10) AS one", MYSQLI_ASYNC);
    $read = $reject = [$mysqli];
    $error = [];
    $polling = true;
    $ready = mysqli_poll($read, $error, $reject, 60);
    echo "poll: ", $ready, "\n";
    $one = $mysqli->reap_async_query()->fetch_assoc()['one'];
    echo "one: ", $one, "\n";
});

while (!$polling) {
    suspend();
}
echo "main: releasing\n";
$holder->query("SELECT RELEASE_LOCK('$lock')");
await($poller);
?>
--EXPECT--
main: releasing
poll: 1
one: 1
