--TEST--
MySQL fixture: a mysqli and a PDO connection are open at the same time, each sees the other
--EXTENSIONS--
mysqli
pdo_mysql
--SKIPIF--
<?php
if (getenv('MYSQL_TEST_HOST') === false) {
    die('skip no MySQL fixture: tools/test.py names one in MYSQL_TEST_HOST');
}
?>
--FILE--
<?php
$host = getenv('MYSQL_TEST_HOST');
$port = (int) getenv('MYSQL_TEST_PORT');
$user = getenv('MYSQL_TEST_USER');
$password = getenv('MYSQL_TEST_PASSWD');
$database = getenv('MYSQL_TEST_DB');

$mysqli = new mysqli($host, $user, $password, $database, $port);
$pdo = new PDO("mysql:host=$host;port=$port;dbname=$database", $user, $password);

$mysqli_id = (int) $mysqli->query('SELECT CONNECTION_ID()')->fetch_row()[0];
$pdo_id = (int) $pdo->query('SELECT CONNECTION_ID()')->fetchColumn();
var_dump($mysqli_id !== $pdo_id);

$both = "SELECT COUNT(*) FROM information_schema.PROCESSLIST WHERE ID IN ($mysqli_id, $pdo_id)";
var_dump($mysqli->query($both)->fetch_row()[0]);
var_dump($pdo->query($both)->fetchColumn());
var_dump($mysqli->query('SELECT DATABASE()')->fetch_row()[0] === $database);
?>
--EXPECT--
bool(true)
string(1) "2"
int(2)
bool(true)
