
function admin_url(command, url) { 
    check = confirm("Are you sure you want to '"+command+"' bearerbox from this Kannel instance?");
    if (check == true) {
        admin_window = window.open("","newWin","");
        admin_window.location.href = url;
    }
    location.reload();
    self.focus();
}

function admin_smsc_url(command, url, smsc) { 
    check = confirm("Are you sure you want to '"+command+"' the smsc-id '"+smsc+"' on the Kannel instance?");
    if (check == true) {
        admin_window = window.open("","newWin","");
        admin_window.location.href = url;
    }
    location.reload();
    self.focus();
}

function do_alert(text) {
    alert(text);
}
