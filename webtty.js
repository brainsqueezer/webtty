/***********************************************************************

  webtty.js - client side script used to display output from server and 
              to send keypresses to server

  This file is part of the webtty package. The webtty package was
  written by Martin Steen Nielsen

  Development is hosted by testape.com at www.testape.com/webtty

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



/***********************************************************************
   Resize terminal
 ***********************************************************************/

function resize(id)
    {
    var elem = document.getElementById(id);
    elem.cols+=20;
    elem.rows+=6;
    if (elem.cols>20*4) {
        elem.cols-=(20*4); elem.rows-=(6*4); }
    elem.scrollTop=elem.scrollHeight;
    return false;
    }


/***********************************************************************
   Toggles scrollbars on terminal
 ***********************************************************************/
function bars(id)
    {
    var elem = document.getElementById(id);
    if (elem.style.overflow!='scroll')
        elem.style.overflow='scroll';
    else
        elem.style.overflow='hidden';
    return false;
    }


/***********************************************************************
   closes terminal
 ***********************************************************************/
function wclose(id)
    {
    var elem = document.getElementById(id);
    elem.style.display='none';
    return false;
    }




/***********************************************************************
   constructor webtty is called from client. Constructor expects to 
   receive id of element that will hold the html for the new process. 
   Function exit_func(err) is optional, but if given, it will be called 
   on errors or if the server terminates the process early.

   Sample usage -->

   <html>
     <script type="text/javascript" src="webtty.js">
     <body onLoad="new webtty('webtty_element');">
       <div id="webtty_element"></div>
     </body>
   </html>


 ***********************************************************************/

