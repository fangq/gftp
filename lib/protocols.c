/*****************************************************************************/
/*  protocols.c - Skeleton functions for the protocols gftp supports         */
/*  Copyright (C) 1998-2003 Brian Masney <masneyb@gftp.org>                  */
/*                                                                           */
/*  This program is free software; you can redistribute it and/or modify     */
/*  it under the terms of the GNU General Public License as published by     */
/*  the Free Software Foundation; either version 2 of the License, or        */
/*  (at your option) any later version.                                      */
/*                                                                           */
/*  This program is distributed in the hope that it will be useful,          */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of           */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            */
/*  GNU General Public License for more details.                             */
/*                                                                           */
/*  You should have received a copy of the GNU General Public License        */
/*  along with this program; if not, write to the Free Software              */
/*  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111 USA      */
/*****************************************************************************/

#include "gftp.h"
static const char cvsid[] = "$Id$";

gftp_request *
gftp_request_new (void)
{
  gftp_request *request;

  request = g_malloc0 (sizeof (*request));
  request->datafd = -1;
  request->cachefd = -1;
  request->server_type = GFTP_DIRTYPE_OTHER;
  return (request);
}


void
gftp_request_destroy (gftp_request * request, int free_request)
{
  g_return_if_fail (request != NULL);

  gftp_disconnect (request);

  if (request->destroy != NULL)
    request->destroy (request);

  if (request->hostname)
    g_free (request->hostname);
  if (request->username)
    g_free (request->username);
  if (request->password)
    {
      memset (request->password, 0, strlen (request->password));
      g_free (request->password);
    }
  if (request->account)
    {
      memset (request->account, 0, strlen (request->account));
      g_free (request->account);
    }
  if (request->directory)
    g_free (request->directory);
  if (request->last_ftp_response)
    g_free (request->last_ftp_response);
  if (request->protocol_data)
    g_free (request->protocol_data);

  if (request->local_options_vars != NULL)
    {
      gftp_config_free_options (request->local_options_vars,
                                request->local_options_hash,
                                request->num_local_options_vars);
    }

  memset (request, 0, sizeof (*request));

  if (free_request)
    g_free (request);
  else
    {
      request->datafd = -1;
      request->cachefd = -1;
    }
}


/* This function is called to copy protocol specific data from one request 
   structure to another. This is typically called when a file transfer is
   completed, state information can be copied back to the main window */
void
gftp_copy_param_options (gftp_request * dest_request,
                         gftp_request * src_request)
{
  g_return_if_fail (dest_request != NULL);
  g_return_if_fail (src_request != NULL);
  g_return_if_fail (dest_request->protonum == src_request->protonum);

  if (dest_request->copy_param_options)
    dest_request->copy_param_options (dest_request, src_request);
}


void
gftp_file_destroy (gftp_file * file, int free_it)
{
  g_return_if_fail (file != NULL);

  if (file->file)
    g_free (file->file);
  if (file->user)
    g_free (file->user);
  if (file->group)
    g_free (file->group);
  if (file->destfile)
    g_free (file->destfile);

  if (free_it)
    g_free (file);
  else
    memset (file, 0, sizeof (*file));
}


int
gftp_connect (gftp_request * request)
{
  int ret;

  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (request->connect == NULL)
    return (GFTP_EFATAL);

  if ((ret = gftp_set_config_options (request)) < 0)
    return (ret);

  return (request->connect (request));
}


void
gftp_disconnect (gftp_request * request)
{
  g_return_if_fail (request != NULL);

#if defined (HAVE_GETADDRINFO) && defined (HAVE_GAI_STRERROR)
  if (request->free_hostp && request->hostp != NULL)
    freeaddrinfo (request->hostp);
#endif
  request->hostp = NULL;

#ifdef USE_SSL
  if (request->ssl != NULL)
    {
      SSL_free (request->ssl);
      request->ssl = NULL;
    }
#endif

#if GLIB_MAJOR_VERSION > 1
  if (request->iconv_initialized)
    {
      g_iconv_close (request->iconv);
      request->iconv_initialized = 0;
    }
#endif

  request->cached = 0;
  if (request->disconnect == NULL)
    return;
  request->disconnect (request);
}


off_t
gftp_get_file (gftp_request * request, const char *filename, int fd,
               off_t startsize)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  request->cached = 0;
  if (request->get_file == NULL)
    return (GFTP_EFATAL);

  return (request->get_file (request, filename, fd, startsize));
}


int
gftp_put_file (gftp_request * request, const char *filename, int fd,
               off_t startsize, off_t totalsize)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  request->cached = 0;
  if (request->put_file == NULL)
    return (GFTP_EFATAL);

  return (request->put_file (request, filename, fd, startsize, totalsize));
}


off_t
gftp_transfer_file (gftp_request * fromreq, const char *fromfile, 
                    int fromfd, off_t fromsize, 
                    gftp_request * toreq, const char *tofile,
                    int tofd, off_t tosize)
{
  /* Needed for systems that size(float) < size(void *) */
  union { intptr_t i; float f; } maxkbs;
  off_t size;
  int ret;

  g_return_val_if_fail (fromreq != NULL, GFTP_EFATAL);
  g_return_val_if_fail (fromfile != NULL, GFTP_EFATAL);
  g_return_val_if_fail (toreq != NULL, GFTP_EFATAL);
  g_return_val_if_fail (tofile != NULL, GFTP_EFATAL);

  gftp_lookup_request_option (toreq, "maxkbs", &maxkbs.f);

  if (maxkbs.f > 0)
    {
      toreq->logging_function (gftp_logging_misc, toreq,
                    _("File transfer will be throttled to %.2f KB/s\n"), 
                    maxkbs.f);
    }

  if (fromreq->protonum == toreq->protonum &&
      fromreq->transfer_file != NULL)
    return (fromreq->transfer_file (fromreq, fromfile, fromsize, toreq, 
                                    tofile, tosize));

  fromreq->cached = 0;
  toreq->cached = 0;

get_file:
  size = gftp_get_file (fromreq, fromfile, fromfd, tosize);
  if (size < 0)
    {
      if (size == GFTP_ETIMEDOUT)
        {
          ret = gftp_connect (fromreq);
          if (ret < 0)
            return (ret);

          goto get_file;
        }

      return (size);
    }

put_file:
  ret = gftp_put_file (toreq, tofile, tofd, tosize, size);
  if (ret != 0)
    {
      if (size == GFTP_ETIMEDOUT)
        {
          ret = gftp_connect (fromreq);
          if (ret < 0)
            return (ret);

          goto put_file;
        }

      if (gftp_abort_transfer (fromreq) != 0)
        gftp_end_transfer (fromreq);

      return (ret);
    }

  return (size);
}


ssize_t 
gftp_get_next_file_chunk (gftp_request * request, char *buf, size_t size)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);
  g_return_val_if_fail (buf != NULL, GFTP_EFATAL);

  if (request->get_next_file_chunk != NULL)
    return (request->get_next_file_chunk (request, buf, size));

  return (request->read_function (request, buf, size, request->datafd));
}


ssize_t 
gftp_put_next_file_chunk (gftp_request * request, char *buf, size_t size)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);
  g_return_val_if_fail (buf != NULL, GFTP_EFATAL);

  if (request->put_next_file_chunk != NULL)
    return (request->put_next_file_chunk (request, buf, size));

  return (request->write_function (request, buf, size, request->datafd));
}


int
gftp_end_transfer (gftp_request * request)
{
  int ret;

  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (!request->cached && 
      request->end_transfer != NULL)
    ret = request->end_transfer (request);
  else
    ret = 0;

  if (request->cachefd > 0)
    {
      close (request->cachefd);
      request->cachefd = -1;
    }

  if (request->last_dir_entry)
    {
      g_free (request->last_dir_entry);
      request->last_dir_entry = NULL;
      request->last_dir_entry_len = 0;
    }

  return (ret);
}


int
gftp_abort_transfer (gftp_request * request)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (request->abort_transfer == NULL)
    return (GFTP_EFATAL);

  /* FIXME - end the transfer if it is not successful */
  return (request->abort_transfer (request));
}


int
gftp_stat_filename (gftp_request * request, const char *filename, mode_t * mode,
                    off_t * filesize)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);
  g_return_val_if_fail (filename != NULL, GFTP_EFATAL);

  if (request->stat_filename != NULL)
    return (request->stat_filename (request, filename, mode, filesize));
  else
    return (0);
}


int
gftp_list_files (gftp_request * request)
{
  char *remote_lc_time, *locret;
  int fd;

  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

#if ENABLE_NLS
  gftp_lookup_request_option (request, "remote_lc_time", &remote_lc_time);
  if (remote_lc_time != NULL && *remote_lc_time != '\0')
    locret = setlocale (LC_TIME, remote_lc_time);
  else
    locret = setlocale (LC_TIME, NULL);

  if (locret == NULL)
    {
      locret = setlocale (LC_TIME, NULL);
      request->logging_function (gftp_logging_error, request,
                                 _("Error setting LC_TIME to '%s'. Falling back to '%s'\n"),
                                 remote_lc_time, locret);
    }
#else
  locret = _("<unknown>");
#endif

  request->cached = 0;
  if (request->use_cache && (fd = gftp_find_cache_entry (request)) > 0)
    {
      request->logging_function (gftp_logging_misc, request,
                                 _("Loading directory listing %s from cache (LC_TIME=%s)\n"),
                                 request->directory, locret);

      request->cachefd = fd;
      request->cached = 1;
      return (0);
    }
  else if (request->use_cache)
    {
      request->logging_function (gftp_logging_misc, request,
                                 _("Loading directory listing %s from server (LC_TIME=%s)\n"),
                                 request->directory, locret);

      request->cachefd = gftp_new_cache_entry (request);
      request->cached = 0; 
    }

  if (request->list_files == NULL)
    return (GFTP_EFATAL);

  return (request->list_files (request));
}


#if GLIB_MAJOR_VERSION > 1

static /*@null@*/ char *
_gftp_get_next_charset (char **curpos)
{
  char *ret, *endpos;

  if (**curpos == '\0')
    return (NULL);

  ret = *curpos;
  if ((endpos = strchr (*curpos, ',')) == NULL)
    *curpos += strlen (*curpos);
  else
    {
      *endpos = '\0';
      *curpos = endpos + 1;
    }

  return (ret);
}


