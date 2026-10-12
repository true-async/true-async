--TEST--
On the Poll queue, $mysqli->close() beside a parked query is refused and the query completes (review B2)
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
TrueAsync\Test\reactor_use_poll_queue();
use function Async\spawn;
use function Async\await;

mysqli_report(MYSQLI_REPORT_OFF);
$mysqli = new mysqli(getenv('MYSQL_TEST_HOST'), getenv('MYSQL_TEST_USER'), getenv('MYSQL_TEST_PASSWD'),
    getenv('MYSQL_TEST_DB'), (int) getenv('MYSQL_TEST_PORT'));

// A second connection holds a lock the query waits for, so the server answers only once it is released.
$holder = new mysqli(getenv('MYSQL_TEST_HOST'), getenv('MYSQL_TEST_USER'), getenv('MYSQL_TEST_PASSWD'),
    getenv('MYSQL_TEST_DB'), (int) getenv('MYSQL_TEST_PORT'));
$lock = 'true_async_015_' . getmypid();
$holder->query("SELECT GET_LOCK('$lock', 10)");

$sent = false;
$query = spawn(function () use ($mysqli, $lock, &$sent) {
    $sent = true;
    return $mysqli->query("SELECT GET_LOCK('$lock', 10) AS one")->fetch_assoc()['one'];
});

$closer = spawn(function () use ($mysqli, &$sent) {
    // Runs once the query is parked on the server's answer.
    while (!$sent) {
        Async\suspend();
    }

    try {
        $mysqli->close();
    } catch (Error $error) {
        echo "close: ", $error->getMessage(), "\n";
    }
});

await($closer);
$holder->query("SELECT RELEASE_LOCK('$lock')");
echo "query: ", await($query), "\n";
?>
--EXPECT--
close: Concurrent access to a MySQL connection
query: 1
