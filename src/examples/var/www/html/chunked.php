<?php

function dump_chunk($chunk) {
    //echo sprintf("%x\r\n", strlen($chunk));
    echo sprintf("\r\n");
    echo $chunk;
    echo "\r\n";
}

ob_start();
?>
<!DOCTYPE html>
<html>
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
        <title>Title</title>
    </head>
    <body>
        <?php
        ob_end_flush();
        flush();
        ob_flush();
        for ($i = 0; $i < 10; $i++) {
            sleep(1);
            dump_chunk('Sending data chunk ' . ($i + 1) . ' of 10 <br />');
            flush();
            ob_flush();
        }
        sleep(1); // needed for last animation
        ?>
    </body>
</html>