function webtty(receiving_element_id, exit_func)
    {

    /***********************************************************************
       Creates XMLHTTPRequest object
     ***********************************************************************/
    this.create_transfer_object = function()
       {
       try { return new ActiveXObject('Msxml2.XMLHTTP');    } catch(e) {}
       try { return new ActiveXObject('Microsoft.XMLHTTP'); } catch(e) {}
       try { return new XMLHttpRequest();                   } catch(e) {}
       return null;
       }

    /***********************************************************************
       callback function whenever html data is ready
     ***********************************************************************/
    this.get_html_state = function()
        {
        if (this.xhr_out.readyState == 4)
            {
            if ((this.xhr_out.status!=200)&&(this.xhr_out.status!=0))
                this.error();
            this.element.innerHTML = this.xhr_out.responseText;
            this.session_id  = document.getElementById('webtty_session_id').innerHTML;
            document.getElementById('webtty_session_id').id='default';
            this.output_area = document.getElementById('webtty_output');
            this.output_area.id = 'default';
            var obj = this;
	    this.output_area.onkeypress = function(e) { if (obj.running) return obj.key_event(e); }
            this.output_area.onkeydown  = function(e) { if (obj.running) return obj.special_key_event(e); }
	    this.get_data();
            }
        }

    /***********************************************************************
       Request a new process and HTML data from server
     ***********************************************************************/
    this.get_html = function()
        {
        var obj = this;
        if (null == this.xhr_out)
          this.xhr_out     = this.create_transfer_object();
        this.xhr_out.onreadystatechange = function() {};
        this.xhr_out.open('GET', 'webtty.php?shell=virt', true);
        this.xhr_out.onreadystatechange = function() { if (obj.running) obj.get_html_state(); }
        this.xhr_out.send('');
        }




    /***********************************************************************
       callback function whenever process data is ready
     ***********************************************************************/
    this.get_data_state = function()
        {
	if (this.xhr_out.readyState == 4)
            {
            if ((this.xhr_out.status!=200)&&(this.xhr_out.status!=0))
                this.error();
            var data = this.xhr_out.responseText; 
            if (data == '_C_L_O_S_E_D_')
                {
                this.session_id=null;
                if (undefined != this.webtty_exit)
                    this.webtty_exit(0);
                this.cleanup();
                }
            else 
               {
               if (data != '')
                  this.handle_data(data);
	       this.get_data();
               }
            }
        }


    /***********************************************************************
       Request new data from the server
     ***********************************************************************/
    this.get_data = function()
        {
        if (null == this.session_id)
            return this.error();
        if (null == this.xhr_out)
          this.xhr_out     = this.create_transfer_object();
        var obj = this;
        this.xhr_out.onreadystatechange = function() { }
        this.xhr_out.open('GET', 'webtty.php?outp=1&id='+this.session_id, true);
        this.xhr_out.onreadystatechange = function() { return obj.get_data_state(); }
        this.xhr_out.send('');
        }


    /***********************************************************************
       exit page. Remove handles and terminate session
     ***********************************************************************/
    this.cleanup = function()
        {
        this.running     = false;
	if (this.xhr_kbd)
            {
            this.xhr_kbd.onreadystatechange = function() {};
            this.xhr_kbd.abort();
            delete this.xhr_kbd;
            }
        if (this.xhr_out)
            {
            this.xhr_out.onreadystatechange = function() {};
            this.xhr_out.abort();
            delete xhr_out;
            }
        if (null != this.session_id)
            {
            var exit_req = this.create_transfer_object();
            exit_req.open('GET', 'webtty.php?id='+this.session_id+'&shell=kill', true);
            exit_req.send('');
            delete exit_req;
  	    this.session_id=null;
            }
        }
    
    /***********************************************************************
       handles process data
     ***********************************************************************/
    this.handle_data = function(data)
        {
        this.output_area.value = this.handle_special_chars(this.output_area.value + data, 2*data.length);
        this.output_area.scrollTop=this.output_area.scrollHeight;
        }

    /***********************************************************************
       Handle special characters. Simulates applying backspace, bell etc to 
       a string
     ***********************************************************************/
    this.handle_special_chars = function(streng, max_look_back)
        {
        var startpos=0;
        var write_pos=0; 
        var new_str='';
        var subs='';

        if (max_look_back>80)
            max_look_back=80;
        
        if (streng.length>max_look_back)
            startpos = streng.length-max_look_back;

        for (tl=startpos; tl<streng.length; tl++)
            {
            if (streng.charCodeAt(tl)==8)
                { write_pos--; subs = subs.substr(0,write_pos); }
            else if (streng.charCodeAt(tl)==7)
                { ; }
            else
                { write_pos++; subs += streng.charAt(tl); }
            }
        return streng.substr(0,startpos)+subs;
        }


    /***********************************************************************
       callback function whenever data has been transmitted
     ***********************************************************************/
    this.put_kbd_state = function()
        {
        if (this.xhr_kbd.readyState == 4)
            {
      	    delete this.xhr_kbd; 
	    this.put_kbd();
            }
        }
	


    /***********************************************************************
       send data to server
     ***********************************************************************/
    this.put_kbd = function()
        {
        if ('' == this.kbd_buffer)
            return;

	this.xhr_kbd=this.create_transfer_object();
        this.xhr_kbd.open('POST', "webtty.php", true);
        var obj = this;
        this.xhr_kbd.onreadystatechange = function() { if (obj.running) return obj.put_kbd_state(); }
        this.xhr_kbd.setRequestHeader("Connection", "close")
        this.xhr_kbd.setRequestHeader("Content-Type", "application/x-www-form-urlencoded")
        this.xhr_kbd.send("id="+this.session_id+"&inp="+this.kbd_buffer);
        this.kbd_buffer='';
        }


    /***********************************************************************
      buffer data and send to server
     ***********************************************************************/
    this.buffer_next_data = function(cmd)
        {
        this.kbd_buffer += cmd;
        if (undefined == this.xhr_kbd) 
            this.put_kbd();
        }



    /***********************************************************************
       handles normal keypresses
     ***********************************************************************/
    this.key_event = function(e)
        {
        if (!e) var e = window.event
        if (e.keyCode) key = e.keyCode;
        else if (e.which) key = e.which;

        if (!(window.event || e.charCode))
          return true;

        this.buffer_next_data(escape(String.fromCharCode(key)));
        return false; 
        }


    /***********************************************************************
       handles special keypresses
     ***********************************************************************/
    this.special_key_event = function (e)
        {
	if (!e) var e = window.event
        if (e.keyCode) key = e.keyCode;
        else if (e.which) key = e.which;

        if (key==9)
            { 
            this.buffer_next_data(escape("\t")); 
            return false; 
            }
        else if (key==8)
            { 
            this.buffer_next_data(escape("\010")); 
            return false; 
            }
        else if (key == 40)
            { 
            this.buffer_next_data(escape("\033[B")); 
            return false; 
            }
        else if (key == 38)
            { 
            this.buffer_next_data(escape("\033[A")); 
            return false; 
            }
        else if (key == 38)
            { 
            this.buffer_next_data(escape("\033[A")); 
            return false; 
            }
        else if (key == 13)
            { 
            this.buffer_next_data(escape("\n")); 
            return false; 
            }
        }


    /***********************************************************************
       handles errors
     ***********************************************************************/
    this.error = function()
        {
        if (undefined != this.webtty_exit)
            this.webtty_exit(1);
        if (this.element)
	   this.element.innerHTML="Error";
	this.cleanup();
        }


    this.element     = document.getElementById(receiving_element_id);
    if (null == this.element)
        return this.error();

    this.running     = true;
    this.session_id  = null;
    this.output_area = null;
    this.kbd_buffer  = '';
    this.webtty_exit = exit_func;
    }


