/*@null@*/ char *
gftp_string_to_utf8 (gftp_request * request, const char *str, size_t *dest_len)
{
  char *ret, *remote_charsets, *stpos, *cur_charset, *tempstr;
  GError * error = NULL;
  gsize bread;

  if (request == NULL)
    return (NULL);

  if (g_utf8_validate (str, -1, NULL))
    return (NULL);
  else if (request->iconv_initialized)
    {
      ret = g_convert_with_iconv (str, -1, request->iconv, &bread, dest_len, 
                                  &error);
      if (ret == NULL)
        printf (_("Error converting string '%s' from character set %s to character set %s: %s\n"),
                str, _("<unknown>"), "UTF-8", error->message);

      return (ret);
    }

  gftp_lookup_request_option (request, "remote_charsets", &tempstr);
  if (*tempstr == '\0')
    {
      error = NULL;
      if ((ret = g_locale_to_utf8 (str, -1, &bread, dest_len, &error)) != NULL)
        return (ret);

      /* Don't use request->logging_function since the strings must be in UTF-8
         for the GTK+ 2.x port */
      printf (_("Error converting string '%s' to UTF-8 from current locale: %s\n"),
              str, error->message);
      return (NULL);
    }

  remote_charsets = g_strdup (tempstr);
  ret = NULL;
  stpos = remote_charsets;
  while ((cur_charset = _gftp_get_next_charset (&stpos)) != NULL)
    {
      if ((request->iconv = g_iconv_open ("UTF-8", cur_charset)) == (GIConv) -1)
        continue;

      error = NULL;
      if ((ret = g_convert_with_iconv (str, -1, request->iconv, &bread,
                                       dest_len, &error)) == NULL)
        {
          printf (_("Error converting string '%s' from character set %s to character set %s: %s\n"),
                  str, cur_charset, "UTF-8", error->message);

          g_iconv_close (request->iconv);
          request->iconv = NULL;
          continue;
        }
      else
        {
          request->iconv_initialized = 1;
          break;
        }
    }

  g_free (remote_charsets);

  return (ret);
}


char *
gftp_string_from_utf8 (gftp_request * request, const char *str,
                       size_t *dest_len)
{
  char *ret, *remote_charsets, *stpos, *cur_charset, *tempstr;
  GError * error = NULL;
  gsize bread;

  if (request == NULL)
    return (NULL);

  /* FIXME - use request->use_local_encoding */

  /* If the string isn't in UTF-8 format, assume it is already in the current
     locale... */
  if (!g_utf8_validate (str, -1, NULL))
    return (NULL);
  else if (request->iconv_initialized)
    {
      ret = g_convert_with_iconv (str, -1, request->iconv, &bread, dest_len, 
                                  &error);
      if (ret == NULL)
        printf (_("Error converting string '%s' from character set %s to character set %s: %s\n"),
                str, "UTF-8", _("<unknown>"), error->message);

      return (ret);
    }

  gftp_lookup_request_option (request, "remote_charsets", &tempstr);
  if (*tempstr == '\0')
    {
      error = NULL;
      if ((ret = g_locale_from_utf8 (str, -1, &bread, dest_len,
                                     &error)) != NULL)
        return (ret);

      /* Don't use request->logging_function since the strings must be in UTF-8
         for the GTK+ 2.x port */
      printf (_("Error converting string '%s' to current locale from UTF-8: %s\n"),
              str, error->message);
      return (NULL);
    }

  remote_charsets = g_strdup (tempstr);
  ret = NULL;
  stpos = remote_charsets;
  while ((cur_charset = _gftp_get_next_charset (&stpos)) != NULL)
    {
      if ((request->iconv = g_iconv_open (cur_charset, "UTF-8")) == (GIConv) -1)
        continue;

      error = NULL;
      if ((ret = g_convert_with_iconv (str, -1, request->iconv, &bread,
                                       dest_len, &error)) == NULL)
        {
          printf (_("Error converting string '%s' from character set %s to character set %s: %s\n"),
                  str, "UTF-8", cur_charset, error->message);

          g_iconv_close (request->iconv);
          request->iconv = NULL;
          continue;
        }
      else
        {
          request->iconv_initialized = 1;
          break;
        }
    }

  g_free (remote_charsets);

  return (ret);
}

#else

char *
gftp_string_to_utf8 (gftp_request * request, const char *str, size_t dest_len)
{
  return (NULL);
}


char *
gftp_string_from_utf8 (gftp_request * request, const char *str, size_t dest_len)
{
  return (NULL);
}

#endif


int
gftp_get_next_file (gftp_request * request, const char *filespec,
                    gftp_file * fle)
{
  char *slashpos, *tmpfile, *utf8;
  size_t destlen;
  int fd, ret;

  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (request->get_next_file == NULL)
    return (GFTP_EFATAL);

  if (request->cached && request->cachefd > 0)
    fd = request->cachefd;
  else
    fd = request->datafd;

  memset (fle, 0, sizeof (*fle));
  do
    {
      gftp_file_destroy (fle, 0);
      ret = request->get_next_file (request, fle, fd);
      if (fle->file != NULL && (slashpos = strrchr (fle->file, '/')) != NULL)
        {
          if (*(slashpos + 1) == '\0')
            {
              gftp_file_destroy (fle, 0);
              continue;
            }

          *slashpos = '\0';
          tmpfile = g_strdup (slashpos + 1);

          if (strcmp (fle->file, request->directory) != 0)
            request->logging_function (gftp_logging_error, request,
                                       _("Warning: Stripping path off of file '%s'. The stripped path (%s) doesn't match the current directory (%s)\n"),
                                       tmpfile, fle->file, request->directory,
                                       g_strerror (errno));
          
          g_free (fle->file);
          fle->file = tmpfile;
        }

      if (ret >= 0 && fle->file != NULL)
        {
          utf8 = gftp_string_to_utf8 (request, fle->file, &destlen);
          if (utf8 != NULL)
            {
              tmpfile = fle->file;
              fle->file = utf8;
              g_free (tmpfile);
            }
        }

      if (ret >= 0 && !request->cached && request->cachefd > 0 && 
          request->last_dir_entry != NULL)
        {
          if (gftp_fd_write (request, request->last_dir_entry,
                          request->last_dir_entry_len, request->cachefd) < 0)
            {
              request->logging_function (gftp_logging_error, request,
                                        _("Error: Cannot write to cache: %s\n"),
                                        g_strerror (errno));
              close (request->cachefd);
              request->cachefd = -1;
            }
        }
    } while (ret > 0 && !gftp_match_filespec (request, fle->file, filespec));

  return (ret);
}


int
gftp_parse_bookmark (gftp_request * request, gftp_request * local_request, 
                     const char * bookmark, int *refresh_local)
{
  gftp_logging_func logging_function;
  gftp_bookmarks_var * tempentry;
  char *default_protocol;
  const char *email;
  int i, init_ret;
  size_t destlen;

  g_return_val_if_fail (request != NULL, GFTP_EFATAL);
  g_return_val_if_fail (bookmark != NULL, GFTP_EFATAL);
  
  logging_function = request->logging_function;
  gftp_request_destroy (request, 0);
  request->logging_function = logging_function;

  if ((tempentry = g_hash_table_lookup (gftp_bookmarks_htable, 
                                        bookmark)) == NULL)
    {
      request->logging_function (gftp_logging_error, request,
                                 _("Error: Could not find bookmark %s\n"), 
                                 bookmark);
      return (GFTP_EFATAL);
    }
  else if (tempentry->hostname == NULL || *tempentry->hostname == '\0')
    {
      request->logging_function (gftp_logging_error, request,
                                 _("Bookmarks Error: The bookmark entry %s does not have a hostname\n"), bookmark);
      return (GFTP_EFATAL);
    }

  if (tempentry->user != NULL)
    gftp_set_username (request, tempentry->user);

  if (tempentry->pass != NULL)
    {
      if (strcmp (tempentry->pass, "@EMAIL@") == 0)
        {
          gftp_lookup_request_option (request, "email", &email);
          gftp_set_password (request, email);
        }
      else
        gftp_set_password (request, tempentry->pass);
    }

  if (tempentry->acct != NULL)
    gftp_set_account (request, tempentry->acct);

  gftp_set_hostname (request, tempentry->hostname);
  gftp_set_directory (request, tempentry->remote_dir);
  gftp_set_port (request, tempentry->port);

  if (local_request != NULL && tempentry->local_dir != NULL &&
      *tempentry->local_dir != '\0')
    {
      gftp_set_directory (local_request, tempentry->local_dir);
      if (refresh_local != NULL)
        *refresh_local = 1;
    }
  else if (refresh_local != NULL)
    *refresh_local = 0;

  for (i = 0; gftp_protocols[i].name; i++)
    {
      if (strcmp (gftp_protocols[i].name, tempentry->protocol) == 0)
        {
          if ((init_ret = gftp_protocols[i].init (request)) < 0)
            {
              gftp_request_destroy (request, 0);
              return (init_ret);
            }
          break;
        }
    }

  if (gftp_protocols[i].name == NULL)
    {
      gftp_lookup_request_option (request, "default_protocol", 
                                  &default_protocol);

      if (default_protocol != NULL && *default_protocol != '\0')
        {
          for (i = 0; gftp_protocols[i].url_prefix; i++)
            {
              if (strcmp (gftp_protocols[i].name, default_protocol) == 0)
                break;
            }
        }

      if (gftp_protocols[i].url_prefix == NULL)
        i = GFTP_FTP_NUM;
    }

  gftp_copy_local_options (&request->local_options_vars,
                           &request->local_options_hash,
                           &request->num_local_options_vars,
                           tempentry->local_options_vars,
                           tempentry->num_local_options_vars);

  if ((init_ret = gftp_protocols[i].init (request)) < 0)
    {
      gftp_request_destroy (request, 0);
      return (init_ret);
    }

  return (0);
}


