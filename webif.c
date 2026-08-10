//
// Web interface code for Mike's System Monitor.
//
// Copyright © 2026 by Michael R Sweet
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#include "msysmon.h"
#include <stdarg.h>
#include "msysmon_png.h"


//
// Local functions...
//

static char	*get_datetime(time_t t, int secs, char *buffer, size_t bufsize);

static bool	html_escape(http_t *http, const char *s, size_t slen);
static bool	html_footer(http_t *http);
static bool	html_header(http_t *http, const char *title, int refresh);
static bool	html_printf(http_t *http, const char *format, ...);
static bool	html_puts(http_t *http, const char *s);

static bool	send_favicon_png(http_t *http);
static bool	send_history_svg(http_t *http, char *options);
static bool	send_html_report(http_t *http, char *options);
static bool	send_http_response(http_t *http, http_status_t status, const char *content_type, size_t content_length, const char *message, ...);


//
// 'msysmonRunWebIf()' - Run a web interface thread.
//

void *					// O - Exit status
msysmonRunWebIf(http_t *http)		// I - Client connection
{
  http_state_t	state;			// HTTP request state
  http_status_t	status;			// HTTP status code
  char		resource[1024],		// Resource path
		*options;		// Pointer to options, if any


  // Loop reading requests...
  while ((state = httpReadRequest(http, resource, sizeof(resource))) != HTTP_STATE_ERROR)
  {
    // See if we have a request...
    if (state == HTTP_STATE_WAITING)
    {
      usleep(1);
      continue;
    }
    else if (state == HTTP_STATE_UNKNOWN_METHOD)
    {
      send_http_response(http, HTTP_STATUS_BAD_REQUEST, "text/plain", /*content_length*/0, "Bad/unknown operation.\n");
      break;
    }
    else if (state == HTTP_STATE_UNKNOWN_VERSION)
    {
      send_http_response(http, HTTP_STATUS_BAD_REQUEST, "text/plain", /*content_length*/0, "Bad HTTP version.\n");
      break;
    }
    else if (resource[0] != '/' && strcmp(resource, "*"))
    {
      send_http_response(http, HTTP_STATUS_BAD_REQUEST, "text/plain", /*content_length*/0, "Bad resource path '%s'.\n", resource);
      break;
    }

    if ((options = strchr(resource, '?')) != NULL)
      *options++ = '\0';

    // Read all HTTP requests...
    while ((status = httpUpdate(http)) == HTTP_STATUS_CONTINUE)
      ;

    if (status != HTTP_STATUS_OK)
    {
      send_http_response(http, HTTP_STATUS_BAD_REQUEST, "text/plain", /*content_length*/0, "Bad/missing request headers.\n");
      break;
    }

    switch (state)
    {
      case HTTP_STATE_OPTIONS :
          send_http_response(http, HTTP_STATUS_OK, /*content_type*/NULL, /*content_length*/0, /*message*/NULL);
          httpWrite(http, "", 0);
          break;

      case HTTP_STATE_HEAD :
          if (!strcmp(resource, "/"))
            send_http_response(http, HTTP_STATUS_OK, "text/html", /*content_length*/0, /*message*/NULL);
          else if (!strcmp(resource, "/apple-touch-icon.png") || !strcmp(resource, "/favicon.png"))
            send_http_response(http, HTTP_STATUS_OK, "image/png", sizeof(msysmon_png), /*message*/NULL);
          else if (!strcmp(resource, "/history.svg"))
            send_http_response(http, HTTP_STATUS_OK, "image/svg+xml", /*content_length*/0, /*message*/NULL);
          else
            send_http_response(http, HTTP_STATUS_NOT_FOUND, "text/plain", /*content_length*/0, /*message*/NULL);
          httpWrite(http, "", 0);
          break;

      case HTTP_STATE_GET :
          if (!strcmp(resource, "/"))
            send_html_report(http, options);
          else if (!strcmp(resource, "/apple-touch-icon.png") || !strcmp(resource, "/favicon.png"))
            send_favicon_png(http);
          else if (!strcmp(resource, "/history.svg"))
            send_history_svg(http, options);
          else
            send_http_response(http, HTTP_STATUS_NOT_FOUND, "text/plain", /*content_length*/0, "The resource '%s' is not available from this server.\n", resource);
          break;

      default :
          send_http_response(http, HTTP_STATUS_NOT_IMPLEMENTED, "text/plain", /*content_length*/0, "The '%s' method is not supported by this server.\n", httpStateString(state));
          goto close_client;
    }
  }

  // Cleanup and return...
  if (state == HTTP_STATE_ERROR && httpGetError(http) != EPIPE && httpGetError(http) != ECONNRESET && httpGetError(http))
    fprintf(stderr, "msysmon: Bad request line (%s).\n", strerror(httpGetError(http)));

  close_client:

  httpClose(http);

  return (NULL);
}


