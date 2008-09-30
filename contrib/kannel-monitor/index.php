<?php

/*
 * kannel-status.php -- display a summarized kannel status page
 *
 * This php script acts as client for the status.xml output of
 * Kannel's bearerbox and aggregates information about smsc link
 * status and message flow. It is the first step to provide an
 * external Kannel Control Center interface via HTTP.
 *
 * Stipe Tolj <stolj@wapme.de>
 * Copyright (c) 2003 Kannel Group.
 */

include("xmlfunc.php");

/* config section: define which Kannel status URLs to monitor */

$configs = array(
            array( "base_url" => "http://kannel.yourdomain.com:13000",
                   "status_passwd" => "foobar",
                   "admin_passwd" => "",
                   "name" => "Kannel 1"
                 ),
            array( "base_url" => "http://kannel.yourdomain.com:23000",
                   "status_passwd" => "foobar",
                   "admin_passwd" => "",
                   "name" => "Kannel 2"
                 ),
            array( "base_url" => "http://kannel.yourdomain.com:33000",
                   "status_passwd" => "foobar",
                   "admin_passwd" => "",
                   "name" => "Kannel 3"
                 )
            );

/* some constants */
$CONST_QUEUE_ERROR = 100;

$depth = array();
$status = array();

/* set php internal error reporting level */
error_reporting(0); 

?>

<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.0 Transitional//EN">
<html>
<head>
    <meta http-equiv="refresh" content="<?php
    
        /* determine if we have a set refresh rate, default to 60s */
        $refresh = $HTTP_GET_VARS[refresh];
        $timeout = (!empty($refresh)) ? $refresh : 60;
        echo "$timeout; URL=$_SERVER[REQUEST_URI]"; ?>">
    <title>Kannel Status</title>
    <link href="kannel.css" rel="stylesheet" type="text/css">
    <script src="kannel.js" type="text/javascript"></script>
</head>
<body>

<!-- the header block showing some basic information -->
<table width=100% cellspacing=0 cellpadding=0 border=0>
<tr><td valign=top>
  <h3>Kannel Status Monitor</h3>
</td><td valign=top align=left class=text>
  Current date and time: <br />
    <b><?php echo date("Y-m-d H:i:s"); ?></b>
</td><td valign=top align=right class=text>
  Refrash rate: <br />
    <?php

        $t_down = ceil($timeout / 2);
        $t_up = ceil($timeout * 2);
        $purl = parse_url($_SERVER['REQUEST_URI']);
        $base_uri = $purl[path];
    ?>
  <a class=href href="<?php echo $base_uri."?refresh=".$t_down; ?>"><?php echo $t_down; ?>s</a> | 
  <b><?php echo $timeout; ?>s</b> | 
  <a class=href href="<?php echo $base_uri."?refresh=".$t_up; ?>"><?php echo $t_up; ?>s</a>
</td></tr>
</table>


<table width=100% cellspacing=0 cellpadding=5 border=0>
<tr><td valign=top align=left class=text>
  <?php echo sizeof($configs); ?> instance(s) configured for this monitor: <br />
</td><td valign=top align=right class=text>
  Admin commands:
</td></tr>
  