int
gftp_parse_url (gftp_request * request, const char *url)
{
  char *pos, *endpos, *default_protocol, *new_url;
  gftp_logging_func logging_function;
  const char *clear_pos;
  int i, ret;

  g_return_val_if_fail (request != NULL, GFTP_EFATAL);
  g_return_val_if_fail (url != NULL, GFTP_EFATAL);

  logging_function = request->logging_function;
  gftp_request_destroy (request, 0);
  request->logging_function = logging_function;

  for (clear_pos = url;
       *clear_pos == ' ' || *clear_pos == '\t';
       clear_pos++);

  new_url = g_strdup (clear_pos);

  for (pos = new_url + strlen (new_url) - 1;
       *pos == ' ' || *pos == '\t';
       pos--)
    *pos = '\0';

  /* See if the URL has a protocol... */
  if ((pos = strstr (new_url, "://")) != NULL)
    {
      *pos = '\0';

      for (i = 0; gftp_protocols[i].url_prefix; i++)
        {
          if (strcmp (gftp_protocols[i].url_prefix, new_url) == 0)
            break;
        }

      if (gftp_protocols[i].url_prefix == NULL)
        {
          request->logging_function (gftp_logging_error, NULL, 
                                     _("The protocol '%s' is currently not supported.\n"),
                                     new_url);
          g_free (new_url);
          return (GFTP_EFATAL);
        }

      *pos = ':';
      pos += 3;
    }
  else
    {
      gftp_lookup_request_option (request, "default_protocol", 
                                  &default_protocol);

      i = GFTP_FTP_NUM;
      if (default_protocol != NULL && *default_protocol != '\0')
        {
          for (i = 0; gftp_protocols[i].url_prefix; i++)
            {
              if (strcmp (gftp_protocols[i].name, default_protocol) == 0)
                break;
            }
        }

      if (gftp_protocols[i].url_prefix == NULL)
        {
          request->logging_function (gftp_logging_error, NULL, 
                                     _("The protocol '%s' is currently not supported.\n"),
                                     default_protocol);
          g_free (new_url);
          return (GFTP_EFATAL);
        }

      pos = new_url;
    }

  if ((ret = gftp_protocols[i].init (request)) < 0)
    {
      gftp_request_destroy (request, 0);
      return (ret);
    }

  if ((endpos = strchr (pos, '/')) != NULL)
    {
      gftp_set_directory (request, endpos);
      *endpos = '\0';
    }

  if (request->parse_url != NULL)
    {
      ret = request->parse_url (request, new_url);
      g_free (new_url);
      return (ret);
    }

  if (*pos != '\0')
    {
      if (endpos == NULL)
        endpos = pos + strlen (pos) - 1;
      else
        endpos--;

      for (; isdigit (*endpos); endpos--);

      if (*endpos == ':' && isdigit (*(endpos + 1)))
        {
          gftp_set_port (request, strtol (endpos + 1, NULL, 10));
          *endpos = '\0';
        }

      if ((endpos = strrchr (pos, '@')) != NULL)
        {
          gftp_set_hostname (request, endpos + 1);
          *endpos = '\0';

          if ((endpos = strchr (pos, ':')) != NULL)
            {
              *endpos = '\0';
              gftp_set_username (request, pos);
              gftp_set_password (request, endpos + 1);
            }
          else
            {
              gftp_set_username (request, pos);
              gftp_set_password (request, "");
            }
        }
      else
        gftp_set_hostname (request, pos);
    }

  g_free (new_url);
  return (0);
}


void
gftp_set_hostname (gftp_request * request, const char *hostname)
{
  g_return_if_fail (request != NULL);
  g_return_if_fail (hostname != NULL);

  if (request->hostname)
    g_free (request->hostname);
  request->hostname = g_strdup (hostname);
}


void
gftp_set_username (gftp_request * request, const char *username)
{
  g_return_if_fail (request != NULL);

  if (request->username)
    g_free (request->username);

  if (username != NULL)
    request->username = g_strdup (username);
  else
    request->username = NULL;
}


void
gftp_set_password (gftp_request * request, const char *password)
{
  g_return_if_fail (request != NULL);

  if (request->password)
    g_free (request->password);

  if (password != NULL)
    request->password = g_strdup (password);
  else
    request->password = NULL;
}


void
gftp_set_account (gftp_request * request, const char *account)
{
  g_return_if_fail (request != NULL);
  g_return_if_fail (account != NULL);

  if (request->account)
    g_free (request->account);
  request->account = g_strdup (account);
}


int
gftp_set_directory (gftp_request * request, const char *directory)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);
  g_return_val_if_fail (directory != NULL, GFTP_EFATAL);


  if (request->datafd <= 0 && !request->always_connected)
    {
      if (directory != request->directory)
        {
          if (request->directory)
            g_free (request->directory);
          request->directory = g_strdup (directory);
        }
      return (0);
    }
  else if (request->chdir == NULL)
    return (GFTP_EFATAL);
  return (request->chdir (request, directory));
}


void
gftp_set_port (gftp_request * request, unsigned int port)
{
  g_return_if_fail (request != NULL);

  request->port = port;
}


int
gftp_remove_directory (gftp_request * request, const char *directory)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (request->rmdir == NULL)
    return (GFTP_EFATAL);
  return (request->rmdir (request, directory));
}


int
gftp_remove_file (gftp_request * request, const char *file)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (request->rmfile == NULL)
    return (GFTP_EFATAL);
  return (request->rmfile (request, file));
}


int
gftp_make_directory (gftp_request * request, const char *directory)
{
  int ret;

  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (request->mkdir == NULL)
    return (GFTP_EFATAL);

  return (request->mkdir (request, directory));
}


int
gftp_rename_file (gftp_request * request, const char *oldname,
                  const char *newname)
{
  int ret;

  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (request->rename == NULL)
    return (GFTP_EFATAL);

  return (request->rename (request, oldname, newname));
}


int
gftp_chmod (gftp_request * request, const char *file, mode_t mode)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (request->chmod == NULL)
    return (GFTP_EFATAL);

  mode &= S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX;
  return (request->chmod (request, file, mode));
}


int
gftp_set_file_time (gftp_request * request, const char *file, time_t datetime)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (request->set_file_time == NULL)
    return (GFTP_EFATAL);
  return (request->set_file_time (request, file, datetime));
}


int
gftp_site_cmd (gftp_request * request, int specify_site, const char *command)
{
  g_return_val_if_fail (request != NULL, GFTP_EFATAL);

  if (request->site == NULL)
    return (GFTP_EFATAL);
  return (request->site (request, specify_site, command));
}


off_t
gftp_get_file_size (gftp_request * request, const char *filename)
{
  g_return_val_if_fail (request != NULL, 0);

  if (request->get_file_size == NULL)
    return (0);
  return (request->get_file_size (request, filename));
}


static int
gftp_need_proxy (gftp_request * request, char *service, char *proxy_hostname, 
                 unsigned int proxy_port)
{
  gftp_config_list_vars * proxy_hosts;
  gftp_proxy_hosts * hostname;
  size_t hostlen, domlen;
  unsigned char addy[4];
  struct sockaddr *addr;
  GList * templist;
  gint32 netaddr;
  char *pos;
#if defined (HAVE_GETADDRINFO) && defined (HAVE_GAI_STRERROR)
  struct addrinfo hints;
  unsigned int port;
  int errnum;
  char serv[8];
#endif

  gftp_lookup_global_option ("dont_use_proxy", &proxy_hosts);

  if (proxy_hostname == NULL || *proxy_hostname == '\0')
    return (0);
  else if (proxy_hosts->list == NULL)
    return (proxy_hostname != NULL && 
            *proxy_hostname != '\0');

  request->hostp = NULL;
#if defined (HAVE_GETADDRINFO) && defined (HAVE_GAI_STRERROR)
  request->free_hostp = 1;
  memset (&hints, 0, sizeof (hints));
  hints.ai_flags = AI_CANONNAME;
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  port = request->use_proxy ? proxy_port : request->port;
  if (port == 0)
    strcpy (serv, service);
  else
    snprintf (serv, sizeof (serv), "%d", port);

  request->logging_function (gftp_logging_misc, request,
                             _("Looking up %s\n"), request->hostname);

  if ((errnum = getaddrinfo (request->hostname, serv, &hints, 
                             &request->hostp)) != 0)
    {
      request->logging_function (gftp_logging_error, request,
                                 _("Cannot look up hostname %s: %s\n"),
                                 request->hostname, gai_strerror (errnum));
      return (GFTP_ERETRYABLE);
    }

  addr = request->hostp->ai_addr;

#else /* !HAVE_GETADDRINFO */
  request->logging_function (gftp_logging_misc, request,
                             _("Looking up %s\n"), request->hostname);

  if (!(request->hostp = r_gethostbyname (request->hostname, &request->host,
                                          NULL)))
    {
      request->logging_function (gftp_logging_error, request,
                                 _("Cannot look up hostname %s: %s\n"),
                                 request->hostname, g_strerror (errno));
      return (GFTP_ERETRYABLE);
    }

  addr = (struct sockaddr *) request->host.h_addr_list[0];

#endif /* HAVE_GETADDRINFO */

  templist = proxy_hosts->list;
  while (templist != NULL)
    {
      hostname = templist->data;
      if (hostname->domain != NULL)
        {
           hostlen = strlen (request->hostname);
           domlen = strlen (hostname->domain);
           if (hostlen > domlen)
             {
                pos = request->hostname + hostlen - domlen;
                if (strcmp (hostname->domain, pos) == 0)
                  return (0);
             }
        }

      if (hostname->ipv4_network_address != 0)
        {
          memcpy (addy, addr, sizeof (*addy));
          netaddr =
            (((addy[0] & 0xff) << 24) | ((addy[1] & 0xff) << 16) |
             ((addy[2] & 0xff) << 8) | (addy[3] & 0xff)) & 
             hostname->ipv4_netmask;
          if (netaddr == hostname->ipv4_network_address)
            return (0);
        }
      templist = templist->next;
    }

  return (proxy_hostname != NULL && *proxy_hostname != '\0');
}


static char *
copy_token (/*@out@*/ char **dest, char *source)
{
  /* This function is used internally by gftp_parse_ls () */
  char *endpos, savepos;

  endpos = source;
  while (*endpos != ' ' && *endpos != '\t' && *endpos != '\0')
    endpos++;
  if (*endpos == '\0')
    {
      *dest = NULL;
      return (NULL);
    }

  savepos = *endpos;
  *endpos = '\0';
  *dest = g_malloc ((gulong) (endpos - source + 1));
  strcpy (*dest, source);
  *endpos = savepos;

  /* Skip the blanks till we get to the next entry */
  source = endpos + 1;
  while ((*source == ' ' || *source == '\t') && *source != '\0')
    source++;
  return (source);
}


static char *
goto_next_token (char *pos)
{
  while (*pos != ' ' && *pos != '\t' && *pos != '\0')
    pos++;

  while (*pos == ' ' || *pos == '\t')
    pos++;

  return (pos);
}


static time_t
parse_vms_time (char *str, char **endpos)
{
  struct tm curtime;
  time_t ret;

  /* 8-JUN-2004 13:04:14 */
  memset (&curtime, 0, sizeof (curtime));

  *endpos = strptime (str, "%d-%b-%Y %H:%M:%S", &curtime);
  if (*endpos == NULL)
    *endpos = strptime (str, "%d-%b-%Y %H:%M", &curtime);
  
  if (*endpos != NULL)
    {
      ret = mktime (&curtime);
      for (; **endpos == ' ' || **endpos == '\t'; (*endpos)++);
    }
  else
    {
      ret = 0;
      *endpos = goto_next_token (str);
      if (*endpos != NULL)
        *endpos = goto_next_token (*endpos);
    }

  return (ret);
}