//
// 'get_datetime()' - Get a date/time string of the form YYYY-MM-DD HH:MM:SS
//

static char *				// O - Date/time string
get_datetime(time_t t,			// I - Time value
             int    secs,		// I - Number of seconds being displayed
             char   *buffer,		// I - String buffer
             size_t bufsize)		// I - Size of string buffer
{
  struct tm	date;			// Date value
  static const char * const days[] =	// Days
  {
    "Sun",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat"
  };


  // Get the local date and time...
  localtime_r(&t, &date);

  // Format it and return...
  if (secs <= 86400)
    snprintf(buffer, bufsize, "%02d:%02d:%02d", date.tm_hour, date.tm_min, date.tm_sec);
  else if (secs <= (7 * 86400))
    snprintf(buffer, bufsize, "%s %02d:%02d:%02d", days[date.tm_wday], date.tm_hour, date.tm_min, date.tm_sec);
  else
    snprintf(buffer, bufsize, "%04d-%02d-%02d %02d:%02d:%02d", date.tm_year + 1900, date.tm_mon + 1, date.tm_mday, date.tm_hour, date.tm_min, date.tm_sec);

  return (buffer);
}


//
// 'html_escape()' - Send a string to a web browser client.
//
// This function sends the specified string to the web browser client and
// escapes special characters as HTML entities as needed, for example "&" is
// sent as `&amp;`.
//

static bool				// O - `true` on success, `false` on error
html_escape(http_t     *http,		// I - Client connection
	    const char *s,		// I - String to write
	    size_t     slen)		// I - Number of characters to write (`0` for nul-terminated)
{
  bool		ret = true;		// Return value
  const char	*start,			// Start of segment
		*end;			// End of string


  start = s;
  end   = s + (slen > 0 ? slen : strlen(s));

  while (*s && s < end)
  {
    if (*s == '&' || *s == '<' || *s == '\"')
    {
      if (s > start)
        ret &= httpWrite(http, start, (size_t)(s - start)) > 0;

      if (*s == '&')
        ret &= httpWrite(http, "&amp;", 5) > 0;
      else if (*s == '<')
        ret &= httpWrite(http, "&lt;", 4) > 0;
      else
        ret &= httpWrite(http, "&quot;", 6) > 0;

      start = s + 1;
    }

    s ++;
  }

  if (s > start)
    ret &= httpWrite(http, start, (size_t)(s - start)) > 0;

  return (ret);
}


//
// 'html_footer()' - Show the web interface footer.
//
// This function sends the standard web interface footer followed by a
// trailing 0-length chunk to finish the current HTTP response.
//

static bool				// O - `true` on success, `false` on error
html_footer(http_t *http)		// I - Client connection
{
  bool		ret = true;		// Return value


  ret &= html_puts(http, "  </body>\n</html>\n");
  httpWrite(http, "", 0);

  return (ret);
}


//
// 'html_header()' - Show the web interface header and title.
//
// This function sends the standard web interface header and title.  If the
// "refresh" argument is greater than zero, the page will automatically reload
// after that many seconds.
//

