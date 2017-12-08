<html>
<head>
<link type="text/css" rel="stylesheet" href="/styles/main.css" />
</head>
<body>
 <form method="POST" action="postData.php">
   <textarea name="data">

   </textarea>
   <input type="submit" name="submit">
 </form>
<?php
   echo $_POST["data"];
    // phpinfo();
   //echo "HI how are you";
   error_log("Hello ,How are you doing!",3,"/var/log/php_errors.log");
   error_log($_POST["data"], 3, "/var/log/php_errors.log");
?>
</body>
</html>