time_t
parse_time (char *str, char **endpos)
{
  struct tm curtime, *loctime;
  time_t t, ret;
  char *tmppos;
  size_t slen;
  int i, num;

  slen = strlen (str);
  memset (&curtime, 0, sizeof (curtime));
  curtime.tm_isdst = -1;

  if (slen > 4 && isdigit ((int) str[0]) && str[2] == '-' && 
      isdigit ((int) str[3]))
    {
      /* This is how DOS will return the date/time */
      /* 07-06-99  12:57PM */

      tmppos = strptime (str, "%m-%d-%y %I:%M%p", &curtime);
    }
  else if (slen > 4 && isdigit ((int) str[0]) && str[2] == '-' && 
           isalpha (str[3]))
    {
      /* 10-Jan-2003 09:14 */
      tmppos = strptime (str, "%d-%h-%Y %H:%M", &curtime);
    }
  else if (slen > 4 && isdigit ((int) str[0]) && str[4] == '/')
    {
      /* 2003/12/25 */
      tmppos = strptime (str, "%Y/%m/%d", &curtime);
    }
  else
    {
      /* This is how most UNIX, Novell, and MacOS ftp servers send their time */
      /* Jul 06 12:57 or Jul  6  1999 */

      if (strchr (str, ':') != NULL)
        {
          tmppos = strptime (str, "%h %d %H:%M", &curtime);
          t = time (NULL);
          loctime = localtime (&t);

          if (curtime.tm_mon > loctime->tm_mon)
            curtime.tm_year = loctime->tm_year - 1;
          else
            curtime.tm_year = loctime->tm_year;
        }
      else
        tmppos = strptime (str, "%h %d %Y", &curtime);
    }

  if (tmppos != NULL)
    ret = mktime (&curtime);
  else
    ret = 0;

  if (endpos != NULL)
    {
      if (tmppos == NULL)
        {
          /* We cannot parse this date format. So, just skip this date field
             and continue to the next token. This is mainly for the HTTP 
             support */

          *endpos = str;
          for (num = 0; num < 2 && **endpos != '\0'; num++)
            {
              for (i=0; 
                   (*endpos)[i] != ' ' && (*endpos)[i] != '\t' && 
                    (*endpos)[i] != '\0'; 
                   i++);
              *endpos += i;

              for (i=0; (*endpos)[i] == ' ' || (*endpos)[i] == '\t'; i++);
              *endpos += i;
            }
        }
      else
        *endpos = tmppos;
    }

  return (ret);
}


static mode_t
gftp_parse_vms_attribs (char **src, mode_t mask)
{
  char *endpos;
  mode_t ret;

  if (*src == NULL)
    return (0);

  if ((endpos = strchr (*src, ',')) != NULL)
    *endpos = '\0';

  ret = 0;
  if (strchr (*src, 'R') != NULL)
    ret |= S_IRUSR | S_IRGRP | S_IROTH;
  if (strchr (*src, 'W') != NULL)
    ret |= S_IWUSR | S_IWGRP | S_IWOTH;
  if (strchr (*src, 'E') != NULL)
    ret |= S_IXUSR | S_IXGRP | S_IXOTH;

  *src = endpos + 1;

  return (ret & mask);
}


static int
gftp_parse_ls_vms (gftp_request * request, int fd, char *str, gftp_file * fle)
{
  char *curpos, *endpos, tempstr[1024];
  int multiline;
  ssize_t len;

  /* .PINE-DEBUG1;1              9  21-AUG-2002 20:06 [MYERSRG] (RWED,RWED,,) */
  /* WWW.DIR;1                   1  23-NOV-1999 05:47 [MYERSRG] (RWE,RWE,RE,E) */

  /* Multiline VMS 
  $MAIN.TPU$JOURNAL;1
	1/18 8-JUN-2004 13:04:14  [NUCLEAR,FISSION]      (RWED,RWED,RE,)
  TCPIP$FTP_SERVER.LOG;29
	0/18 8-JUN-2004 14:42:04  [NUCLEAR,FISSION]      (RWED,RWED,RE,)
  TCPIP$FTP_SERVER.LOG;28
	5/18 8-JUN-2004 13:05:11  [NUCLEAR,FISSION]      (RWED,RWED,RE,)
  TCPIP$FTP_SERVER.LOG;27
	5/18 8-JUN-2004 13:03:51  [NUCLEAR,FISSION]      (RWED,RWED,RE,) */

  if ((curpos = strchr (str, ';')) == NULL)
    return (GFTP_EFATAL);

  multiline = strchr (str, ' ') == NULL;

  *curpos = '\0';
  if (strlen (str) > 4 && strcmp (curpos - 4, ".DIR") == 0)
    {
      fle->st_mode |= S_IFDIR;
      *(curpos - 4) = '\0';
    }

  fle->file = g_strdup (str);

  if (multiline)
    {
      if (request->get_next_dirlist_line == NULL)
        return (GFTP_EFATAL);

      len = request->get_next_dirlist_line (request, fd, tempstr,
                                            sizeof (tempstr));
      if (len <= 0)
        return ((int) len);

      for (curpos = tempstr; *curpos == ' ' || *curpos == '\t'; curpos++);
    }
  else
    curpos = goto_next_token (curpos + 1);

  fle->size = gftp_parse_file_size (curpos) * 512; /* Is this correct? */

  curpos = goto_next_token (curpos);

  fle->datetime = parse_vms_time (curpos, &curpos);

  if (*curpos != '[')
    return (GFTP_EFATAL);

  if ((endpos = strchr (curpos, ']')) == NULL)
    return (GFTP_EFATAL);

  curpos = goto_next_token (endpos + 1);
  if ((curpos = strchr (curpos, ',')) == NULL)
    return (0);
  curpos++;

  fle->st_mode = gftp_parse_vms_attribs (&curpos, S_IRWXU);
  fle->st_mode |= gftp_parse_vms_attribs (&curpos, S_IRWXG);
  fle->st_mode |= gftp_parse_vms_attribs (&curpos, S_IRWXO);

  fle->user = g_strdup ("");
  fle->group = g_strdup ("");

  return (0);
}


static int
gftp_parse_ls_mvs (char *str, gftp_file * fle)
{
  char *curpos;

  /* Volume Unit    Referred Ext Used Recfm Lrecl BlkSz Dsorg Dsname */
  /* SVI52A 3390   2003/12/10  8  216  FB      80 27920  PS  CARDS.DELETES */
  /* SVI528 3390   2003/12/12  1    5  FB      80 24000  PO  CLIST */

  curpos = goto_next_token (str + 1);
  if (curpos == NULL)
    return (GFTP_EFATAL);

  curpos = goto_next_token (curpos + 1);
  if (curpos == NULL)
    return (GFTP_EFATAL);

  fle->datetime = parse_time (curpos, &curpos);

  curpos = goto_next_token (curpos);
  if (curpos == NULL)
    return (GFTP_EFATAL);

  curpos = goto_next_token (curpos + 1);
  if (curpos == NULL)
    return (GFTP_EFATAL);

  fle->size = gftp_parse_file_size (curpos) * 55996; 
  curpos = goto_next_token (curpos + 1);
  if (curpos == NULL)
    return (GFTP_EFATAL);

  curpos = goto_next_token (curpos + 1);
  if (curpos == NULL)
    return (GFTP_EFATAL);

  curpos = goto_next_token (curpos + 1);
  if (curpos == NULL)
    return (GFTP_EFATAL);

  curpos = goto_next_token (curpos + 1);
  if (curpos == NULL)
    return (GFTP_EFATAL);

  if (strncmp (curpos, "PS", 2) == 0)
    fle->st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  else if (strncmp (curpos, "PO", 2) == 0)
    fle->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
  else
    return (GFTP_EFATAL);

  curpos = goto_next_token (curpos + 1);

  fle->user = g_strdup (_("unknown"));
  fle->group = g_strdup (_("unknown"));
  fle->file = g_strdup (curpos);

  return (0);
}
  

static int
gftp_parse_ls_eplf (char *str, gftp_file * fle)
{
  char *startpos;
  int isdir = 0;

  startpos = str;
  while (startpos)
    {
      startpos++;
      switch (*startpos)
        {
          case '/':
            isdir = 1;
            break;
          case 's':
            fle->size = gftp_parse_file_size (startpos + 1);
            break;
          case 'm':
            fle->datetime = strtol (startpos + 1, NULL, 10);
            break;
        }
      startpos = strchr (startpos, ',');
    }

  if ((startpos = strchr (str, 9)) == NULL)
    return (GFTP_EFATAL);

  if (isdir)
    fle->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
  else
    fle->st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

  fle->file = g_strdup (startpos + 1);
  fle->user = g_strdup (_("unknown"));
  fle->group = g_strdup (_("unknown"));
  return (0);
}


static int
gftp_parse_ls_unix (gftp_request * request, char *str, size_t slen,
                    gftp_file * fle)
{
  char *endpos, *startpos, *pos, *attribs;
  int cols;

  /* If there is no space between the attribs and links field, just make one */
  if (slen > 10)
    str[10] = ' ';

  /* Determine the number of columns */
  cols = 0;
  pos = str;
  while (*pos != '\0')
    {
      while (*pos != '\0' && *pos != ' ' && *pos != '\t')
        {
          if (*pos == ':')
            break;
          pos++;
        }

      cols++;

      if (*pos == ':')
        {
          cols++;
          break;
        }

      while (*pos == ' ' || *pos == '\t')
        pos++;
    }

  startpos = str;
  /* Copy file attributes */
  if ((startpos = copy_token (&attribs, startpos)) == NULL)
    return (GFTP_EFATAL);

  if (strlen (attribs) < 10)
    return (GFTP_EFATAL);

  fle->st_mode = gftp_convert_attributes_to_mode_t (attribs);
  g_free (attribs);

  if (cols >= 9)
    {
      /* Skip the number of links */
      startpos = goto_next_token (startpos);

      /* Copy the user that owns this file */
      if ((startpos = copy_token (&fle->user, startpos)) == NULL)
        return (GFTP_EFATAL);

      /* Copy the group that owns this file */
      if ((startpos = copy_token (&fle->group, startpos)) == NULL)
        return (GFTP_EFATAL);
    }
  else
    {
      fle->group = g_strdup (_("unknown"));
      if (cols == 8)
        {
          if ((startpos = copy_token (&fle->user, startpos)) == NULL)
            return (GFTP_EFATAL);
        }
      else
        fle->user = g_strdup (_("unknown"));
      startpos = goto_next_token (startpos);
    }

  if (request->server_type == GFTP_DIRTYPE_CRAY)
    {
      /* See if this is a Cray directory listing. It has the following format:
      drwx------     2 feiliu    g913     DK  common      4096 Sep 24  2001 wv */
      if (cols == 11 && strstr (str, "->") == NULL)
        {
          startpos = goto_next_token (startpos);
          startpos = goto_next_token (startpos);
        }
    }

  /* See if this is a block or character device. We will store the major number
     in the high word and the minor number in the low word.  */
  if (GFTP_IS_SPECIAL_DEVICE (fle->st_mode) &&
      (endpos = strchr (startpos, ',')) != NULL)
    {
      fle->size = (unsigned long) strtol (startpos, NULL, 10) << 16;

      startpos = endpos + 1;
      while (*startpos == ' ')
        startpos++;

      /* Get the minor number */
      if ((endpos = strchr (startpos, ' ')) == NULL)
        return (GFTP_EFATAL);
      fle->size |= strtol (startpos, NULL, 10) & 0xFF;
    }
  else
    {
      /* This is a regular file  */
      if ((endpos = strchr (startpos, ' ')) == NULL)
        return (GFTP_EFATAL);
      fle->size = gftp_parse_file_size (startpos);
    }

  /* Skip the blanks till we get to the next entry */
  startpos = endpos + 1;
  while (*startpos == ' ')
    startpos++;

  fle->datetime = parse_time (startpos, &startpos);

  /* Skip the blanks till we get to the next entry */
  startpos = goto_next_token (startpos);

  /* Parse the filename. If this file is a symbolic link, remove the -> part */
  if (S_ISLNK (fle->st_mode) && ((endpos = strstr (startpos, "->")) != NULL))
    *(endpos - 1) = '\0';

  fle->file = g_strdup (startpos);

  /* Uncomment this if you want to strip the spaces off of the end of the file.
     I don't want to do this by default since there are valid filenames with
     spaces at the end of them. Some broken FTP servers like the Paradyne IPC
     DSLAMS append a bunch of spaces at the end of the file.
  for (endpos = fle->file + strlen (fle->file) - 1; 
       *endpos == ' '; 
       *endpos-- = '\0');
  */

  return (0);
}


