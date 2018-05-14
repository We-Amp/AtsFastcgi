<html>
<body>

Welcome <?php echo $_POST["name"]; ?><br>
Your email address is: <?php echo $_POST["email"]; 
echo "<br/>";
foreach ($_SERVER as $key => $value) {
    // $arr[3] will be updated with each value from $arr...
    echo "<h>{$key} => {$value} </h><br/> ";
 	
}

#echo "Post Params".$_SERVER;
echo "<br/>QUERY_STRING: ".$_SERVER['QUERY_STRING'];
echo "<br/> Request Uri: ".$_SERVER['REQUEST_URI'];

?>
</body>
</html>
