<?php

/***********************************************************************

  webtty.php - server side script used to control and transfer data
               between server process and client webpage

  This file is part of the webtty package. Latest version can be found
  at the homepage at http://testape.com/webtty_sample.html
  The webtty package was written by Martin Steen Nielsen.

  webtty is free software; you can redistribute it and/or modify it 
  under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  webtty is distributed in the hope that it will be useful,but 
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with webtty; if not, write to the Free Software Foundation, 
  Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

***********************************************************************/

$temp_dir="/data/webtty_log/";



/***********************************************************************
  creates a new administration process
 ***********************************************************************/
function admin_process_create( )
    {
    global $temp_dir;
    $count = 0;
    $id = md5($time . rand() );
    $cmd = "./webtty " . $temp_dir . $id . " " . $temp_dir . "dummy tail -n0 -f /data/webtty_log/admin   2>/dev/null >&- <&- >/dev/null &";
    system($cmd); 
    /* wait until all fifo's are created */
    while ( $count<10 )
        {
	if (file_exists($temp_dir . $id . "_out"))
	     { break; }
	 sleep(1); 
	 $count++;
	};

    /* Check if we have waited to long */
    if ($count == 10)
        { return 0; }
	
    return $id;
    }


/***********************************************************************
   creates a new session with a shell
 ***********************************************************************/
function virtual_shell_create( )
    {
    global $temp_dir;
    $count = 0;
    $id = md5($time . rand() );
    $cmd = "./webtty " . $temp_dir . $id . " " . $temp_dir . "/admin ./webtty.sh " . $id . " 2>/dev/null >&- <&- >/dev/null &";
    system($cmd); 
    /* wait until all fifo's are created */
    while ( $count<10 )
        {
	if (file_exists($temp_dir . $id . "_out"))
	     { break; }
	 sleep(1); 
	 $count++;
	};

    /* Check if we have waited to long */
    if ($count == 10)
        { return 0; }
	
    return $id;
    }



/***********************************************************************
   deletes a session - kills shell
 ***********************************************************************/
function shell_delete($id)
    {
    global $temp_dir;
    if (file_exists($temp_dir . $id . "_pid"))
        {
        $pid = file($temp_dir . $id . "_pid");
        exec ("kill " . $pid[0] );
        }
    flush();
    }
  

/***********************************************************************
   forwards data to shell
 ***********************************************************************/
function shell_put($ids, $cmd)
    {
    global $temp_dir;
    $inp = $temp_dir . $ids . "_in";
    $fil = fopen($inp, "w"); 
    fputs($fil,$cmd); 
    fclose($fil);
    flush();
    }



/***********************************************************************
   outputs data from shell
 ***********************************************************************/
function shell_get($ids)
    {
    global $temp_dir;
    
    $outp = $temp_dir . $ids . "_out";

    if (file_exists($outp))
        {             
    	readfile($outp);
        }	
    else
        {
	echo "_C_L_O_S_E_D_";
	}

    flush();
    }

/* initialize session system and close immediately to avoid hanging later */
session_start(); 
session_write_close ();

define_syslog_variables();
openlog("webtty", LOG_PID | LOG_PERROR, LOG_LOCAL0);

syslog(LOG_WARNING,$_POST['inp']);

/* get input  */
$shell=$_GET['shell'];

if (isset($_POST['id']))
  { $ids = $_POST['id']; }
else if (isset($_GET['id']))
  { $ids = $_GET['id']; }
else
  { $ids=''; }
  
$inp=(isset($_POST['inp']))?$_POST['inp']:''; 

$outp=(isset($_GET['outp']))?$_GET['outp']:''; 

syslog(LOG_WARNING,"inp=" . $inp . " outp=" . $outp . " id=" . $id );

if ( 'kill' == $shell )
    {
    shell_delete($ids);
    }
else if ( 'virt' == $shell )
    {
    $id = virtual_shell_create();
    
    /* create new process */
    if ($id != '0')
        {
        echo "<SPAN id=webtty_session_id style='display:none'>" . $id . "</SPAN>\n";
	  echo "<TEXTAREA ID=webtty_output class=webtty READONLY></TEXTAREA>\n";
	  }
    }
else if ( 'admin' == $shell )
    {
    /* create new process */
    $id = admin_process_create();
    
    if ($id != '0')
        {
        echo "<SPAN id=webtty_session_id style='display:none'>" . $id . "</SPAN>\n";
  	echo "<DIV ID=webtty_output></DIV>\n";
	}
    }
else
    {
    if ($inp != '')
        {
        /* Yes - process input */
        shell_put($ids,$inp);
        } 
    else if ($outp != '' )
        {
        /* Dump the output */
        shell_get($ids); 
        }
    }