static int
gftp_parse_ls_nt (char *str, gftp_file * fle)
{
  char *startpos;

  startpos = str;
  fle->datetime = parse_time (startpos, &startpos);

  fle->user = g_strdup (_("unknown"));
  fle->group = g_strdup (_("unknown"));

  startpos = goto_next_token (startpos);

  if (startpos[0] == '<')
    fle->st_mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
  else
    {
      fle->st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
      fle->size = gftp_parse_file_size (startpos);
    }

  startpos = goto_next_token (startpos);
  fle->file = g_strdup (startpos);
  return (0);
}


static int
gftp_parse_ls_novell (char *str, gftp_file * fle)
{
  char *startpos;

  if (str[12] != ' ')
    return (GFTP_EFATAL);

  str[12] = '\0';
  fle->st_mode = gftp_convert_attributes_to_mode_t (str);
  startpos = str + 13;

  while ((*startpos == ' ' || *startpos == '\t') && *startpos != '\0')
    startpos++;

  if ((startpos = copy_token (&fle->user, startpos)) == NULL)
    return (GFTP_EFATAL);

  fle->group = g_strdup (_("unknown"));

  while (*startpos != '\0' && !isdigit (*startpos))
    startpos++;

  fle->size = gftp_parse_file_size (startpos);

  startpos = goto_next_token (startpos);
  fle->datetime = parse_time (startpos, &startpos);

  startpos = goto_next_token (startpos);
  fle->file = g_strdup (startpos);
  return (0);
}


int
gftp_parse_ls (gftp_request * request, const char *lsoutput, gftp_file * fle,
               int fd)
{
  char *str, *endpos, tmpchar;
  int result, is_vms;
  size_t len;

  g_return_val_if_fail (lsoutput != NULL, GFTP_EFATAL);
  g_return_val_if_fail (fle != NULL, GFTP_EFATAL);

  str = g_strdup (lsoutput);
  memset (fle, 0, sizeof (*fle));

  len = strlen (str);
  if (len > 0 && str[len - 1] == '\n')
    str[--len] = '\0';
  if (len > 0 && str[len - 1] == '\r')
    str[--len] = '\0';

  switch (request->server_type)
    {
      case GFTP_DIRTYPE_CRAY:
      case GFTP_DIRTYPE_UNIX:
        result = gftp_parse_ls_unix (request, str, len, fle);
        break;
      case GFTP_DIRTYPE_EPLF:
        result = gftp_parse_ls_eplf (str, fle);
        break;
      case GFTP_DIRTYPE_NOVELL:
        result = gftp_parse_ls_novell (str, fle);
        break;
      case GFTP_DIRTYPE_DOS:
        result = gftp_parse_ls_nt (str, fle);
        break;
      case GFTP_DIRTYPE_VMS:
        result = gftp_parse_ls_vms (request, fd, str, fle);
        break;
      case GFTP_DIRTYPE_MVS:
        result = gftp_parse_ls_mvs (str, fle);
        break;
      default: /* autodetect */
        if (*lsoutput == '+')
          result = gftp_parse_ls_eplf (str, fle);
        else if (isdigit ((int) str[0]) && str[2] == '-')
          result = gftp_parse_ls_nt (str, fle);
        else if (str[1] == ' ' && str[2] == '[')
          result = gftp_parse_ls_novell (str, fle);
        else
          {
            if ((endpos = strchr (str, ' ')) != NULL)
              {
                /* If the first token in the string has a ; in it, then */
                /* we'll assume that this is a VMS directory listing    */
                tmpchar = *endpos;
                *endpos = '\0';
                is_vms = strchr (str, ';') != NULL;
                *endpos = tmpchar;
              }
            else
              is_vms = 0;

            if (is_vms)
              result = gftp_parse_ls_vms (request, fd, str, fle);
            else
              result = gftp_parse_ls_unix (request, str, len, fle);
          }
        break;
    }
  g_free (str);

  return (result);
}


static GHashTable *
gftp_gen_dir_hash (gftp_request * request, int *ret)
{
  GHashTable * dirhash;
  gftp_file * fle;
  off_t *newsize;

  dirhash = g_hash_table_new (string_hash_function, string_hash_compare);
  *ret = gftp_list_files (request);
  if (*ret == 0)
    {
      fle = g_malloc0 (sizeof (*fle));
      while (gftp_get_next_file (request, NULL, fle) > 0)
        {
          newsize = g_malloc (sizeof (*newsize));
          *newsize = fle->size;
          g_hash_table_insert (dirhash, fle->file, newsize);
          fle->file = NULL;
          gftp_file_destroy (fle, 0);
        }
      gftp_end_transfer (request);
      g_free (fle);
    }
  else
    {
      g_hash_table_destroy (dirhash);
      dirhash = NULL;
    }

  return (dirhash);
}


static void
destroy_hash_ent (gpointer key, gpointer value, gpointer user_data)
{

  g_free (key);
  g_free (value);
}


static void
gftp_destroy_dir_hash (GHashTable * dirhash)
{
  if (dirhash == NULL)
    return;

  g_hash_table_foreach (dirhash, destroy_hash_ent, NULL);
  g_hash_table_destroy (dirhash);
}


static GList *
gftp_get_dir_listing (gftp_transfer * transfer, int getothdir, int *ret)
{
  GHashTable * dirhash;
  GList * templist;
  gftp_file * fle;
  off_t *newsize;
  char *newname;

  if (getothdir && transfer->toreq != NULL)
    {
      dirhash = gftp_gen_dir_hash (transfer->toreq, ret);
      if (*ret == GFTP_EFATAL)
        return (NULL);
    }
  else 
    dirhash = NULL; 

  *ret = gftp_list_files (transfer->fromreq);
  if (*ret < 0)
    {
      gftp_destroy_dir_hash (dirhash);
      return (NULL);
    }

  fle = g_malloc (sizeof (*fle));
  templist = NULL;
  while (gftp_get_next_file (transfer->fromreq, NULL, fle) > 0)
    {
      if (strcmp (fle->file, ".") == 0 || strcmp (fle->file, "..") == 0)
        {
          gftp_file_destroy (fle, 0);
          continue;
        }

      if (dirhash && 
          (newsize = g_hash_table_lookup (dirhash, fle->file)) != NULL)
        {
          fle->exists_other_side = 1;
          fle->startsize = *newsize;
        }
      else
        fle->exists_other_side = 0;

      if (transfer->toreq && fle->destfile == NULL)
        fle->destfile = gftp_build_path (transfer->toreq,
                                         transfer->toreq->directory, 
                                         fle->file, NULL);

      if (transfer->fromreq->directory != NULL &&
          *transfer->fromreq->directory != '\0' &&
          *fle->file != '/')
        {
          newname = gftp_build_path (transfer->fromreq,
                                     transfer->fromreq->directory,
                                     fle->file, NULL);

          g_free (fle->file);
          fle->file = newname;
        }

      templist = g_list_append (templist, fle);

      fle = g_malloc0 (sizeof (*fle));
    }
  gftp_end_transfer (transfer->fromreq);

  gftp_file_destroy (fle, 1);
  gftp_destroy_dir_hash (dirhash);

  return (templist);
}


static void
_cleanup_get_all_subdirs (gftp_transfer * transfer, char *oldfromdir,
                          char *oldtodir,
                          void (*update_func) (gftp_transfer * transfer))
{
  if (update_func != NULL)
    {
      transfer->numfiles = transfer->numdirs = -1;
      update_func (transfer);
    }

  if (oldfromdir != NULL)
    g_free (oldfromdir);

  if (oldtodir != NULL)
    g_free (oldtodir);
}


static GList *
_setup_current_directory_transfer (gftp_transfer * transfer, int *ret)
{
  GHashTable * dirhash;
  char *pos, *newname;
  gftp_file * curfle;
  GList * lastlist;
  off_t *newsize;

  *ret = 0;
  if (transfer->toreq != NULL)
    {
      dirhash = gftp_gen_dir_hash (transfer->toreq, ret);
      if (*ret == GFTP_EFATAL)
        return (NULL);
    }
  else
    dirhash = NULL;

  for (lastlist = transfer->files; ; lastlist = lastlist->next)
    {
      curfle = lastlist->data;

      if ((pos = strrchr (curfle->file, '/')) != NULL)
        pos++;
      else
        pos = curfle->file;

      if (dirhash != NULL && 
          (newsize = g_hash_table_lookup (dirhash, pos)) != NULL)
        {
          curfle->exists_other_side = 1;
          curfle->startsize = *newsize;
        }
      else
        curfle->exists_other_side = 0;

      if (curfle->size < 0 && GFTP_IS_CONNECTED (transfer->fromreq))
        {
          curfle->size = gftp_get_file_size (transfer->fromreq, curfle->file);
          if (curfle->size == GFTP_EFATAL)
            {
              gftp_destroy_dir_hash (dirhash);
              *ret = curfle->size;
              return (NULL);
            }
        }

      if (transfer->toreq && curfle->destfile == NULL)
        curfle->destfile = gftp_build_path (transfer->toreq,
                                            transfer->toreq->directory, 
                                            curfle->file, NULL);

      if (transfer->fromreq->directory != NULL &&
          *transfer->fromreq->directory != '\0' && *curfle->file != '/')
        {
          newname = gftp_build_path (transfer->fromreq,
                                     transfer->fromreq->directory,
                                     curfle->file, NULL);
          g_free (curfle->file);
          curfle->file = newname;
        }

      if (lastlist->next == NULL)
        break;
    }

  gftp_destroy_dir_hash (dirhash);

  return (lastlist);
}