static bool				// O - `true` on success, `false` on error
html_header(http_t     *http,		// I - Client connection
	    const char *title,		// I - Title
	    int        refresh)		// I - Refresh time in seconds (`0` for no refresh)
{
  bool		ret = true;		// Return value


  ret &= html_printf(http,
                     "<!DOCTYPE html>\n"
		     "<html>\n"
		     "  <head>\n"
		     "    <title>%s%sMike's System Monitor</title>\n"
		     "    <link rel=\"shortcut icon\" href=\"/favicon.png\" type=\"image/png\">\n",
		     title ? title : "", title ? " - " : "");

  if (refresh > 0)
    ret &= html_printf(http, "    <meta http-equiv=\"refresh\" content=\"%d\">\n", refresh);

  ret &= html_puts(http,
		   "    <style>\n"
		   "body {\n"
		   "  font-family: sans-serif;\n"
		   "  margin: 36pt 18pt;\n"
		   "}\n"
		   "table {\n"
		   "  border-bottom: black 1px solid;\n"
		   "  border-collapse: collapse;\n"
		   "  width: 100%;"
		   "}\n"
		   "thead tr th {\n"
		   "  border-bottom: 2px black solid;\n"
		   "}\n"
		   "tbody tr:nth-child(odd) {\n"
		   "  background: #dddddd;\n"
		   "}\n"
		   "tbody tr:nth-child(even) {\n"
		   "  background: #eeeeee;\n"
		   "}\n"
		   "tbody tr td {\n"
		   "  border-right: gray 1px solid;\n"
		   "  padding: 2px 5px;\n"
		   "  text-align: center;\n"
		   "}\n"
		   "tbody tr td:last-child {\n"
		   "  border-right: none;\n"
		   "}\n"
		   "a:link {\n"
		   "  text-decoration: none;\n"
		   "}\n"
		   "div.modal {\n"
		   "  background: rgba(255,255,255,0.9);\n"
		   "  border: gray 1px solid;\n"
		   "  box-shadow: 2px 2px 3px rgba(0,0,0,0.5);\n"
		   "  display: none;\n"
		   "  left: 10%;\n"
		   "  overflow: auto;\n"
		   "  padding: 0 10px 10px;\n"
		   "  position: absolute;\n"
		   "  width: 80%;\n"
		   "  z-index: 1;\n"
		   "}\n"
		   "span.mclose {\n"
		   "  color: black;\n"
		   "  cursor: pointer;\n"
		   "  float: right;\n"
		   "  font-size: 200%;\n"
		   "  font-weight: bold;\n"
		   "  text-decoration: none;\n"
		   "}\n"
		   "    </style>\n"
		   "    <script>\n"
		   "function close_modal(name) {\n"
		   "  document.getElementById(name).style.display = 'none';\n"
		   "}\n"
		   "function open_modal(name) {\n"
		   "  document.getElementById(name).style.display = 'block';\n"
		   "}\n"
		   "    </script>\n");

  ret &= html_puts(http, "  </head>\n  <body>\n");

  return (ret);
}


//
// 'html_printf()' - Send formatted text to the web browser client,
//                   escaping as needed.
//
// This function sends formatted text to the web browser client using
// `printf`-style formatting codes.  The format string itself is not escaped
// to allow for embedded HTML, however strings inserted using the '%c' or `%s`
// codes are escaped properly for HTML - "&" is sent as `&amp;`, etc.
//