<?php
    
      /* loop through all configured URLs */
    foreach ($configs as $inst => $config) {

        echo "<tr><td class=text valign=top align=left>\n";
                
        $xml_parser = xml_parser_create();
        xml_set_element_handler($xml_parser, "startElement", "endElement");

        /* get the status.xml URL of one config */
        $url = $config["base_url"]."/status.xml?password=".$config["status_passwd"];

        $status[$inst] = "";

        /* open the file description to the URL */
        if (($fp = fopen($url, "r"))) {
            echo "<span class=green>($inst) (".$config["name"].") <b>$url</b></span> <br /> \n";

            /* read the XML input */
            while (!feof($fp)) {  
                $status[$inst] .= fread($fp, 200000);
            }

        } else {
            echo "<span class=red>($inst) (".$config["name"].") <b>$url</b></span> <br /> \n";
        }     
        
        fclose($fp);

        /* get the status of this bearerbox */
        $s = XPathValue("gateway/status", $status[$inst]);
        if (ereg("(.*), uptime (.*)d (.*)h (.*)m (.*)s", $s, $regs)) {
            $ts = ($regs[2]*24*60*60) + ($regs[3]*60*60) 
                  + ($regs[4]*60) + $regs[5];

            echo "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;";
            echo "<b>$regs[1]</b>, ";
            echo "started ".date("Y-m-d H:i:s", mktime()-$ts);
            $bb_time[$inst] = mktime()-$ts;
            echo ", uptime $regs[2]d $regs[3]h $regs[4]m $regs[5]s <br />\n";
        }

        /* get the inbound load of this bearerbox */
        $s = XPathValue("gateway/sms/inbound", $status[$inst]);
        echo "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<b>Inbound:</b> $s<br/>";

        /* get the outbound load of this bearerbox */
        $s = XPathValue("gateway/sms/outbound", $status[$inst]);       
        echo "&nbsp;&nbsp;&nbsp;<b>Outbound:</b> $s<br/>";      

        /* get the info of this bearerbox */        
        $s = XPathValue("gateway/version", $status[$inst]);
        $s = str_replace(chr(10),'<br />&nbsp&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;'.
             '&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;',$s);
        $s = str_replace(chr(13),'',$s); 
        echo "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<b>Version:</b> $s<br/>";

        ?>
        </td><td class=text valign=top align=right>
            <a class=href href="#" onClick="admin_url('suspend', '<?php echo $config["base_url"]."/suspend"; ?>', '<?php echo $config["admin_passwd"]; ?>');">suspend</a> |
			   <a class=href href="#" onClick="admin_url('isolate', '<?php echo $config["base_url"]."/isolate"; ?>', '<?php echo $config["admin_passwd"]; ?>');">isolate</a> |
			   <a class=href href="#" onClick="admin_url('resume', '<?php echo $config["base_url"]."/resume"; ?>', '<?php echo $config["admin_passwd"]; ?>');">resume</a> <br/>
			   <a class=href href="#" onClick="admin_url('flush-dlr', '<?php echo $config["base_url"]."/flush-dlr"; ?>', '<?php echo $config["admin_passwd"]; ?>');">flush-dlr</a> |
			   <a class=href href="#" onClick="admin_url('shutdown', '<?php echo $config["base_url"]."/shutdown"; ?>', '<?php echo $config["admin_passwd"]; ?>');">shutdown</a> |
			   <a class=href href="#" onClick="admin_url('restart', '<?php echo $config["base_url"]."/restart"; ?>', '<?php echo $config["admin_passwd"]; ?>');">restart</a>
        </td></tr>

        <?php

        /*
        if (!xml_parse($xml_parser, $status[$url], feof($fp))) {
            die(sprintf("XML error: %s at line %d",
                xml_error_string(xml_get_error_code($xml_parser)),
                xml_get_current_line_number($xml_parser)));
        }
        */

        xml_parser_free($xml_parser);
  }

?>

</table>

<h4>Overall SMS traffic</h4>

<p id=bord>
<table width=100% cellspacing=0 cellpadding=5 border=1>
<tr><td valign=top align=right class=text>
  Received (MO)
</td><td valign=top align=right class=text>
  Inbound (MO)
</td><td valign=top align=right class=text>
  Sent (MT)
</td><td valign=top align=right class=text>
  Outbound (MT)
</td><td valign=top align=right class=text>
  Queued (MO)
</td><td valign=top align=right class=text>
  Queued (MT)
</td></tr>
<tr><td valign=top align=right class=text>
    <?php

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $mo[$inst] = XPathValue("gateway/sms/received/total", $status[$inst]);
            $sum += $mo[$inst];
            echo "($inst) <b>".nf($mo[$inst])."</b> msgs<br />\n";
            
        }
        echo "<hr size=1>\n";
        echo "(all) <b>".nf($sum)."</b> msgs<br />\n";
    ?>
</td><td valign=top align=right class=text>
    <?php

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $in[$inst] = XPathValue("gateway/sms/inbound", $status[$inst]);
            $sum += $in[$inst];
            echo "($inst) <b>".nfd($in[$inst])."</b> msgs/s<br />\n";
            
        }
        echo "<hr size=1>\n";
        echo "(all) <b>".nfd($sum)."</b> msgs/s<br />\n";
    ?>
</td><td valign=top align=right class=text>
    <?php

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $mt[$inst] = XPathValue("gateway/sms/sent/total", $status[$inst]);
            $sum += $mt[$inst];
            echo "($inst) <b>".nf($mt[$inst])."</b> msgs<br />\n";
            
        }
        echo "<hr size=1>\n";
        echo "(all) <b>".nf($sum)."</b> msgs<br />\n";
    ?>
</td><td valign=top align=right class=text>
    <?php

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $out[$inst] = XPathValue("gateway/sms/outbound", $status[$inst]);
            $sum += $out[$inst];
            echo "($inst) <b>".nfd($out[$inst])."</b> msgs/s<br />\n";
            
        }
        echo "<hr size=1>\n";
        echo "(all) <b>".nfd($sum)."</b> msgs/s<br />\n";
    ?>
</td><td valign=top align=right class=text>
    <?php

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $mo_q[$inst] = XPathValue("gateway/sms/received/queued", $status[$inst]);
            $sum += $mo_q[$inst];
            echo "($inst) ".nf($mo_q[$inst])." msgs<br />\n";
            
        }
        echo "<hr size=1>\n";
        echo "(all) ";
        if ($sum > $CONST_QUEUE_ERROR) {
            echo "<span class=red>".nf($sum)." msgs</span>";
        } else {
            echo nf($sum)." msgs";
        }
        echo " <br />\n";
    ?>
