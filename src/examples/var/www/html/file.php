<?php
    $filename=$_POST["data"];
    if (isset($filename) && file_exists($filename)) {
      header("Content-disposition: attachment;filename=$filename");
      readfile($filename);
    } else {
      echo "Not a file";
    }
?>