int
gftp_get_all_subdirs (gftp_transfer * transfer,
                      void (*update_func) (gftp_transfer * transfer))
{
  GList * templist, * lastlist;
  char *oldfromdir, *oldtodir;
  gftp_file * curfle;
  off_t linksize;
  mode_t st_mode;
  int ret;

  g_return_val_if_fail (transfer != NULL, GFTP_EFATAL);
  g_return_val_if_fail (transfer->fromreq != NULL, GFTP_EFATAL);
  g_return_val_if_fail (transfer->files != NULL, GFTP_EFATAL);

  if (transfer->files == NULL)
    return (0);

  ret = 0;
  lastlist = _setup_current_directory_transfer (transfer, &ret);
  if (lastlist == NULL)
    return (ret);

  oldfromdir = oldtodir = NULL;

  for (templist = transfer->files; templist != NULL; templist = templist->next)
    {
      curfle = templist->data;

      if (S_ISLNK (curfle->st_mode) && !S_ISDIR (curfle->st_mode))
        {
          st_mode = 0;
          linksize = 0;
          ret = gftp_stat_filename (transfer->fromreq, curfle->file, &st_mode,
                                    &linksize);
          if (ret < 0)
            {
              _cleanup_get_all_subdirs (transfer, oldfromdir, oldtodir,
                                        update_func);
              return (ret);
            }
          else if (S_ISDIR (st_mode))
            curfle->st_mode = st_mode;
          else
            curfle->size = linksize;
        }

      if (!S_ISDIR (curfle->st_mode))
        {
          transfer->numfiles++;
          continue;
        }

      /* Got a directory... */
      transfer->numdirs++;

      if (oldfromdir == NULL)
        oldfromdir = g_strdup (transfer->fromreq->directory);

      ret = gftp_set_directory (transfer->fromreq, curfle->file);
      if (ret < 0)
        {
          _cleanup_get_all_subdirs (transfer, oldfromdir, oldtodir,
                                    update_func);
          return (ret);
        }

      if (transfer->toreq != NULL)
        {
          if (oldtodir == NULL)
            oldtodir = g_strdup (transfer->toreq->directory);

          if (curfle->exists_other_side)
            {
              ret = gftp_set_directory (transfer->toreq, curfle->destfile);
              if (ret == GFTP_EFATAL)
                {
                  _cleanup_get_all_subdirs (transfer, oldfromdir, oldtodir,
                                            update_func);
                  return (ret);
                }
            }
          else
            {
              if (transfer->toreq->directory != NULL)
                g_free (transfer->toreq->directory);

              transfer->toreq->directory = g_strdup (curfle->destfile);
            }
        } 

      ret = 0;
      lastlist->next = gftp_get_dir_listing (transfer,
                                             curfle->exists_other_side, &ret);
      if (ret < 0)
        {
          _cleanup_get_all_subdirs (transfer, oldfromdir, oldtodir,
                                    update_func);
          return (ret);
        }

      if (lastlist->next != NULL)
        {
          lastlist->next->prev = lastlist;
          for (; lastlist->next != NULL; lastlist = lastlist->next);
        }

      if (update_func != NULL)
        update_func (transfer);
    }

  if (oldfromdir != NULL)
    {
      ret = gftp_set_directory (transfer->fromreq, oldfromdir);
      if (ret < 0)
        {
          _cleanup_get_all_subdirs (transfer, oldfromdir, oldtodir,
                                    update_func);
          return (ret);
        }
    }

  if (oldtodir != NULL)
    {
      ret = gftp_set_directory (transfer->toreq, oldtodir);
      if (ret < 0)
        {
          _cleanup_get_all_subdirs (transfer, oldfromdir, oldtodir,
                                    update_func);
          return (ret);
        }
    }

  _cleanup_get_all_subdirs (transfer, oldfromdir, oldtodir, update_func);

  return (0);
}


#if defined (HAVE_GETADDRINFO) && defined (HAVE_GAI_STRERROR)
static int
get_port (struct addrinfo *addr)
{
  struct sockaddr_in * saddr;
  int port;

  if (addr->ai_family == AF_INET)
    {
      saddr = (struct sockaddr_in *) addr->ai_addr;
      port = ntohs (saddr->sin_port);
    }
  else
    port = 0;

  return (port);
}
#endif


