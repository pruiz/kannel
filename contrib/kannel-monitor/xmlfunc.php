<?php

/*
 * xmlfunc.php -- Kannel's XML status output parsing functions.
 */

if (empty($XMLFUNC_PHP)) {
    $XMLFUNC_PHP = 1;


function startElement($parser, $name, $attrs) {
    global $depth;
    for ($i = 0; $i < $depth[$parser]; $i++) {
        print "  ";
    }
    print "$name\n";
    $depth[$parser]++;
}


function endElement($parser, $name) {
    global $depth;
    $depth[$parser]--;
}


function GetElementByName($xml, $start, $end) {
    $startpos = strpos($xml, $start);
    if ($startpos === false) {
        return false;
    }
    $endpos = strpos($xml, $end);
    $endpos = $endpos + strlen($end); 
    $endpos = $endpos - $startpos;
    $endpos = $endpos - strlen($end);
    $tag = substr($xml, $startpos, $endpos);
    $tag = substr($tag, strlen($start));
    return $tag;
}


function XPathValue($xpath, $xml) {
    $XPathArray = explode("/", $xpath);
    $node = $xml;
    while (list($key, $value) = each($XPathArray)) {
        $node = GetElementByName($node, "<$value>", "</$value>"); 
    }
  
    return $node;
}


function nf($number) {
    return number_format($number, 0, ",", ".");
}

function nfd($number) {
    return number_format($number, 2, ",", ".");
}


function check_status($status, $xml) { 

    $x = XPathValue("gateway/smscs", $xml);
    /* loop the smsc */ 
    $i = 0;
    $n = 0;
    while (($y = XPathValue("smsc", $x)) != "") {
        $i++;
        if (substr(XPathValue("status", $y), 0, strlen($status)) == $status) {
           $n++;
        }
        $a = substr($x, strpos($x, "</smsc>") + 7);
        $x = $a;
    }

    return $n;
}

function get_smscids($status, $xml) { 

    $x = XPathValue("gateway/smscs", $xml);
    /* loop the smsc */ 
    $i = 0;
    $n = "";
    while (($y = XPathValue("smsc", $x)) != "") {
        $i++;
        if (substr(XPathValue("status", $y), 0, strlen($status)) == $status) {
           $n .= XPathValue("id", $y)." ";
        }
        $a = substr($x, strpos($x, "</smsc>") + 7);
        $x = $a;
    }

    return $n;
}

function smsc_details($inst, $xml) { 
    global $config;
    
    $x = XPathValue("gateway/smscs", $xml);
    /* loop the smsc */ 
    $i = 0;
    while (($y = XPathValue("smsc", $x)) != "") {
        $i++;
        echo "<tr><td valign=top align=center class=text>\n";
        echo "($inst)";
        echo "</td><td valign=top class=text>\n";
        $smsc = XPathValue("id", $y);
        echo "<b>".$smsc."</b> <br />";
        echo XPathValue("name", $y)." <br />";
        if (ereg("online (.*)s", XPathValue("status", $y), $regs)) {
            $z = $regs[1];
            echo "started ".date("Y-m-d H:i:s", mktime()-$z);
        }

        echo "</td><td valign=top class=text nowrap>\n";
        $a = explode(" ", XPathValue("status", $y));
        switch ($a[0]) {
            case "online":
                echo "<span class=green>online</span> <br />";
                echo " (".$z."s)";
                break;
            case "disconnected":
                echo "<span class=red>disconnected</span>";
                break;
            case "connecting":
                echo "<span class=red>connecting</span>";
                break;
            case "re-connecting":
                echo "<span class=red>re-connecting</span>";
                break;
            case "dead":
                echo "<span class=text>dead</span>";
                break;
            case "unkown":
                echo "<span class=text>unknown</span>";
                break;

        }
        echo "</td><td valign=top align=right class=text nowrap>\n";
        echo nf(XPathValue("received", $y));
        echo "</td><td valign=top align=right class=text nowrap>\n";
        echo nf(XPathValue("sent", $y));
        echo "</td><td valign=top align=right class=text nowrap>\n";
        echo nf(XPathValue("failed", $y));
        echo "</td><td valign=top align=right class=text nowrap>\n";
        echo nf(XPathValue("queued", $y));
        echo "</td><td valign=top align=right class=text nowrap>\n";
        echo "<a class=href href=\"#\" onClick=\"admin_smsc_url('stop-smsc', '";
        echo $config["base_url"]."/stop-smsc?password=".$config["passwd"]."&smsc=$smsc','";
        echo "$smsc');\">stop</a> <br />";
        echo "<a class=href href=\"#\" onClick=\"admin_smsc_url('start-smsc', '";
        echo $config["base_url"]."/start-smsc?password=".$config["passwd"]."&smsc=$smsc','";
        echo "$smsc');\">start</a>";
        echo "</td></tr>\n";
        $a = substr($x, strpos($x, "</smsc>") + 7);
        $x = $a;
    }

    return $n;
}


} /* XMLFUNC_PHP */
	    
?>