/***********************************************************************
   constructor webtty_admin is called from client. Constructor expects 
   to receive id of element that will hold the html for the new process. 
   Function exit_func(err) is optional, but if given, it will be called 
   on errors or if the server terminates the process early.

   Sample usage -->

   <html>
     <script type="text/javascript" src="webtty.js">
     <body onLoad="new webtty_admin('webtty_element');">
       <div id="webtty_element"></div>
     </body>
   </html>


 ***********************************************************************/

function webtty_admin(receiving_element_id, exit_func)
    {

    this.init_super = webtty;
    this.init_super(receiving_element_id, exit_func);
    this.save = '';
    delete this.xhr_kbd;
    

    /***********************************************************************
       adds new clone terminal
     ***********************************************************************/

    this.add_clone = function(data_id)
        {
        if (null == this.output_area)
            this.error(1);

        this.output_area.innerHTML +=
          ( "<span id=s"+data_id+" style='float:left;'>" +
            "  &nbsp;&nbsp;" +
	    "  <a id=close"+data_id+" href='#' onClick='return wclose(\"s" + data_id + "\");'>close</a>&nbsp;&nbsp;" +
            "  <a id=size" +data_id+" href='#' onClick='return resize(\"t" + data_id + "\");'>size</a>&nbsp;&nbsp;"  +
            "  <a id=bars" +data_id+" href='#' onClick='return bars  (\"t" + data_id + "\");'>bars</a>"  +
            "  " +
            "  <br>&nbsp;&nbsp;<textarea rows=12 cols=40 class=webtty_clone id='t" + data_id + "'></textarea>" +
            "</span>" );

        return document.getElementById('t'+data_id);
        }


    /***********************************************************************
       Request a new process and HTML data from server - 
       replaces superclass function
     ***********************************************************************/
    this.get_html = function()
        {
        var obj = this;
        if (null == this.xhr_out)
          this.xhr_out     = this.create_transfer_object();
        this.xhr_out.open('GET', 'webtty.php?shell=admin', true);
        this.xhr_out.onreadystatechange = function() { obj.get_html_state(); }
        this.xhr_out.send('');
        }

    /***********************************************************************
       handles interpretation of admin data from admin process - 
       replaces superclass function
     ***********************************************************************/
    this.handle_data = function(data)
        {
	var nxt = 0;
        var elem;
        if ((data.charCodeAt(5)==58) && (data.charCodeAt(10)==58) )
	  this.save='';
	this.save+=data;
        while (1+nxt<this.save.length)
            {
            if ((this.save.charCodeAt(5)==58) && (this.save.charCodeAt(10)==58) )
                {
                var data_id   = this.save.substr(nxt,5);
                var data_size = 1*this.save.substr(nxt+6,4);
                var data_text = this.save.substr(nxt+11,data_size);

                if ( nxt+11+data_size > this.save.length)
                    { this.save=this.save.substr(nxt); break;}
                elem = document.getElementById('t'+data_id);
                if (elem == null)
                    elem = this.add_clone(data_id);
       
                elem.textContent += data_text;
                elem.value += data_text;

                elem.scrollTop=elem.scrollHeight;
 
                nxt += (11+data_size);
                }
            else
                nxt == this.save.length;
            }
        if (nxt == this.save.length)
            this.save='';
        }

    /***********************************************************************
       handles normal keypresses - replaces superclass function
     ***********************************************************************/
    this.key_event = function(e)
        {
        return true;
        }


    /***********************************************************************
       handles special keypresses - replaces superclass function
     ***********************************************************************/
    this.special_key_event = function (e)
        {
        return true;
        }

    }