static bool				// O - `true` on success, `false` on error
html_printf(http_t     *http,		// I - Client connection
	    const char *format,		// I - Printf-style format string
	    ...)			// I - Additional arguments as needed
{
  bool		ret = true;		// Return value
  va_list	ap;			// Pointer to arguments
  const char	*start;			// Start of string
  char		size,			// Size character (h, l, L)
		type;			// Format type character
  int		width,			// Width of field
		prec;			// Number of characters of precision
  char		tformat[100],		// Temporary format string for snprintf()
		*tptr,			// Pointer into temporary format
		temp[1024];		// Buffer for formatted numbers
  const char	*s;			// Pointer to string


  // Loop through the format string, formatting as needed...
  va_start(ap, format);
  start = format;

  while (*format)
  {
    if (*format == '%')
    {
      if (format > start)
        ret &= httpWrite(http, start, (size_t)(format - start)) > 0;

      tptr    = tformat;
      *tptr++ = *format++;

      if (*format == '%')
      {
        httpWrite(http, "%", 1);
        format ++;
	start = format;
	continue;
      }
      else if (strchr(" -+#\'", *format))
        *tptr++ = *format++;

      if (*format == '*')
      {
        // Get width from argument...
	format ++;
	width = va_arg(ap, int);

	snprintf(tptr, sizeof(tformat) - (size_t)(tptr - tformat), "%d", width);
	tptr += strlen(tptr);
      }
      else
      {
	width = 0;

	while (isdigit(*format & 255))
	{
	  if (tptr < (tformat + sizeof(tformat) - 1))
	    *tptr++ = *format;

	  width = width * 10 + *format++ - '0';
	}
      }

      if (*format == '.')
      {
	if (tptr < (tformat + sizeof(tformat) - 1))
	  *tptr++ = *format;

        format ++;

        if (*format == '*')
	{
          // Get precision from argument...
	  format ++;
	  prec = va_arg(ap, int);

	  snprintf(tptr, sizeof(tformat) - (size_t)(tptr - tformat), "%d", prec);
	  tptr += strlen(tptr);
	}
	else
	{
	  prec = 0;

	  while (isdigit(*format & 255))
	  {
	    if (tptr < (tformat + sizeof(tformat) - 1))
	      *tptr++ = *format;

	    prec = prec * 10 + *format++ - '0';
	  }
	}
      }

      if (*format == 'l' && format[1] == 'l')
      {
        size = 'L';

	if (tptr < (tformat + sizeof(tformat) - 2))
	{
	  *tptr++ = 'l';
	  *tptr++ = 'l';
	}

	format += 2;
      }
      else if (*format == 'h' || *format == 'l' || *format == 'L')
      {
	if (tptr < (tformat + sizeof(tformat) - 1))
	  *tptr++ = *format;

        size = *format++;
      }
      else
        size = 0;

      if (!*format)
      {
        start = format;
        break;
      }

      if (tptr < (tformat + sizeof(tformat) - 1))
        *tptr++ = *format;

      type  = *format++;
      *tptr = '\0';
      start = format;

      switch (type)
      {
	case 'E' : // Floating point formats
	case 'G' :
	case 'e' :
	case 'f' :
	case 'g' :
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

	    snprintf(temp, sizeof(temp), tformat, va_arg(ap, double));

            ret &= httpWrite(http, temp, strlen(temp)) > 0;
	    break;

        case 'B' : // Integer formats
	case 'X' :
	case 'b' :
        case 'd' :
	case 'i' :
	case 'o' :
	case 'u' :
	case 'x' :
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

#  ifdef HAVE_LONG_LONG
            if (size == 'L')
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, long long));
	    else
#  endif // HAVE_LONG_LONG
            if (size == 'l')
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, long));
	    else
	      snprintf(temp, sizeof(temp), tformat, va_arg(ap, int));

            ret &= httpWrite(http, temp, strlen(temp)) > 0;
	    break;

	case 'p' : // Pointer value
	    if ((size_t)(width + 2) > sizeof(temp))
	      break;

	    snprintf(temp, sizeof(temp), tformat, va_arg(ap, void *));

            ret &= httpWrite(http, temp, strlen(temp)) > 0;
	    break;

        case 'c' : // Character or character array
            if (width <= 1)
            {
              temp[0] = (char)va_arg(ap, int);
              temp[1] = '\0';
              ret &= html_escape(http, temp, 1);
            }
            else
              ret &= html_escape(http, va_arg(ap, char *), (size_t)width);
	    break;

	case 's' : // String
	    if ((s = va_arg(ap, const char *)) == NULL)
	      s = "(null)";

            ret &= html_escape(http, s, strlen(s));
	    break;
      }
    }
    else
    {
      // Keep literal character...
      format ++;
    }
  }

  if (format > start)
    ret &= httpWrite(http, start, (size_t)(format - start)) > 0;

  va_end(ap);

  return (ret);
}


//
// 'html_puts()' - Send a HTML string to the web browser client.
//
// This function sends a HTML string to the client without performing any
// escaping of special characters.
//

static bool				// O - `true` on success, `false` on error
html_puts(http_t     *http,		// I - Client connection
	  const char *s)		// I - String
{
  if (*s)
    return (httpWrite(http, s, strlen(s)) > 0);
  else
    return (true);
}


//
// 'send_favicon_png()' - Send the favorites icon.
//

static bool				// O - `true` on success, `false` on error
send_favicon_png(http_t *http)		// I - Client connection
{
  bool		ret = true;		// Return value


  ret &= send_http_response(http, HTTP_STATUS_OK, "image/png", sizeof(msysmon_png), /*message*/NULL);

  ret &= httpWrite(http, (const char *)msysmon_png, sizeof(msysmon_png)) > 0;

  return (ret);
}


//
// 'send_history_svg()' - Send a history graph in SVG format.
//