int
gftp_connect_server (gftp_request * request, char *service,
                     char *proxy_hostname, unsigned int proxy_port)
{
  char *connect_host, *disphost;
  unsigned int port;
  int sock = -1;
#if defined (HAVE_GETADDRINFO) && defined (HAVE_GAI_STRERROR)
  struct addrinfo hints, *res;
  intptr_t enable_ipv6;
  char serv[8];
  int errnum;

  if ((errnum = gftp_need_proxy (request, service, proxy_hostname,
                                 proxy_port)) < 0)
    return (errnum);
  else
    {
      request->use_proxy = errnum;
      if (request->use_proxy)
        request->hostp = NULL;
    }

  gftp_lookup_request_option (request, "enable_ipv6", &enable_ipv6);

  request->free_hostp = 1;
  memset (&hints, 0, sizeof (hints));
  hints.ai_flags = AI_CANONNAME;

  if (enable_ipv6)
    hints.ai_family = PF_UNSPEC;
  else
    hints.ai_family = AF_INET;

  hints.ai_socktype = SOCK_STREAM;

  if (request->use_proxy)
    {
      connect_host = proxy_hostname;
      port = proxy_port;
    }
  else
    {
      connect_host = request->hostname;
      port = request->port;
    }

  if (request->hostp == NULL)
    {
      if (port == 0)
        strcpy (serv, service); 
      else
        snprintf (serv, sizeof (serv), "%d", port);

      request->logging_function (gftp_logging_misc, request,
                                 _("Looking up %s\n"), connect_host);
      if ((errnum = getaddrinfo (connect_host, serv, &hints, 
                                 &request->hostp)) != 0)
        {
          request->logging_function (gftp_logging_error, request,
                                     _("Cannot look up hostname %s: %s\n"),
                                     connect_host, gai_strerror (errnum));
          return (GFTP_ERETRYABLE);
        }
    }

  disphost = connect_host;
  for (res = request->hostp; res != NULL; res = res->ai_next)
    {
      disphost = res->ai_canonname ? res->ai_canonname : connect_host;
      port = get_port (res);
      if (!request->use_proxy)
        request->port = port;

      if ((sock = socket (res->ai_family, res->ai_socktype, 
                          res->ai_protocol)) < 0)
        {
          request->logging_function (gftp_logging_error, request,
                                     _("Failed to create a socket: %s\n"),
                                     g_strerror (errno));
          continue; 
        } 

      request->logging_function (gftp_logging_misc, request,
                                 _("Trying %s:%d\n"), disphost, port);

      if (connect (sock, res->ai_addr, res->ai_addrlen) == -1)
        {
          request->logging_function (gftp_logging_error, request,
                                     _("Cannot connect to %s: %s\n"),
                                     disphost, g_strerror (errno));
          close (sock);
          continue;
        }

      request->current_hostp = res;
      request->ai_family = res->ai_family;
      break;
    }

  if (res == NULL)
    {
      if (request->hostp != NULL)
        {
          freeaddrinfo (request->hostp);
          request->hostp = NULL;
        }
      return (GFTP_ERETRYABLE);
    }

#else /* !HAVE_GETADDRINFO */
  struct sockaddr_in remote_address;
  struct servent serv_struct;
  int ret;

  if ((ret = gftp_need_proxy (request, service, proxy_hostname,
                              proxy_port)) < 0)
    return (ret);

  request->use_proxy = ret;
  if (request->use_proxy == 1)
    request->hostp = NULL;

  request->ai_family = AF_INET;
  if ((sock = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      request->logging_function (gftp_logging_error, request,
                                 _("Failed to create a IPv4 socket: %s\n"),
                                 g_strerror (errno));
      return (GFTP_ERETRYABLE);
    }

  memset (&remote_address, 0, sizeof (remote_address));
  remote_address.sin_family = AF_INET;

  if (request->use_proxy)
    {
      connect_host = proxy_hostname;
      port = proxy_port;
    }
  else
    {
      connect_host = request->hostname;
      port = request->port;
    }

  if (port == 0)
    {
      if (!r_getservbyname (service, "tcp", &serv_struct, NULL))
        {
          request->logging_function (gftp_logging_error, request,
                                     _("Cannot look up service name %s/tcp. Please check your services file\n"),
                                     service);
          close (sock);
          return (GFTP_EFATAL);
        }

      port = ntohs (serv_struct.s_port);

      if (!request->use_proxy)
        request->port = port;
    }

  remote_address.sin_port = htons (port);

  if (request->hostp == NULL)
    {
      request->logging_function (gftp_logging_misc, request,
                                 _("Looking up %s\n"), connect_host);
      if (!(request->hostp = r_gethostbyname (connect_host, &request->host,
                                              NULL)))
        {
          request->logging_function (gftp_logging_error, request,
                                     _("Cannot look up hostname %s: %s\n"),
                                     connect_host, g_strerror (errno));
          close (sock);
          return (GFTP_ERETRYABLE);
        }
    }

  disphost = NULL;
  for (request->curhost = 0;
       request->host.h_addr_list[request->curhost] != NULL;
       request->curhost++)
    {
      disphost = request->host.h_name;
      memcpy (&remote_address.sin_addr,
              request->host.h_addr_list[request->curhost],
              request->host.h_length);
      request->logging_function (gftp_logging_misc, request,
                                 _("Trying %s:%d\n"),
                                 request->host.h_name, port);

      if (connect (sock, (struct sockaddr *) &remote_address,
                   sizeof (remote_address)) == -1)
        {
          request->logging_function (gftp_logging_error, request,
                                     _("Cannot connect to %s: %s\n"),
                                     connect_host, g_strerror (errno));
        }
      break;
    }

  if (request->host.h_addr_list[request->curhost] == NULL)
    {
      close (sock);
      return (GFTP_ERETRYABLE);
    }
#endif /* HAVE_GETADDRINFO */

  if (fcntl (sock, F_SETFD, 1) == -1)
    {
      request->logging_function (gftp_logging_error, request,
                                 _("Error: Cannot set close on exec flag: %s\n"),
                                 g_strerror (errno));

      return (GFTP_ERETRYABLE);
    }

  request->logging_function (gftp_logging_misc, request,
                             _("Connected to %s:%d\n"), connect_host, port);

  if (gftp_fd_set_sockblocking (request, sock, 1) < 0)
    {
      close (sock);
      return (GFTP_ERETRYABLE);
    }

  request->datafd = sock;

  if (request->post_connect != NULL)
    return (request->post_connect (request));

  return (0);
}


int
gftp_set_config_options (gftp_request * request)
{
  if (request->set_config_options != NULL)
    return (request->set_config_options (request));
  else
    return (0);
}


void
print_file_list (GList * list)
{
  gftp_file * tempfle;
  GList * templist;
  char *attribs;

  printf ("--START OF FILE LISTING - TOP TO BOTTOM--\n");
  for (templist = list; ; templist = templist->next)
    {
      tempfle = templist->data;
      attribs = gftp_convert_attributes_from_mode_t (tempfle->st_mode);

      printf ("%s:%s:" GFTP_OFF_T_PRINTF_MOD ":" GFTP_OFF_T_PRINTF_MOD ":%s:%s:%s\n", 
              tempfle->file, tempfle->destfile, 
              tempfle->size, tempfle->startsize, 
              tempfle->user, tempfle->group, attribs);

      g_free (attribs);
      if (templist->next == NULL)
        break;
    }

  printf ("--START OF FILE LISTING - BOTTOM TO TOP--\n");
  for (; ; templist = templist->prev)
    {
      tempfle = templist->data;
      attribs = gftp_convert_attributes_from_mode_t (tempfle->st_mode);

      printf ("%s:%s:" GFTP_OFF_T_PRINTF_MOD ":" GFTP_OFF_T_PRINTF_MOD ":%s:%s:%s\n", 
              tempfle->file, tempfle->destfile, 
              tempfle->size, tempfle->startsize, 
              tempfle->user, tempfle->group, attribs);

      g_free (attribs);
      if (templist == list)
        break;
    }
  printf ("--END OF FILE LISTING--\n");
}


void
gftp_free_getline_buffer (gftp_getline_buffer ** rbuf)
{
  g_free ((*rbuf)->buffer);
  g_free (*rbuf);
  *rbuf = NULL;
}


ssize_t
gftp_get_line (gftp_request * request, gftp_getline_buffer ** rbuf, 
               char * str, size_t len, int fd)
{
  ssize_t (*read_function) (gftp_request * request, void *ptr, size_t size,
                            int fd);
  char *pos, *nextpos;
  size_t rlen, nslen;
  int end_of_buffer;
  ssize_t ret;

  if (request == NULL || request->read_function == NULL)
    read_function = gftp_fd_read;
  else
    read_function = request->read_function;

  if (*rbuf == NULL)
    {
      *rbuf = g_malloc0 (sizeof (**rbuf));
      (*rbuf)->max_bufsize = len;
      (*rbuf)->buffer = g_malloc0 ((gulong) ((*rbuf)->max_bufsize + 1));

      if ((ret = read_function (request, (*rbuf)->buffer, 
                                (*rbuf)->max_bufsize, fd)) <= 0)
        {
          gftp_free_getline_buffer (rbuf);
          return (ret);
        }
      (*rbuf)->buffer[ret] = '\0';
      (*rbuf)->cur_bufsize = ret;
      (*rbuf)->curpos = (*rbuf)->buffer;
    }

  ret = 0;
  while (1)
    {
      pos = strchr ((*rbuf)->curpos, '\n');
      end_of_buffer = (*rbuf)->curpos == (*rbuf)->buffer && 
            ((*rbuf)->max_bufsize == (*rbuf)->cur_bufsize || (*rbuf)->eof);

      if ((*rbuf)->cur_bufsize > 0 && (pos != NULL || end_of_buffer))
        {
          if (pos != NULL)
            {
              nslen = pos - (*rbuf)->curpos + 1;
              nextpos = pos + 1;
              if (pos > (*rbuf)->curpos && *(pos - 1) == '\r')
                pos--;
              *pos = '\0';
            }
          else
            {
              nslen = (*rbuf)->cur_bufsize;
              nextpos = NULL;

              /* This is not an overflow since we allocated one extra byte to
                 buffer above */
              ((*rbuf)->buffer)[nslen] = '\0';
            }

          strncpy (str, (*rbuf)->curpos, len);
          str[len - 1] = '\0';
          (*rbuf)->cur_bufsize -= nslen;

          if (nextpos != NULL)
            (*rbuf)->curpos = nextpos;
          else
            (*rbuf)->cur_bufsize = 0;

          ret = nslen;
          break;
        }
      else
        {
          if ((*rbuf)->cur_bufsize == 0 || *(*rbuf)->curpos == '\0')
            {
              rlen = (*rbuf)->max_bufsize;
              pos = (*rbuf)->buffer;
            }
          else
            {
              memmove ((*rbuf)->buffer, (*rbuf)->curpos, (*rbuf)->cur_bufsize);
              pos = (*rbuf)->buffer + (*rbuf)->cur_bufsize;
              rlen = (*rbuf)->max_bufsize - (*rbuf)->cur_bufsize;
            }

          (*rbuf)->curpos = (*rbuf)->buffer;

          if ((*rbuf)->eof)
            ret = 0;
          else
            {
              ret = read_function (request, pos, rlen, fd);
              if (ret < 0)
                {
                  gftp_free_getline_buffer (rbuf);
                  return (ret);
                }
            }

          if (ret == 0)
            {
              if ((*rbuf)->cur_bufsize == 0)
                {
                  gftp_free_getline_buffer (rbuf);
                  return (ret);
                }

              (*rbuf)->eof = 1;
            }

          (*rbuf)->cur_bufsize += ret;
          (*rbuf)->buffer[(*rbuf)->cur_bufsize] = '\0';
        }
    }

  return (ret);
}


ssize_t 
gftp_fd_read (gftp_request * request, void *ptr, size_t size, int fd)
{
  intptr_t network_timeout;
  struct timeval tv;
  fd_set fset;
  ssize_t ret;
  int s_ret;

  g_return_val_if_fail (fd >= 0, GFTP_EFATAL);

  gftp_lookup_request_option (request, "network_timeout", &network_timeout);  

  errno = 0;
  ret = 0;

  do
    {
      FD_ZERO (&fset);
      FD_SET (fd, &fset);
      tv.tv_sec = network_timeout;
      tv.tv_usec = 0;
      s_ret = select (fd + 1, &fset, NULL, NULL, &tv);
      if (s_ret == -1 && (errno == EINTR || errno == EAGAIN))
        {
          if (request != NULL && request->cancel)
            {
              gftp_disconnect (request);
              return (GFTP_ERETRYABLE);
            }

          continue;
        }
      else if (s_ret <= 0)
        {
          if (request != NULL)
            {
              request->logging_function (gftp_logging_error, request,
                                         _("Connection to %s timed out\n"),
                                         request->hostname);
              gftp_disconnect (request);
            }

          return (GFTP_ERETRYABLE);
        }

      if ((ret = read (fd, ptr, size)) < 0)
        {
          if (errno == EINTR || errno == EAGAIN)
            {
              if (request != NULL && request->cancel)
                {
                  gftp_disconnect (request);
                  return (GFTP_ERETRYABLE);
                }

              continue;
            }
 
          if (request != NULL)
            {
              request->logging_function (gftp_logging_error, request,
                                   _("Error: Could not read from socket: %s\n"),
                                    g_strerror (errno));
              gftp_disconnect (request);
            }

          return (GFTP_ERETRYABLE);
        }

      break;
    }
  while (1);

  return (ret);
}


ssize_t 
gftp_fd_write (gftp_request * request, const char *ptr, size_t size, int fd)
{
  intptr_t network_timeout;
  struct timeval tv;
  int ret, s_ret;
  ssize_t w_ret;
  fd_set fset;

  g_return_val_if_fail (fd >= 0, GFTP_EFATAL);

  gftp_lookup_request_option (request, "network_timeout", &network_timeout);  

  errno = 0;
  ret = 0;
  do
    {
      FD_ZERO (&fset);
      FD_SET (fd, &fset);
      tv.tv_sec = network_timeout;
      tv.tv_usec = 0;
      s_ret = select (fd + 1, NULL, &fset, NULL, &tv);
      if (s_ret == -1 && (errno == EINTR || errno == EAGAIN))
        {
          if (request != NULL && request->cancel)
            {
              gftp_disconnect (request);
              return (GFTP_ERETRYABLE);
            }

          continue;
        }
      else if (s_ret <= 0)
        {
          if (request != NULL)
            {
              request->logging_function (gftp_logging_error, request,
                                         _("Connection to %s timed out\n"),
                                         request->hostname);
              gftp_disconnect (request);
            }

          return (GFTP_ERETRYABLE);
        }

      w_ret = write (fd, ptr, size);
      if (w_ret < 0)
        {
          if (errno == EINTR || errno == EAGAIN)
            {
              if (request != NULL && request->cancel)
                {
                  gftp_disconnect (request);
                  return (GFTP_ERETRYABLE);
                }

              continue;
             }
 
          if (request != NULL)
            {
              request->logging_function (gftp_logging_error, request,
                                    _("Error: Could not write to socket: %s\n"),
                                    g_strerror (errno));
              gftp_disconnect (request);
            }

          return (GFTP_ERETRYABLE);
        }

      ptr += w_ret;
      size -= w_ret;
      ret += w_ret;
    }
  while (size > 0);

  return (ret);
}


ssize_t 
gftp_writefmt (gftp_request * request, int fd, const char *fmt, ...)
{
  char *tempstr;
  va_list argp;
  ssize_t ret;

  va_start (argp, fmt);
  tempstr = g_strdup_vprintf (fmt, argp);
  va_end (argp);

  ret = request->write_function (request, tempstr, strlen (tempstr), fd);
  g_free (tempstr);
  return (ret);
}


int
gftp_fd_set_sockblocking (gftp_request * request, int fd, int non_blocking)
{
  int flags;

  g_return_val_if_fail (fd >= 0, GFTP_EFATAL);

  if ((flags = fcntl (fd, F_GETFL, 0)) < 0)
    {
      request->logging_function (gftp_logging_error, request,
                                 _("Cannot get socket flags: %s\n"),
                                 g_strerror (errno));
      gftp_disconnect (request);
      return (GFTP_ERETRYABLE);
    }

  if (non_blocking)
    flags |= O_NONBLOCK;
  else
    flags &= ~O_NONBLOCK;

  if (fcntl (fd, F_SETFL, flags) < 0)
    {
      request->logging_function (gftp_logging_error, request,
                                 _("Cannot set socket to non-blocking: %s\n"),
                                 g_strerror (errno));
      gftp_disconnect (request);
      return (GFTP_ERETRYABLE);
    }

  return (0);
}


void
gftp_swap_socks (gftp_request * dest, gftp_request * source)
{
  g_return_if_fail (dest != NULL);
  g_return_if_fail (source != NULL);
  g_return_if_fail (dest->protonum == source->protonum);

  dest->datafd = source->datafd;
  dest->cached = 0;
#ifdef USE_SSL
  dest->ssl = source->ssl;
#endif

  if (!source->always_connected)
    {
      source->datafd = -1;
      source->cached = 1;
#ifdef USE_SSL
      source->ssl = NULL;
#endif
    }

  if (dest->swap_socks != NULL)
    dest->swap_socks (dest, source);
}


void
gftp_calc_kbs (gftp_transfer * tdata, ssize_t num_read)
{
  /* Needed for systems that size(float) < size(void *) */
  union { intptr_t i; float f; } maxkbs;
  unsigned long waitusecs;
  double start_difftime;
  struct timeval tv;
  int waited;

  gftp_lookup_request_option (tdata->fromreq, "maxkbs", &maxkbs.f);

  if (g_thread_supported ())
    g_static_mutex_lock (&tdata->statmutex);

  gettimeofday (&tv, NULL);

  tdata->trans_bytes += num_read;
  tdata->curtrans += num_read;
  tdata->stalled = 0;

  start_difftime = (tv.tv_sec - tdata->starttime.tv_sec) + ((double) (tv.tv_usec - tdata->starttime.tv_usec) / 1000000.0);

  if (start_difftime <= 0)
    tdata->kbs = tdata->trans_bytes / 1024.0;
  else
    tdata->kbs = tdata->trans_bytes / 1024.0 / start_difftime;

  waited = 0;
  if (maxkbs.f > 0 && tdata->kbs > maxkbs.f)
    {
      waitusecs = num_read / 1024.0 / maxkbs.f * 1000000.0 - start_difftime;

      if (waitusecs > 0)
        {
          if (g_thread_supported ())
            g_static_mutex_unlock (&tdata->statmutex);

          waited = 1;
          usleep (waitusecs);

          if (g_thread_supported ())
            g_static_mutex_lock (&tdata->statmutex);
        }

    }

  if (waited)
    gettimeofday (&tdata->lasttime, NULL);
  else
    memcpy (&tdata->lasttime, &tv, sizeof (tdata->lasttime));

  if (g_thread_supported ())
    g_static_mutex_unlock (&tdata->statmutex);
}


static int
_do_sleep (int sleep_time)
{
  struct timeval tv;
  int ret;

  tv.tv_sec = sleep_time;
  tv.tv_usec = 0;

  /* FIXME - check for user aborted connection */
  do
    {
      ret = select (0, NULL, NULL, NULL, &tv);
    }
  while (ret == -1 && (errno == EINTR || errno == EAGAIN));

  return (ret);
}


int
gftp_get_transfer_status (gftp_transfer * tdata, ssize_t num_read)
{
  intptr_t retries, sleep_time;
  gftp_file * tempfle;
  int ret1, ret2;

  gftp_lookup_request_option (tdata->fromreq, "retries", &retries);
  gftp_lookup_request_option (tdata->fromreq, "sleep_time", &sleep_time);

  if (g_thread_supported ())
    g_static_mutex_lock (&tdata->structmutex);

  if (tdata->curfle == NULL)
    {
      if (g_thread_supported ())
        g_static_mutex_unlock (&tdata->structmutex);

      return (GFTP_EFATAL);
    }

  tempfle = tdata->curfle->data;

  if (g_thread_supported ())
    g_static_mutex_unlock (&tdata->structmutex);

  gftp_disconnect (tdata->fromreq);
  gftp_disconnect (tdata->toreq);

  if (tdata->cancel || num_read == GFTP_EFATAL)
    return (GFTP_EFATAL);
  else if (num_read >= 0 && !tdata->skip_file)
    return (0);

  if (num_read != GFTP_ETIMEDOUT && !tdata->conn_error_no_timeout)
    {
      if (retries != 0 && 
          tdata->current_file_retries >= retries)
        {
          tdata->fromreq->logging_function (gftp_logging_error, tdata->fromreq,
                   _("Error: Remote site %s disconnected. Max retries reached...giving up\n"),
                   tdata->fromreq->hostname != NULL ? 
                         tdata->fromreq->hostname : tdata->toreq->hostname);
          return (GFTP_EFATAL);
        }
      else
        {
          tdata->fromreq->logging_function (gftp_logging_error, tdata->fromreq,
                     _("Error: Remote site %s disconnected. Will reconnect in %d seconds\n"),
                     tdata->fromreq->hostname != NULL ? 
                         tdata->fromreq->hostname : tdata->toreq->hostname, 
                     sleep_time);
        }
    }

  while (retries == 0 || 
         tdata->current_file_retries <= retries)
    {
      /* Look up the options in case the user changes them... */
      gftp_lookup_request_option (tdata->fromreq, "retries", &retries);
      gftp_lookup_request_option (tdata->fromreq, "sleep_time", &sleep_time);

      if (num_read != GFTP_ETIMEDOUT && !tdata->conn_error_no_timeout &&
          !tdata->skip_file)
        _do_sleep (sleep_time);

      tdata->current_file_retries++;

      ret1 = ret2 = 0;
      if ((ret1 = gftp_connect (tdata->fromreq)) == 0 &&
          (ret2 = gftp_connect (tdata->toreq)) == 0)
        {
          if (g_thread_supported ())
            g_static_mutex_lock (&tdata->structmutex);

          tdata->resumed_bytes = tdata->resumed_bytes + tdata->trans_bytes - tdata->curresumed - tdata->curtrans;
          tdata->trans_bytes = 0;
          if (tdata->skip_file)
            {
              tdata->total_bytes -= tempfle->size;
              tdata->curtrans = 0;

              tdata->curfle = tdata->curfle->next;
              tdata->next_file = 1;
              tdata->skip_file = 0;
              tdata->cancel = 0;
              tdata->fromreq->cancel = 0;
              tdata->toreq->cancel = 0;
            }
          else
            {
              tempfle->transfer_action = GFTP_TRANS_ACTION_RESUME;
              tempfle->startsize = tdata->curtrans + tdata->curresumed;
              /* We decrement this here because it will be incremented in 
                 the loop again */
              tdata->curresumed = 0;
              tdata->current_file_number--; /* Decrement this because it 
                                               will be incremented when we 
                                               continue in the loop */
            }

          gettimeofday (&tdata->starttime, NULL);

          if (g_thread_supported ())
            g_static_mutex_unlock (&tdata->structmutex);

          return (GFTP_ERETRYABLE);
        }
      else if (ret1 == GFTP_EFATAL || ret2 == GFTP_EFATAL)
        {
          gftp_disconnect (tdata->fromreq);
          gftp_disconnect (tdata->toreq);
          return (GFTP_EFATAL);
        }
    }

  return (0);
}


int
gftp_fd_open (gftp_request * request, const char *pathname, int flags, mode_t mode)
{
  int fd;

  if (mode == 0)
    fd = open (pathname, flags);
  else
    fd = open (pathname, flags, mode);

  if (fd < 0)
    {
      if (request != NULL)
        request->logging_function (gftp_logging_error, request,
                                   _("Error: Cannot open local file %s: %s\n"),
                                   pathname, g_strerror (errno));
      return (GFTP_ERETRYABLE);
    }

  if (fcntl (fd, F_SETFD, 1) == -1)
    {
      if (request != NULL)
        request->logging_function (gftp_logging_error, request,
                                   _("Error: Cannot set close on exec flag: %s\n"),
                                   g_strerror (errno));

      return (-1);
    }

  return (fd);
}


void
gftp_setup_startup_directory (gftp_request * request, const char *option_name)
{
  char *startup_directory, *tempstr;

  gftp_lookup_request_option (request, option_name, &startup_directory);

  if (*startup_directory != '\0' &&
      (tempstr = gftp_expand_path (request, startup_directory)) != NULL)
    {
      gftp_set_directory (request, tempstr);
      g_free (tempstr);
    }
}


char *
gftp_convert_attributes_from_mode_t (mode_t mode)
{
  char *str;

  str = g_malloc0 (11UL);
  
  str[0] = '?';
  if (S_ISREG (mode))
    str[0] = '-';

  if (S_ISLNK (mode))
    str[0] = 'l';

  if (S_ISBLK (mode))
    str[0] = 'b';

  if (S_ISCHR (mode))
    str[0] = 'c';

  if (S_ISFIFO (mode))
    str[0] = 'p';

  if (S_ISSOCK (mode))
    str[0] = 's';

  if (S_ISDIR (mode))
    str[0] = 'd';

  str[1] = mode & S_IRUSR ? 'r' : '-';
  str[2] = mode & S_IWUSR ? 'w' : '-';

  if ((mode & S_ISUID) && (mode & S_IXUSR))
    str[3] = 's';
  else if (mode & S_ISUID)
    str[3] = 'S';
  else if (mode & S_IXUSR)
    str[3] = 'x';
  else
    str[3] = '-';
    
  str[4] = mode & S_IRGRP ? 'r' : '-';
  str[5] = mode & S_IWGRP ? 'w' : '-';

  if ((mode & S_ISGID) && (mode & S_IXGRP))
    str[6] = 's';
  else if (mode & S_ISGID)
    str[6] = 'S';
  else if (mode & S_IXGRP)
    str[6] = 'x';
  else
    str[6] = '-';

  str[7] = mode & S_IROTH ? 'r' : '-';
  str[8] = mode & S_IWOTH ? 'w' : '-';

  if ((mode & S_ISVTX) && (mode & S_IXOTH))
    str[9] = 't';
  else if (mode & S_ISVTX)
    str[9] = 'T';
  else if (mode & S_IXOTH)
    str[9] = 'x';
  else
    str[9] = '-';

  return (str);
}


mode_t
gftp_convert_attributes_to_mode_t (char *attribs)
{
  mode_t mode;

  if (attribs[0] == 'd')
    mode = S_IFDIR;
  else if (attribs[0] == 'l')
    mode = S_IFLNK;
  else if (attribs[0] == 's')
    mode = S_IFSOCK;
  else if (attribs[0] == 'b')
    mode = S_IFBLK;
  else if (attribs[0] == 'c')
    mode = S_IFCHR;
  else
    mode = S_IFREG;

  if (attribs[1] == 'r')
    mode |= S_IRUSR;
  if (attribs[2] == 'w')
    mode |= S_IWUSR;
  if (attribs[3] == 'x' || attribs[3] == 's')
    mode |= S_IXUSR;
  if (attribs[3] == 's' || attribs[3] == 'S')
    mode |= S_ISUID;

  if (attribs[4] == 'r')
    mode |= S_IRGRP;
  if (attribs[5] == 'w')
    mode |= S_IWGRP;
  if (attribs[6] == 'x' ||
      attribs[6] == 's')
    mode |= S_IXGRP;
  if (attribs[6] == 's' || attribs[6] == 'S')
    mode |= S_ISGID;

  if (attribs[7] == 'r')
    mode |= S_IROTH;
  if (attribs[8] == 'w')
    mode |= S_IWOTH;
  if (attribs[9] == 'x' ||
      attribs[9] == 's')
    mode |= S_IXOTH;
  if (attribs[9] == 't' || attribs[9] == 'T')
    mode |= S_ISVTX;

  return (mode);
}


unsigned int
gftp_protocol_default_port (gftp_request * request)
{
  struct servent serv_struct;

  if (r_getservbyname (gftp_protocols[request->protonum].url_prefix, "tcp",
                       &serv_struct, NULL) == NULL)
    return (gftp_protocols[request->protonum].default_port);
  else
    return (ntohs (serv_struct.s_port));
}

