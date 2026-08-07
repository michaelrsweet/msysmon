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
#include <ctype.h>


//
// Local functions...
//

static bool	html_escape(http_t *http, const char *s, size_t slen);
static bool	html_footer(http_t *http);
static bool	html_header(http_t *http, const char *title, int refresh);
static bool	html_printf(http_t *http, const char *format, ...);
static bool	html_puts(http_t *http, const char *s);

static bool	send_favicon_png(http_t *http);
static bool	send_history_svg(http_t *http, char *options);
static bool	send_html_report(http_t *http, char *options);
static bool	send_http_response(http_t *http, http_status_t status, const char *content_type, const char *message, ...);


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
      send_http_response(http, HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad/unknown operation.\n");
      break;
    }
    else if (state == HTTP_STATE_UNKNOWN_VERSION)
    {
      send_http_response(http, HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad HTTP version.\n");
      break;
    }
    else if (resource[0] != '/' && strcmp(resource, "*"))
    {
      send_http_response(http, HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad resource path '%s'.\n", resource);
      break;
    }

    if ((options = strchr(resource, '?')) != NULL)
      *options++ = '\0';

    // Read all HTTP requests...
    while ((status = httpUpdate(http)) == HTTP_STATUS_CONTINUE)
      ;

    if (status != HTTP_STATUS_OK)
    {
      send_http_response(http, HTTP_STATUS_BAD_REQUEST, "text/plain", "Bad/missing request headers.\n");
      break;
    }

    switch (state)
    {
      case HTTP_STATE_OPTIONS :
          send_http_response(http, HTTP_STATUS_OK, /*content_type*/NULL, /*message*/NULL);
          httpWrite(http, "", 0);
          break;

      case HTTP_STATE_HEAD :
          if (!strcmp(resource, "/"))
            send_http_response(http, HTTP_STATUS_OK, "text/html", /*message*/NULL);
          else if (!strcmp(resource, "/favicon.png"))
            send_http_response(http, HTTP_STATUS_OK, "image/png", /*message*/NULL);
          else if (!strcmp(resource, "/history.svg"))
            send_http_response(http, HTTP_STATUS_OK, "image/svg", /*message*/NULL);
          else
            send_http_response(http, HTTP_STATUS_NOT_FOUND, "text/plain", /*message*/NULL);
          httpWrite(http, "", 0);
          break;

      case HTTP_STATE_GET :
          if (!strcmp(resource, "/"))
            send_html_report(http, options);
          else if (!strcmp(resource, "/favicon.png"))
            send_favicon_png(http);
          else if (!strcmp(resource, "/history.svg"))
            send_history_svg(http, options);
          else
            send_http_response(http, HTTP_STATUS_NOT_FOUND, "text/plain", "The resource '%s' is not available from this server.\n", resource);
          break;

      default :
          send_http_response(http, HTTP_STATUS_NOT_IMPLEMENTED, "text/plain", "The '%s' method is not supported by this server.\n", httpStateString(state));
          goto close_client;
    }
  }

  // Cleanup and return...
  if (state == HTTP_STATE_ERROR && httpGetError(http) != EPIPE && httpGetError(http))
    fprintf(stderr, "msysmon: Bad request line (%s).\n", strerror(httpGetError(http)));

  close_client:

  httpClose(http);

  return (NULL);
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

#if 0 // No stylesheet yet
  ret &= html_puts(http,
		   "    <style>\n"
		   "    </style>\n");
#endif // 0

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


  // TODO: Add static favicon.png file.
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
  msysmon_data_t *data;			// Current data
  uint32_t	max_mem = msysmonGetSystemMemory();
					// Maximum amount of memory


  // Response and SVG header...
  ret &= send_http_response(http, HTTP_STATUS_OK, "image/svg", /*message*/NULL);

  ret &= html_puts(http, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1200\" height=\"600\" viewBox=\"0 0 1200 600\">\n");

  // Draw axis lines
  html_puts(http, "<path d=\"M 100 0 L 100 500 L 1100 500 L 1100 0\" stroke=\"black\" stroke-width=\"2\" />\n");

  cupsRWLockRead(&msysmonData.rwlock);

  // Draw red graph of CPU
  ret &= html_puts(http, "<path d=\"");

  for (i = 0, data = msysmonData.data; i < msysmonData.num_data; i ++, data ++)
    ret &= html_printf(http, "%s %.1f %d", i == 0 ? "M" : " L", 0.5 * i, 500 - 5 * data->cpu_percent);

  ret &= html_puts(http, "\" stroke=\"red\" />\n");

  // Draw blue graph of memory
  ret &= html_puts(http, "<path d=\"");

  for (i = 0, data = msysmonData.data; i < msysmonData.num_data; i ++, data ++)
    ret &= html_printf(http, "%s %.1f %d", i == 0 ? "M" : " L", 0.5 * i, 500 - 500 * data->mem_k / max_mem);

  ret &= html_puts(http, "\" stroke=\"blue\" />\n");

  cupsRWUnlock(&msysmonData.rwlock);

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
  ret &= send_http_response(http, HTTP_STATUS_OK, "text/html", /*message*/NULL);

  ret &= html_header(http, /*title*/NULL, /*refresh*/0);

  cupsRWLockRead(&msysmonData.rwlock);

  data = msysmonData.data + msysmonData.num_data - 1;

  ret &= html_puts(http, "<h1>System Monitor History</h1>\n");
  ret &= html_printf(http, "<p>Current CPU @ %u%%, memory @ %uk, %u processes.</p>\n", data->cpu_percent, data->mem_k, data->tp_count);
  ret &= html_puts(http, "<img src=\"/history.svg\" width=\"100%\">\n");

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
    httpSetLength(http, 0);

    ret = httpWriteResponse(http, status);
  }

  return (ret);
}