static bool				// O - `true` on success, `false` on error
send_history_svg(http_t *http,		// I - Client connection
                 char   *options)	// I - Options or `NULL` if none
{
  bool		ret = true;		// Return value
  unsigned	i;			// Looping var
  long		pid;			// Process ID
  msysmon_proc_t *proc;			// Process
  time_t	data_start;		// Start time of data
  unsigned	num_data;		// Number of data elements
  msysmon_data_t *data = NULL,		// Data to display
		*dataptr;		// Current data
  uint32_t	max_mem = 0x100;	// Maximum amount of memory
  double	mem_div;		// Memory divisor
  const char	*mem_units;		// Memory units
  unsigned	disp_data = MAX_DATA / 4;
					// Displayed data samples
  double	disp_scale = 1000.0 / (MAX_DATA / 4);
					// Horizontal scale for samples
  int		disp_secs;		// Number of seconds being displayed
  int		disp_off;		// Display offset
  char		datetime[256];		// Date/time string


  // Memory units/scaling...
  if (max_mem > 1048576)
  {
    mem_div   = 1048576.0;
    mem_units = "GB";
  }
  else
  {
    mem_div   = 1024.0;
    mem_units = "MB";
  }

  MSYSMON_DEBUG("Read lock...\n");
  cupsRWLockRead(&msysmonData.rwlock);

  // See if we have a PID in the options?
  if (options && !strncmp(options, "pid=", 4))
  {
    // Yes, show the process info
    pid = strtol(options + 4, NULL, 10);
    for (i = msysmonData.num_processes, proc = msysmonData.processes; i > 0; i --, proc ++)
    {
      if (proc->pid == (pid_t)pid)
      {
        uint32_t	high_mem;	// Highest memory reading

        data_start = proc->data_start;
        data       = proc->data;
        num_data   = proc->num_data;

        for (i = num_data, high_mem = 0, dataptr = data; i > 0; i --, dataptr ++)
        {
          if (dataptr->mem_k > high_mem)
            high_mem = dataptr->mem_k;
        }

        for (max_mem = 0x100; max_mem < 0x80000000; max_mem *= 2)
        {
          if (high_mem < max_mem)
            break;
        }
        break;
      }
    }
  }

  if (!data)
  {
    data_start = msysmonData.data_start;
    data       = msysmonData.data;
    max_mem    = msysmonGetSystemMemory();
    num_data   = msysmonData.num_data;
  }

  // Response and SVG header...
  ret &= send_http_response(http, HTTP_STATUS_OK, "image/svg+xml", /*content_length*/0, /*message*/NULL);

  ret &= html_puts(http, "<?xml version=\"1.0\" standalone=\"no\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" height=\"550\" viewBox=\"0 0 1200 550\">\n");

  // Grid lines
  html_puts(http, "<path d=\"M100 10h1000M100 110h1000M100 210h1000M100 310h1000M100 410h1000\" fill=\"none\" stroke=\"gray\" />\n");

  if (num_data > 1)
  {
    // Determine the proper time scale...
    if (num_data > (MAX_DATA / 2))
      disp_data = MAX_DATA;
    else if (num_data > (MAX_DATA / 4))
      disp_data = MAX_DATA / 2;
    else
      disp_data = MAX_DATA / 4;

    disp_scale = 1000.0 / disp_data;

    // Draw blue graph of memory
    ret &= html_puts(http, "<path d=\"M100 510");

    for (i = 0, dataptr = data; i < num_data; i ++, dataptr ++)
    {
      int y = (int)(510.0 - 400.0 * dataptr->mem_k / max_mem);

      ret &= html_printf(http, "L%.1f %d", 100.0 + i * disp_scale, y < 0 ? 0 : y);
    }

    ret &= html_puts(http, "V510z\" fill=\"#6699ff\" stroke=\"blue\" />\n");

    // Draw red graph of CPU
    ret &= html_puts(http, "<path d=\"");

    for (i = 0, dataptr = data; i < num_data; i ++, dataptr ++)
    {
      int y = 500 - 4 * dataptr->cpu_percent;

      ret &= html_printf(http, "%s%.1f %d", i == 0 ? "M" : "L", 100.0 + i * disp_scale, y < 0 ? 0 : y);
    }

    ret &= html_puts(http, "\" fill=\"none\" stroke=\"red\" stroke-width=\"2\" />\n");
  }

  MSYSMON_DEBUG("Unlock...\n");
  cupsRWUnlock(&msysmonData.rwlock);

  // Draw axis lines and labels
  html_puts(http, "<path d=\"M100 10L100 510L1100 510L1100 10\" fill=\"none\" stroke=\"black\" stroke-width=\"2\" />\n");
  html_puts(http, "<path d=\"M80 10h20M80 110h20M80 210h20M80 310h20M80 410h20M80 510h20\" fill=\"none\" stroke=\"black\" stroke-width=\"2\" />\n");
  html_puts(http, "<path d=\"M1100 110h20M1100 210h20M1100 310h20M1100 410h20M1100 510h20\" fill=\"none\" stroke=\"black\" stroke-width=\"2\" />\n");
  html_puts(http, "<path d=\"M100 510v20M200 510v20M300 510v20M400 510v20M500 510v20M600 510v20M700 510v20M800 510v20M900 510v20M1000 510v20M1100 510v20\" fill=\"none\" stroke=\"black\" stroke-width=\"2\" />\n");

  disp_secs = disp_data * msysmonData.interval;
  if (disp_secs <= 86400)
    disp_off = 70;
  else if (disp_secs <= (7 * 86400))
    disp_off = 50;
  else
    disp_off = 40;

  for (i = 0; i <= 10; i ++)
    ret &= html_printf(http, "<text x=\"%d\" y=\"545\" font-family=\"sans-serif\" font-size=\"15\" fill=\"black\">%s</text>\n", disp_off + i * 100, get_datetime(data_start + i * disp_secs / 10, disp_secs, datetime, sizeof(datetime)));

  ret &= html_puts(http, "<text x=\"25\" y=\"15\" font-family=\"sans-serif\" font-size=\"20\" fill=\"red\">125%</text>\n");
  ret &= html_puts(http, "<text x=\"25\" y=\"65\" font-family=\"sans-serif\" font-size=\"20\" font-weight=\"bold\" fill=\"red\">CPU</text>\n");
  ret &= html_puts(http, "<text x=\"25\" y=\"115\" font-family=\"sans-serif\" font-size=\"20\" fill=\"red\">100%</text>\n");
  ret &= html_puts(http, "<text x=\"35\" y=\"215\" font-family=\"sans-serif\" font-size=\"20\" fill=\"red\">75%</text>\n");
  ret &= html_puts(http, "<text x=\"35\" y=\"315\" font-family=\"sans-serif\" font-size=\"20\" fill=\"red\">50%</text>\n");
  ret &= html_puts(http, "<text x=\"35\" y=\"415\" font-family=\"sans-serif\" font-size=\"20\" fill=\"red\">25%</text>\n");
  ret &= html_puts(http, "<text x=\"45\" y=\"515\" font-family=\"sans-serif\" font-size=\"20\" fill=\"red\">0%</text>\n");

  ret &= html_puts(http, "<text x=\"1120\" y=\"65\" font-family=\"sans-serif\" font-size=\"20\" font-weight=\"bold\" fill=\"#6699ff\">Memory</text>\n");
  ret &= html_printf(http, "<text x=\"1125\" y=\"115\" font-family=\"sans-serif\" font-size=\"20\" fill=\"#6699ff\">%.1f%s</text>\n", max_mem / mem_div, mem_units);
  ret &= html_printf(http, "<text x=\"1125\" y=\"215\" font-family=\"sans-serif\" font-size=\"20\" fill=\"#6699ff\">%.1f%s</text>\n", 0.75 * max_mem / mem_div, mem_units);
  ret &= html_printf(http, "<text x=\"1125\" y=\"315\" font-family=\"sans-serif\" font-size=\"20\" fill=\"#6699ff\">%.1f%s</text>\n", 0.5 * max_mem / mem_div, mem_units);
  ret &= html_printf(http, "<text x=\"1125\" y=\"415\" font-family=\"sans-serif\" font-size=\"20\" fill=\"#6699ff\">%.1f%s</text>\n", 0.25 * max_mem / mem_div, mem_units);
  ret &= html_printf(http, "<text x=\"1125\" y=\"515\" font-family=\"sans-serif\" font-size=\"20\" fill=\"#6699ff\">0.0%s</text>\n", mem_units);

  // SVG footer
  ret &= html_puts(http, "</svg>\n");
  httpWrite(http, "", 0);

  return (ret);
}