</td><td valign=top align=right class=text>
    <?php

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $mt_q[$inst] = XPathValue("gateway/sms/sent/queued", $status[$inst]);
            $sum += $mt_q[$inst];
            echo "($inst) ".nf($mt_q[$inst])." msgs<br />\n";
            
        }
        echo "<hr size=1>\n";
        echo "(all) ";
        if ($sum > $CONST_QUEUE_ERROR) {
            echo "<span class=red>".nf($sum)." msgs</span>";
        } else {
            echo nf($sum)." msgs";
        }
        echo " <br />\n";
    ?>
</td></tr>
</table>
</p>

<h4>Box connections</h4>

<p id=bord>
<table width=100% cellspacing=0 cellpadding=1 border=0>
<tr><td valign=top align=center class=text>
  Instance
</td><td valign=top class=text>
  Type
</td><td valign=top class=text>
  ID
</td><td valign=top class=text>
  IP
</td><td valign=top align=right class=text>
  Queued (MO)
</td><td valign=top align=right class=text>
</td><td valign=top class=text>
  Started
</td><td valign=top class=text>
  SSL
</td></tr>
    <?php 

        foreach ($configs as $inst => $config) {
            $x = XPathValue("gateway/boxes", $status[$inst]);
            $x = trim($x); // the boxes number sometimes returns a few blank spaces
            /* drop an error in case we have no boxes connected */
            if (empty($x)) {
                        echo "<tr><td valign=top align=center class=text>\n";
                echo "($inst)";
                        echo "</td><td valign=top align=left colspan=4 class=text>\n";
                echo "<span class=red><b>no boxes connected to this bearerbox!</b></span> <br /> \n";
                        echo "</td></tr>\n";
            } else {
                /* loop the boxes */          
                $i = 0;
                while (($y = XPathValue("box", $x)) != "") {
                    $i++;
        				  echo "<tr><td valign=top align=center class=text>\n";
        		        echo "($inst)";
        				  echo "</td><td valign=top align=left class=text>\n";
                    echo "<b>".XPathValue("type", $y)."</b>";
						  echo "</td><td valign=top class=text nowrap>\n";
                    echo XPathValue("id", $y);
						  echo "</td><td valign=top class=text nowrap>\n";
                    echo XPathValue("IP", $y);
						  echo "</td><td valign=top align=right class=text nowrap>\n";
                    echo "<b>".XPathValue("queue", $y)."</b> msgs";
						  echo "</td><td valign=top nowrap></td>";
						  echo "<td valign=top class=text nowrap>\n";
                    if (ereg("on-line (.*)d (.*)h (.*)m (.*)s", XPathValue("status", $y), $regs)) {
                        $ts = ($regs[1]*24*60*60) + ($regs[2]*60*60) 
                            + ($regs[3]*60) + $regs[4];
                        echo date("Y-m-d H:i:s", mktime()-$ts).", ";
                        echo "uptime $regs[1]d $regs[2]h $regs[3]m $regs[4]s";
                    }
						  echo "</td><td valign=top class=text nowrap>\n";
                    echo XPathValue("ssl", $y);
						  echo "</td></tr>\n";
                    $a = substr($x, strpos($x, "</box>") + 6);
                    $x = $a;
                }
            }
        }

    ?>
</table>
</p>

<h4>SMSC connections</h4>

<p id=bord>
<table width=100% cellspacing=0 cellpadding=5 border=0>
<tr><td valign=top align=right class=text>
  Links
</td><td valign=top align=right class=text>
  Online
</td><td valign=top align=right class=text>
  Disconnected
</td><td valign=top align=right class=text>
  Connecting
</td><td valign=top align=right class=text>
  Re-connecting
</td><td valign=top align=right class=text>
  Dead
</td><td valign=top align=right class=text>
  Unknown
</td></tr>
<tr><td valign=top align=right class=text>
    <?php 

        $sum = 0;
        foreach ($configs as $inst => $config) {
            echo "($inst) ";
            if (!empty($status[$inst])) {
                $links[$inst] = XPathValue("gateway/smscs/count", $status[$inst]);
                $sum += $links[$inst];
                echo $links[$inst]." links";
            } else {
                echo "none";
            }
            echo "<br />\n";
        }
        echo "<hr size=1>\n";
        echo "(all) $sum links <br />\n";

    ?>
</td><td valign=top align=right class=text>
    <?php 

        $sum = 0;
        echo "<span class=green>";
        foreach ($configs as $inst => $config) {
            echo "($inst) ";
            if (!empty($status[$inst])) {
                $x = check_status("online", $status[$inst]);
                $sum += $x;
                echo ($links[$inst] == $x ? "<b>all</b> links" : "$x links");
            } else {
                echo "none";
            }
            echo "<br />\n";
        }
        echo "<hr size=1>\n";
        echo "(all) $sum links <br />\n";
        echo "</span>\n";

    ?>
