<html><head>
</head>
<body>

<form action="welcome.php" method="POST">
Name: <input type="text" name="name"><br>
E-mail: <input type="text" name="email"><br>
<input type="submit">
</form>

<?php
// another way to call error_log():
echo "Hi How are you ";
echo $_SERVER['QUERY_STRING'];
error_log("You messed up! \n", 3, "/var/log/php_errors.log");
?>
</body>
<html>