//
// 'send_html_report()' - Send a usage report in HTML format.
//

static bool				// O - `true` on success, `false` on error
send_html_report(http_t *http,		// I - Client connection
                 char   *options)	// I - Options or `NULL` if none
{
  bool		ret = true;		// Return value
  msysmon_data_t *data;			// Pointer to last update


  // Response and HTML header...
  ret &= send_http_response(http, HTTP_STATUS_OK, "text/html", /*content_length*/0, /*message*/NULL);

  ret &= html_header(http, /*title*/NULL, /*refresh*/0/*msysmonData.interval < 10 ? 10 : msysmonData.interval*/);

  MSYSMON_DEBUG("Read lock...\n");
  cupsRWLockRead(&msysmonData.rwlock);

  data = msysmonData.data + msysmonData.num_data - 1;

  ret &= html_puts(http, "<h1>System Monitor History</h1>\n");
  if (data->mem_k > 1048576)
    ret &= html_printf(http, "<p>Current CPU: %u%% &nbsp; Memory: %.1fGB &nbsp; Processes: %u</p>\n", data->cpu_percent, data->mem_k / 1048576.0, data->tp_count);
  else
    ret &= html_printf(http, "<p>Current CPU: %u%% &nbsp; Memory: %.1fMB &nbsp; Processes: %u</p>\n", data->cpu_percent, data->mem_k / 1024.0, data->tp_count);

  ret &= html_puts(http, "<p><img src=\"/history.svg\" width=\"100%\"></p>\n");

  ret &= html_puts(http, "<h2>Interesting Processes</h2>\n");

  if (msysmonData.num_processes == 0)
  {
    ret &= html_puts(http, "<p>None.<p>\n");
  }
  else
  {
    unsigned		i;		// Looping var
    msysmon_proc_t	*proc;		// Current process

    ret &= html_printf(http, "<p>%u interesting processes:<br>\n<table summary=\"Interesting Processes\">\n  <thead>\n    <tr><th>PID</th><th>Command</th><th>CPU</th><th>Memory</th><th>Threads</th></tr>\n  </thead>\n  <tbody>\n", msysmonData.num_processes);

    for (i = msysmonData.num_processes, proc = msysmonData.processes; i > 0; i --, proc ++)
    {
      data = proc->data + proc->num_data - 1;

      ret &= html_printf(http, "    <tr><td>%d</td><td><div class=\"modal\" id=\"pid%d\"><span class=\"mclose\" onclick=\"close_modal('pid%d');\">&times;</span><img src=\"/history.svg?pid=%d\" width=\"100%%\"></div>%s%s</td><td><button onclick=\"open_modal('pid%d');\">%u%%</button></td><td><button onclick=\"open_modal('pid%d');\">", (int)proc->pid, (int)proc->pid, (int)proc->pid, (int)proc->pid, proc->command, proc->end_time ? " (terminated)" : "", (int)proc->pid, data->cpu_percent, (int)proc->pid);

      if (data->mem_k > 1048576)
	ret &= html_printf(http, "%.1fGB</button></td><td>%u</td></tr>\n", data->mem_k / 1048576.0, data->tp_count);
      else
	ret &= html_printf(http, "%.1fMB</button></td><td>%u</td></tr>\n", data->mem_k / 1024.0, data->tp_count);
    }

    ret &= html_puts(http, "  </tbody>\n</table></p>\n");
  }

  MSYSMON_DEBUG("Unlock...\n");
  cupsRWUnlock(&msysmonData.rwlock);

  // HTML footer
  ret &= html_footer(http);

  return (ret);
}