</td><td valign=top align=right class=text>
    <?php 

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $x = check_status("disconnected", $status[$inst]);
            $sum += $x;
            echo "($inst) ";
            if ($x == 0) {
                echo "<span class=text>none</span>";
            } else {
                echo "<a href=\"#\" class=href onClick=\"do_alert('";
                echo "smsc-ids in disconnected state are\\n\\n";
                echo get_smscids("disconnected", $status[$inst]);
                echo "');\"><span class=red><b>$x</b> links</span></a>";
            }
            echo "<br />\n";
        }
        echo "<hr size=1>\n";
        echo "(all) $sum links <br />\n";

    ?>
</td><td valign=top align=right class=text>
    <?php 

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $x = check_status("connecting", $status[$inst]);
            $sum += $x;
            echo "($inst) ";
            if ($x == 0) {
                echo "<span class=text>none</span>";
            } else {
                echo "<a href=\"#\" class=href onClick=\"do_alert('";
                echo "smsc-ids in connecting state are\\n\\n";
                echo get_smscids("connecting", $status[$inst]);
                echo "');\"><span class=red><b>$x</b> links</span></a>";
            }
            echo "<br />\n";
        }
        echo "<hr size=1>\n";
        echo "(all) $sum links <br />\n";

    ?>
</td><td valign=top align=right class=text>
    <?php 

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $x = check_status("re-connecting", $status[$inst]);
            $sum += $x;
            echo "($inst) ";
            if ($x == 0) {
                echo "<span class=text>none</span>";
            } else {
                echo "<a href=\"#\" class=href onClick=\"do_alert('";
                echo "smsc-ids in re-connecting state are\\n\\n";
                echo get_smscids("re-connecting", $status[$inst]);
                echo "');\"><span class=red><b>$x</b> links</span></a>";
            }
            echo "<br />\n";
        }
        echo "<hr size=1>\n";
        echo "(all) $sum links <br />\n";

    ?>
</td><td valign=top align=right class=text>
    <?php 

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $x = check_status("dead", $status[$inst]);
            $sum += $x;
            echo "($inst) ";
            if ($x == 0) {
                echo "<span class=text>none</span>";
            } else {
                echo "<a href=\"#\" class=href onClick=\"do_alert('";
                echo "smsc-ids in dead state are\\n\\n";
                echo get_smscids("dead", $status[$inst]);
                echo "');\"><span class=text><b>$x</b> links</span></a>";
            }
            echo "<br />\n";
        }
        echo "<hr size=1>\n";
        echo "(all) $sum links <br />\n";

    ?>
</td><td valign=top align=right class=text>
    <?php 

        $sum = 0;
        foreach ($configs as $inst => $config) {
            $x = check_status("unknown", $status[$inst]);
            $sum += $x;
            echo "($inst) ";
            if ($x == 0) {
                echo "<span class=text>none</span>";
            } else {
                echo "<a href=\"#\" class=href onClick=\"do_alert('";
                echo "smsc-ids in unknown state are\\n\\n";
                echo get_smscids("unknown", $status[$inst]);
                echo "');\"><span class=text><b>$x</b> links</span></a>";
            }
            echo "<br />\n";
        }
        echo "<hr size=1>\n";
        echo "(all) $sum links <br />\n";

    ?>
</td><td valign=top align=right class=text>
</td></tr>
</table>
</p>


<?php

    if (!empty($HTTP_GET_VARS[details])) {
?>

<h4>SMSC connection details</h4>

<p id=bord>
<table width=100% cellspacing=0 cellpadding=1 border=0>
<tr><td width=10% valign=top class=text>
  Instance
</td><td valign=top class=text>
  SMSC-ID
</td><td valign=top class=text>
  Status
</td><td valign=top class=text>
  Started
</td><td valign=top align=right class=text>
  Received (MO)
</td><td valign=top align=right class=text>
  Sent (MT)
</td><td valign=top align=right class=text>
  Failed (MT)
</td><td valign=top align=right class=text>
  Queued (MT)
</td><td valign=top align=right class=text>
  Admin
</td></tr>
<?php 

    foreach ($configs as $inst => $config) {
        smsc_details($inst, $status[$inst]);
    }
?>
</table>
</p>

<?php

    } else {
        echo "<a class=href href=\"".$_SERVER[REQUEST_URI];
        if (strpos($_SERVER[REQUEST_URI], "?") > 0) {
            echo "&details=1";
        } else {
            echo "?details=1";
        }
        echo "\">SMSC connection details</a>\n";
    }

?>

</body>
</html>

