--TEST--
A query cancelled while parked closes the connection, and the next query reports it gone (review B2)
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
use function Async\spawn;
use function Async\await;

mysqli_report(MYSQLI_REPORT_OFF);
$mysqli = new mysqli(getenv('MYSQL_TEST_HOST'), getenv('MYSQL_TEST_USER'), getenv('MYSQL_TEST_PASSWD'),
    getenv('MYSQL_TEST_DB'), (int) getenv('MYSQL_TEST_PORT'));

$query = spawn(fn() => $mysqli->query('SELECT SLEEP(5)'));

/* The query runs first and parks on the server's answer. */
Async\suspend();
$query->cancel();

try {
    await($query);
} catch (Async\AsyncCancellation $cancellation) {
    echo "query cancelled\n";
}

var_dump($mysqli->query('SELECT 1'));
echo "error: ", $mysqli->error, "\n";
?>
--EXPECT--
query cancelled
bool(false)
error: MySQL server has gone away