//
// 'send_http_response()' - Send a HTTP response.
//

static bool				// O - `true` on success, `false` on error
send_http_response(
    http_t        *http,		// I - Client connection
    http_status_t status,		// I - HTTP response status
    const char    *content_type,	// I - Content-Type value
    size_t        content_length,	// I - Content-Length value or `0` for chunked
    const char    *message,		// I - Printf message or `NULL` for none
    ...)				// I - Additional arguments as needed
{
  bool		ret;			// Return value
  va_list	ap;			// Pointer to additional arguments
  char		buffer[8192];		// Message buffer
  size_t	buflen;			// Length of message


  httpClearFields(http);
  if (status >= HTTP_STATUS_BAD_REQUEST)
    httpSetField(http, HTTP_FIELD_CONNECTION, "close");
  httpSetField(http, HTTP_FIELD_CONTENT_TYPE, content_type);

  if (message)
  {
    // Format the message...
    va_start(ap, message);
    vsnprintf(buffer, sizeof(buffer), message, ap);
    va_end(ap);

    buflen = strlen(buffer);

    httpSetLength(http, buflen);

    ret = httpWriteResponse(http, status);

    if (ret)
      ret = httpWrite(http, buffer, buflen) > 0;
  }
  else
  {
    // No message...
    httpSetLength(http, content_length);

    ret = httpWriteResponse(http, status);
  }

  return (ret);
}
