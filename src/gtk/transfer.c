/*****************************************************************************/
/*  transfer.c - functions to handle transfering files                       */
/*  Copyright (C) 1998-2002 Brian Masney <masneyb@gftp.org>                  */
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
/*  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.                */
/*****************************************************************************/

#include <gftp-gtk.h>
static const char cvsid[] = "$Id$";

static GtkWidget * dialog;
static int num_transfers_in_progress = 0;

static void 
wakeup_main_thread (gpointer data, gint source, GdkInputCondition condition)
{
  gftp_request * request;
  char c;
 
  request = data; 
  if (request->wakeup_main_thread[0] > 0)
    read (request->wakeup_main_thread[0], &c, 1);
}


static gint
setup_wakeup_main_thread (gftp_request * request)
{
  gint handler;

  if (socketpair (AF_UNIX, SOCK_STREAM, 0, request->wakeup_main_thread) == 0)
    {
      handler = gdk_input_add (request->wakeup_main_thread[0], 
                               GDK_INPUT_READ, wakeup_main_thread, request);
    }
  else
    {
      request->wakeup_main_thread[0] = 0;
      request->wakeup_main_thread[1] = 0;
      handler = 0;
    }
  return (handler);
}


static void
teardown_wakeup_main_thread (gftp_request * request, gint handler)
{
  if (request->wakeup_main_thread[0] > 0 && request->wakeup_main_thread[1] > 0)
    {
      gdk_input_remove (handler);
      close (request->wakeup_main_thread[0]);
      close (request->wakeup_main_thread[1]);
      request->wakeup_main_thread[0] = 0;
      request->wakeup_main_thread[1] = 0;
    }
}


static void *
getdir_thread (void * data)
{
  int sj, havedotdot, got;
  gftp_request * request;
  gftp_file * fle;
  GList * files;

  request = data;
  
  if (request->use_threads)
    {
      sj = sigsetjmp (jmp_environment, 1);
      use_jmp_environment = 1;
    }
  else
    sj = 0;

  files = NULL;
  if (sj == 0 || sj == 2)
    {
      if (gftp_list_files (request) != 0 || !GFTP_IS_CONNECTED (request))
        {
          if (request->use_threads)
            use_jmp_environment = 0;

          request->stopable = 0;
          if (request->wakeup_main_thread[1] > 0)
            write (request->wakeup_main_thread[1], " ", 1);
          return (NULL);
        }

      request->gotbytes = 0; 
      havedotdot = 0; 
      fle = g_malloc0 (sizeof (*fle));
      while ((got = gftp_get_next_file (request, NULL, fle)) > 0)
        { 
          request->gotbytes += got;
          if (strcmp (fle->file, ".") == 0)
            {
              gftp_file_destroy (fle);
              continue;
            }
          else if (strcmp (fle->file, "..") == 0)
            havedotdot = 1;

          files = g_list_append (files, fle);
          fle = g_malloc0 (sizeof (*fle));
        }
      g_free (fle);

      if (!GFTP_IS_CONNECTED (request))
        {
          if (request->use_threads)
            use_jmp_environment = 0;

          request->stopable = 0;
          if (request->wakeup_main_thread[1] > 0)
            write (request->wakeup_main_thread[1], " ", 1);
          return (NULL);
        }

      gftp_end_transfer (request); 
      request->gotbytes = -1; 

      if (!havedotdot)
        {
          fle = g_malloc0 (sizeof (*fle));
          fle->file = g_strdup ("..");
          fle->user = g_malloc0 (1);
          fle->group = g_malloc0 (1);
          fle->attribs = g_malloc0 (1);
          *fle->attribs = '\0';
          fle->isdir = 1;
          files = g_list_prepend (files, fle);
        }
    }

  if (request->use_threads)
    use_jmp_environment = 0;

  request->stopable = 0;
  if (request->wakeup_main_thread[1] > 0)
    write (request->wakeup_main_thread[1], " ", 1);
  return (files);
}


int
ftp_list_files (gftp_window_data * wdata, int usecache)
{
  guint handler;
  void *success;

  gtk_label_set (GTK_LABEL (wdata->hoststxt), _("Receiving file names..."));

  wdata->show_selected = 0;
  if (wdata->files == NULL)
    {
      if (check_reconnect (wdata) < 0)
        return (0);

      gtk_clist_freeze (GTK_CLIST (wdata->listbox));
      wdata->request->stopable = 1;
      if (wdata->request->use_threads)
        {
          gtk_widget_set_sensitive (stop_btn, 1);

          handler = setup_wakeup_main_thread (wdata->request);
          pthread_create (&wdata->tid, NULL, getdir_thread, wdata->request);
          while (wdata->request->stopable)
            {
              GDK_THREADS_LEAVE ();
#if GTK_MAJOR_VERSION == 1
              g_main_iteration (TRUE);
#else
              g_main_context_iteration (NULL, TRUE);
#endif
            }
          teardown_wakeup_main_thread (wdata->request, handler);

          pthread_join (wdata->tid, &success);
          gtk_widget_set_sensitive (stop_btn, 0);
        }
      else
        success = getdir_thread (wdata->request);
      wdata->files = success;
      gtk_clist_thaw (GTK_CLIST (wdata->listbox));
      memset (&wdata->tid, 0, sizeof (wdata->tid));
    }
  
  if (wdata->files == NULL || !GFTP_IS_CONNECTED (wdata->request))
    {
      disconnect (wdata);
      return (0);
    }

  wdata->sorted = 0;
  sortrows (GTK_CLIST (wdata->listbox), -1, (gpointer) wdata);
  if (IS_NONE_SELECTED (wdata))
    gtk_clist_select_row (GTK_CLIST (wdata->listbox), 0, 0);
  return (1);
}


static void
try_connect_again (gftp_request * request, gftp_dialog_data * ddata)
{
  gftp_set_password (request, gtk_entry_get_text (GTK_ENTRY (ddata->edit)));
  request->stopable = 0;
}


static void
dont_connect_again (gftp_request * request, gftp_dialog_data * ddata)
{
  request->stopable = 0;
}


static void *
connect_thread (void *data)
{
  int ret, sj, retries, sleep_time, network_timeout;
  static int conn_num;
  gftp_request * request;

  request = data;

  gftp_lookup_request_option (request, "retries", &retries);
  gftp_lookup_request_option (request, "sleep_time", &sleep_time);
  gftp_lookup_request_option (request, "network_timeout", &network_timeout);

  conn_num = 0;
  if (request->use_threads)
    {
      sj = sigsetjmp (jmp_environment, 1);
      use_jmp_environment = 1;
    }
  else
    sj = 0;

  ret = 0;
  if (sj != 0)
    {
      ret = 0;
      gftp_disconnect (request);
    }

  while (sj != 1 && (retries == 0 || conn_num < retries))
    {
      conn_num++;
      if (network_timeout > 0)
        alarm (network_timeout);
      ret = gftp_connect (request);
      alarm (0);

      if (ret == GFTP_EFATAL)
        {
          ret = 0;
          break;
        }
      else if (ret == 0)
        {
          ret = 1;
          break;
        }
      else if (retries == 0 || conn_num < retries)
        {
          request->logging_function (gftp_logging_misc, request->user_data,
                     _("Waiting %d seconds until trying to connect again\n"),
		     sleep_time);
          alarm (sleep_time);
          pause ();
        }  
    }

  if (request->use_threads)
    use_jmp_environment = 0;

  request->stopable = 0;
  if (request->wakeup_main_thread[1] > 0)
    write (request->wakeup_main_thread[1], " ", 1);
  return ((void *) ret);
}


int
ftp_connect (gftp_window_data * wdata, gftp_request * request, int getdir)
{
  int success;
  guint handler;
  void *ret;

  ret = 0;
  if (wdata->request == request)
    {
      gtk_label_set (GTK_LABEL (wdata->hoststxt), _("Connecting..."));
    }

  if (request->need_userpass && request->username != NULL &&
      *request->username != '\0' &&
      (request->password == NULL || *request->password == '\0'))
    {
      if (wdata && wdata->request == request)
        {
          request->stopable = 1;
          MakeEditDialog (_("Enter Password"),
                          _("Please enter your password for this site"), NULL,
                          0, NULL, gftp_dialog_button_connect, 
                          try_connect_again, request, 
                          dont_connect_again, request);

          while (request->stopable)
            {
              GDK_THREADS_LEAVE ();
#if GTK_MAJOR_VERSION == 1
              g_main_iteration (TRUE);
#else
              g_main_context_iteration (NULL, TRUE);
#endif
            }

          if (request->password == NULL || *request->password == '\0')
            return (0);
        }
      else
        gftp_set_password (request, "");
    }

  if (wdata && wdata->request == request && request->use_threads)
    {
      request->stopable = 1;
      if (wdata)       
        gtk_clist_freeze (GTK_CLIST (wdata->listbox));
      gtk_widget_set_sensitive (stop_btn, 1);
      pthread_create (&wdata->tid, NULL, connect_thread, request);

      handler = setup_wakeup_main_thread (wdata->request);
      while (request->stopable)
        {
          GDK_THREADS_LEAVE ();
#if GTK_MAJOR_VERSION == 1
          g_main_iteration (TRUE);
#else
          g_main_context_iteration (NULL, TRUE);
#endif
        }
      pthread_join (wdata->tid, &ret);
      teardown_wakeup_main_thread (wdata->request, handler);

      gtk_widget_set_sensitive (stop_btn, 0);
      if (wdata)
        gtk_clist_thaw (GTK_CLIST (wdata->listbox));
    }
  else
    ret = connect_thread (request);
  success = (int) ret;
  memset (&wdata->tid, 0, sizeof (wdata->tid));

  if (!GFTP_IS_CONNECTED (wdata->request))
    disconnect (wdata);
  else if (success)
    {
      ftp_list_files (wdata, 1);
      if (!GFTP_IS_CONNECTED (wdata->request))
        disconnect (wdata);
    }

  return (success);
}


void 
get_files (gpointer data)
{
  transfer_window_files (&window2, &window1);
}


void
put_files (gpointer data)
{
  transfer_window_files (&window1, &window2);
}


void
transfer_window_files (gftp_window_data * fromwdata, gftp_window_data * towdata)
{
  gftp_file * tempfle, * newfle;
  GList * templist, * filelist;
  gftp_transfer * transfer;
  guint timeout_num;
  void *ret;
  int num;

  if (!check_status (_("Transfer Files"), fromwdata, 1, 0, 1,
       towdata->request->put_file != NULL && fromwdata->request->get_file != NULL))
    return;

  if (!GFTP_IS_CONNECTED (fromwdata->request) || 
      !GFTP_IS_CONNECTED (towdata->request))
    {
      ftp_log (gftp_logging_misc, NULL,
               _("Retrieve Files: Not connected to a remote site\n"));
      return;
    }

  if (check_reconnect (fromwdata) < 0 || check_reconnect (towdata) < 0)
    return;

  transfer = g_malloc0 (sizeof (*transfer));
  transfer->fromreq = copy_request (fromwdata->request, 0);
  transfer->toreq = copy_request (towdata->request, 0);
  transfer->transfer_direction = fromwdata == &window2 ? 
                           GFTP_DIRECTION_DOWNLOAD : GFTP_DIRECTION_UPLOAD;
  transfer->fromwdata = fromwdata;
  transfer->towdata = towdata;

  num = 0;
  templist = GTK_CLIST (fromwdata->listbox)->selection;
  filelist = fromwdata->files;
  while (templist != NULL)
    {
      templist = get_next_selection (templist, &filelist, &num);
      tempfle = filelist->data;
      if (strcmp (tempfle->file, "..") != 0)
        {
          newfle = copy_fdata (tempfle);
          transfer->files = g_list_append (transfer->files, newfle);
        }
    }

  if (transfer->files != NULL)
    {
      gftp_swap_socks (transfer->fromreq, fromwdata->request);
      gftp_swap_socks (transfer->toreq, towdata->request);

      if (transfer->fromreq->use_threads || 
          (transfer->toreq && transfer->toreq->use_threads))
        {
          transfer->fromreq->stopable = 1;
          pthread_create (&fromwdata->tid, NULL, do_getdir_thread, transfer);

          timeout_num = gtk_timeout_add (100, progress_timeout, transfer);

          while (transfer->fromreq->stopable)
            {
              GDK_THREADS_LEAVE ();
#if GTK_MAJOR_VERSION == 1
              g_main_iteration (TRUE);
#else
              g_main_context_iteration (NULL, TRUE);
#endif
            }

          gtk_timeout_remove (timeout_num);
          transfer->numfiles = transfer->numdirs = -1; 
          update_directory_download_progress (transfer);

          pthread_join (fromwdata->tid, &ret);
        }
      else
        ret = do_getdir_thread (transfer);

      if (!GFTP_IS_CONNECTED (transfer->fromreq))
        {
          disconnect (fromwdata);
          return;
        } 

      if (!GFTP_IS_CONNECTED (transfer->toreq))
        {
          disconnect (towdata);
          return;
        } 

      gftp_swap_socks (fromwdata->request, transfer->fromreq);
      gftp_swap_socks (towdata->request, transfer->toreq);
    }

  if (transfer->files != NULL)
    {
      add_file_transfer (transfer->fromreq, transfer->toreq, 
                         transfer->fromwdata, transfer->towdata, 
                         transfer->files, 0);
      g_free (transfer);
    }
  else
    free_tdata (transfer);
}

void *
do_getdir_thread (void * data)
{
  gftp_transfer * transfer;
  int success, sj;

  transfer = data;

  if (transfer->fromreq->use_threads || 
      (transfer->toreq && transfer->toreq->use_threads))
    {
      sj = sigsetjmp (jmp_environment, 1);
      use_jmp_environment = 1;
    }
  else
    sj = 0;

  success = 0;
  if (sj == 0)
    success = gftp_get_all_subdirs (transfer, NULL) == 0;
  else
    {
      gftp_disconnect (transfer->fromreq);
      if (transfer->toreq)
        gftp_disconnect (transfer->toreq);
      transfer->fromreq->logging_function (gftp_logging_error,
                                           transfer->fromreq->user_data,
                                           _("Operation canceled\n"));
    }

  if (transfer->fromreq->use_threads || 
      (transfer->toreq && transfer->toreq->use_threads))
    use_jmp_environment = 0;

  transfer->fromreq->stopable = 0;
  return ((void *) success);
}


void * 
gftp_gtk_transfer_files (void *data)
{
  int i, mode, tofd, fromfd;
  gftp_transfer * transfer;
  char buf[8192];
  off_t fromsize, total;
  gftp_file * curfle; 
  ssize_t num_read, ret;

  pthread_detach (pthread_self ());
  transfer = data;
  transfer->curfle = transfer->files;
  gettimeofday (&transfer->starttime, NULL);
  memcpy (&transfer->lasttime, &transfer->starttime, 
          sizeof (transfer->lasttime));

  while (transfer->curfle != NULL)
    {
      num_read = -1;
      g_static_mutex_lock (&transfer->structmutex);
      curfle = transfer->curfle->data;
      transfer->current_file_number++; 
      g_static_mutex_unlock (&transfer->structmutex);
 
      if (curfle->transfer_action == GFTP_TRANS_ACTION_SKIP)
        {
          g_static_mutex_lock (&transfer->structmutex);
          transfer->next_file = 1;
          transfer->curfle = transfer->curfle->next;
          g_static_mutex_unlock (&transfer->structmutex);
          continue;
        }

      fromsize = -1;
      if (gftp_connect (transfer->fromreq) == 0 &&
          gftp_connect (transfer->toreq) == 0)
        {
          if (curfle->isdir)
            {
              if (transfer->toreq->mkdir != NULL)
                {
                  transfer->toreq->mkdir (transfer->toreq, curfle->destfile);
                  if (!GFTP_IS_CONNECTED (transfer->toreq))
                    break;
                }

              g_static_mutex_lock (&transfer->structmutex);
              transfer->next_file = 1;
              transfer->curfle = transfer->curfle->next;
              g_static_mutex_unlock (&transfer->structmutex);
              continue;
            }

          if (curfle->is_fd)
            {
              if (transfer->transfer_direction == GFTP_DIRECTION_DOWNLOAD)
                {
                  tofd = curfle->fd;
                  fromfd = -1;
                }
              else
                {
                  tofd = -1;
                  fromfd = curfle->fd;
                }
            }
          else
            {
              tofd = -1;
              fromfd = -1;
            }

          if (curfle->size == 0)
            {
              curfle->size = gftp_get_file_size (transfer->fromreq, curfle->file);
              transfer->total_bytes += curfle->size;
            }

          if (GFTP_IS_CONNECTED (transfer->fromreq) &&
              GFTP_IS_CONNECTED (transfer->toreq))
            {
              fromsize = gftp_transfer_file (transfer->fromreq, curfle->file, 
                          fromfd,
                          curfle->transfer_action == GFTP_TRANS_ACTION_RESUME ?
                                                    curfle->startsize : 0,
                          transfer->toreq, curfle->destfile, tofd,
                          curfle->transfer_action == GFTP_TRANS_ACTION_RESUME ?
                                                    curfle->startsize : 0);
            }
        }

      if (!GFTP_IS_CONNECTED (transfer->fromreq) || 
          !GFTP_IS_CONNECTED (transfer->toreq))
        {
          transfer->fromreq->logging_function (gftp_logging_misc, 
                         transfer->fromreq->user_data, 
                         _("Error: Remote site disconnected after trying to transfer file\n"));
        }
      else if (fromsize < 0)
        {
          if (curfle->is_fd)
            {
              if (transfer->transfer_direction == GFTP_DIRECTION_DOWNLOAD)
                transfer->toreq->datafd = -1;
              else
                transfer->fromreq->datafd = -1;
            }

          g_static_mutex_lock (&transfer->structmutex);
          curfle->transfer_action = GFTP_TRANS_ACTION_SKIP;
          transfer->next_file = 1;
          transfer->curfle = transfer->curfle->next;
          g_static_mutex_unlock (&transfer->structmutex);
          continue;
        }
      else
        {
          g_static_mutex_lock (&transfer->structmutex);
          transfer->curtrans = 0;
          transfer->curresumed = curfle->transfer_action == GFTP_TRANS_ACTION_RESUME ? curfle->startsize : 0;
          transfer->resumed_bytes += transfer->curresumed;
          g_static_mutex_unlock (&transfer->structmutex);
  
          total = 0;
          i = 0;
          while (!transfer->cancel && 
                 (num_read = gftp_get_next_file_chunk (transfer->fromreq,
                                                       buf, sizeof (buf))) > 0)
            {
              total += num_read;
              gftp_calc_kbs (transfer, num_read);

              if ((ret = gftp_put_next_file_chunk (transfer->toreq, buf, 
                                                   num_read)) < 0)
                {
                  num_read = (int) ret;
                  break;
                }
            }
        }

      if (transfer->cancel)
        {
          if (gftp_abort_transfer (transfer->fromreq) != 0)
            gftp_disconnect (transfer->fromreq);

          if (gftp_abort_transfer (transfer->toreq) != 0)
            gftp_disconnect (transfer->toreq);
        }
      else if (num_read < 0)
        {
          transfer->fromreq->logging_function (gftp_logging_misc, 
                                        transfer->fromreq->user_data, 
                                        _("Could not download %s from %s\n"), 
                                        curfle->file,
                                        transfer->fromreq->hostname);

          if (gftp_get_transfer_status (transfer, num_read) == GFTP_ERETRYABLE)
            continue;

          break;
        }
      else
        {
          if (curfle->is_fd)
            {
              if (transfer->transfer_direction == GFTP_DIRECTION_DOWNLOAD)
                transfer->toreq->datafd = -1;
              else
                transfer->fromreq->datafd = -1;
            }

          if (gftp_end_transfer (transfer->fromreq) != 0)
            {
              if (gftp_get_transfer_status (transfer, -1) == GFTP_ERETRYABLE)
                continue;

              break;
            }
          gftp_end_transfer (transfer->toreq);

          transfer->fromreq->logging_function (gftp_logging_misc, 
                         transfer->fromreq->user_data, 
                         _("Successfully transferred %s at %.2f KB/s\n"),
                         curfle->file, transfer->kbs);
        }

      if (!curfle->is_fd)
        {
          if (curfle->attribs)
            {
              mode = gftp_parse_attribs (curfle->attribs);
              if (mode != 0)
                gftp_chmod (transfer->toreq, curfle->destfile, mode);
            }

          if (curfle->datetime != 0)
            gftp_set_file_time (transfer->toreq, curfle->destfile,
                                curfle->datetime);
        }

      g_static_mutex_lock (&transfer->structmutex);
      transfer->next_file = 1;
      curfle->transfer_done = 1;
      transfer->curfle = transfer->curfle->next;
      g_static_mutex_unlock (&transfer->structmutex);

      if (transfer->cancel && !transfer->skip_file)
        break;
      transfer->cancel = 0;
      transfer->fromreq->cancel = 0;
      transfer->toreq->cancel = 0;
    }
  transfer->done = 1; 
  return (NULL);
}


void
add_file_transfer (gftp_request * fromreq, gftp_request * toreq,
                   gftp_window_data * fromwdata, gftp_window_data * towdata, 
                   GList * files, int copy_req)
{
  int dialog, append_transfers;
  gftp_curtrans_data * transdata;
  GList * templist, *curfle;
  gftp_transfer * tdata;
  gftp_file * tempfle;
  char *pos, *text[2];

  for (templist = files; templist != NULL; templist = templist->next)
    { 
      tempfle = templist->data;
      if (tempfle->startsize > 0)
        break;
    }
  dialog = templist != NULL;

  gftp_lookup_request_option (fromreq, "append_transfers", 
                              &append_transfers);

  if (append_transfers)
    {
      pthread_mutex_lock (&transfer_mutex);
      for (templist = gftp_file_transfers; templist != NULL; templist = templist->next)
        {
          tdata = templist->data;
          g_static_mutex_lock (&tdata->structmutex);
          if (compare_request (tdata->fromreq, fromreq, 0) &&
              compare_request (tdata->toreq, toreq, 0) &&
              tdata->curfle != NULL)
            {
              if (!copy_req)
                {
                  gftp_request_destroy (fromreq, 1);
                  gftp_request_destroy (toreq, 1);
                }
              fromreq = NULL;
              toreq = NULL;

              for (curfle = tdata->curfle; 
                   curfle != NULL && curfle->next != NULL; 
                   curfle = curfle->next);
              if (curfle == NULL)
                {
                  curfle = files;
                  files->prev = NULL;
                }
              else
                {
                  curfle->next = files;
                  files->prev = curfle;
                }

              for (curfle = files; curfle != NULL; curfle = curfle->next)
                {
                  tempfle = curfle->data;
                  if (tempfle->isdir)
                    tdata->numdirs++;
                  else
                    tdata->numfiles++;
                  if ((pos = strrchr (tempfle->file, '/')) == NULL)
                    pos = tempfle->file;
                  else
                    pos++;

                  text[0] = pos;
                  if (tempfle->transfer_action == GFTP_TRANS_ACTION_SKIP)
                    text[1] = _("Skipped");
                  else
                    {
                      tdata->total_bytes += tempfle->size;
                      text[1] = _("Waiting...");
                    }

                  tempfle->user_data = gtk_ctree_insert_node (GTK_CTREE (dlwdw),  
                                             tdata->user_data, NULL, text, 5,
                                             NULL, NULL, NULL, NULL, 
                                             FALSE, FALSE);
                  transdata = g_malloc (sizeof (*transdata));
                  transdata->transfer = tdata;
                  transdata->curfle = curfle;
                  gtk_ctree_node_set_row_data (GTK_CTREE (dlwdw), tempfle->user_data, 
                                               transdata);
                }
              g_static_mutex_unlock (&tdata->structmutex);
              break; 
            }
          g_static_mutex_unlock (&tdata->structmutex);
        }
      pthread_mutex_unlock (&transfer_mutex);
    }
  else
    templist = NULL;
    
  if (templist == NULL)
    {
      tdata = gftp_tdata_new ();
      if (copy_req)
        {
          tdata->fromreq = copy_request (fromreq, 0);
          tdata->toreq = copy_request (toreq, 0); 
        }
      else
        {
          tdata->fromreq = fromreq;
          tdata->toreq = toreq; 
        } 
      tdata->transfer_direction = fromwdata && fromwdata == &window1 ?
                            GFTP_DIRECTION_UPLOAD : GFTP_DIRECTION_DOWNLOAD;
      tdata->fromwdata = fromwdata;
      tdata->towdata = towdata;
      if (!dialog)
        tdata->show = tdata->ready = 1;
      tdata->files = files;
      for (curfle = files; curfle != NULL; curfle = curfle->next)
        {
          tempfle = curfle->data;
          if (tempfle->isdir)
            tdata->numdirs++;
          else
            tdata->numfiles++;
        }

      pthread_mutex_lock (&transfer_mutex);
      gftp_file_transfers = g_list_append (gftp_file_transfers, tdata);
      pthread_mutex_unlock (&transfer_mutex);

      if (dialog)
        gftp_gtk_ask_transfer (tdata);
    }
}


static void
remove_file (char *filename)
{
  if (unlink (filename) == 0)
    ftp_log (gftp_logging_misc, NULL, _("Successfully removed %s\n"),
             filename);
  else
    ftp_log (gftp_logging_error, NULL,
             _("Error: Could not remove file %s: %s\n"), filename,
             g_strerror (errno));
}


static void
free_edit_data (gftp_viewedit_data * ve_proc)
{
  int i;

  if (ve_proc->filename)
    g_free (ve_proc->filename);
  if (ve_proc->remote_filename)
    g_free (ve_proc->remote_filename);
  for (i = 0; ve_proc->argv[i] != NULL; i++)
    g_free (ve_proc->argv[i]);
  g_free (ve_proc->argv);
  g_free (ve_proc);
}


static void
dont_upload (gftp_viewedit_data * ve_proc, gftp_dialog_data * ddata)
{
  remove_file (ve_proc->filename);
  free_edit_data (ve_proc);
}


static void
do_upload (gftp_viewedit_data * ve_proc, gftp_dialog_data * ddata)
{
  gftp_file * tempfle;
  GList * newfile;

  tempfle = g_malloc0 (sizeof (*tempfle));
  tempfle->destfile = ve_proc->remote_filename;
  ve_proc->remote_filename = NULL;
  tempfle->file = ve_proc->filename;
  ve_proc->filename = NULL;
  tempfle->done_rm = 1;
  newfile = g_list_append (NULL, tempfle);
  add_file_transfer (ve_proc->fromwdata->request, ve_proc->towdata->request,
                     ve_proc->fromwdata, ve_proc->towdata, newfile, 1);
  free_edit_data (ve_proc);
}


static void
check_done_process (void)
{
  gftp_viewedit_data * ve_proc;
  GList * curdata, *deldata;
  struct stat st;
  int ret;
  char *str;
  pid_t pid;

  viewedit_process_done = 0;
  while ((pid = waitpid (-1, &ret, WNOHANG)) > 0)
    {
      curdata = viewedit_processes;
      while (curdata != NULL)
        {
	  ve_proc = curdata->data;
          deldata = curdata;
          curdata = curdata->next;
	  if (ve_proc->pid == pid)
	    {
	      viewedit_processes = g_list_remove_link (viewedit_processes, 
                                                       deldata);
	      if (ret != 0)
		ftp_log (gftp_logging_error, NULL,
			 _("Error: Child %d returned %d\n"), pid, ret);
	      else
		ftp_log (gftp_logging_misc, NULL,
			 _("Child %d returned successfully\n"), pid);

	      if (!ve_proc->view && !ve_proc->dontupload)
		{
		  /* We was editing the file. Upload it */
		  if (stat (ve_proc->filename, &st) == -1)
		    ftp_log (gftp_logging_error, NULL,
		         _("Error: Cannot get information about file %s: %s\n"),
			 ve_proc->filename, g_strerror (errno));
		  else if (st.st_mtime == ve_proc->st.st_mtime)
                    {
		      ftp_log (gftp_logging_misc, NULL,
		  	       _("File %s was not changed\n"),
			       ve_proc->filename);
                      remove_file (ve_proc->filename);
                    }
		  else
		    {
		      memcpy (&ve_proc->st, &st, sizeof (ve_proc->st));
		      str = g_strdup_printf (
			_("File %s has changed.\nWould you like to upload it?"),
                        ve_proc->remote_filename);

		      MakeYesNoDialog (_("Edit File"), str, 
                                       do_upload, ve_proc, 
                                       dont_upload, ve_proc);
		      g_free (str);
		      continue;
		    }
		}

              free_edit_data (ve_proc);
	      continue;
	    }
	}
    }
}


static void
on_next_transfer (gftp_transfer * tdata)
{
  int fd, refresh_files;
  gftp_file * tempfle;

  tdata->next_file = 0;
  for (; tdata->updfle != tdata->curfle; tdata->updfle = tdata->updfle->next)
    {
      tempfle = tdata->updfle->data;

      if (tempfle->is_fd)
        fd = tempfle->fd;
      else
        fd = 0;

      if (tempfle->done_view)
        {
          if (tempfle->transfer_action != GFTP_TRANS_ACTION_SKIP)
            view_file (tempfle->destfile, fd, 1, tempfle->done_rm, 1, 0,
                       tempfle->file, NULL);

          if (tempfle->is_fd)
            {
              close (tempfle->fd);
              tempfle->fd = -1;
            }
        }
      else if (tempfle->done_edit)
        {
          if (tempfle->transfer_action != GFTP_TRANS_ACTION_SKIP)
	    view_file (tempfle->destfile, fd, 0, tempfle->done_rm, 1, 0,
                       tempfle->file, NULL);

          if (tempfle->is_fd)
            {
              close (tempfle->fd);
              tempfle->fd = -1;
            }
        }
      else if (tempfle->done_rm)
	tdata->fromreq->rmfile (tdata->fromreq, tempfle->file);
      
      if (tempfle->transfer_action == GFTP_TRANS_ACTION_SKIP)
        gtk_ctree_node_set_text (GTK_CTREE (dlwdw), tempfle->user_data, 1,
  			         _("Skipped"));
      else
        gtk_ctree_node_set_text (GTK_CTREE (dlwdw), tempfle->user_data, 1,
  			         _("Finished"));
    }

  gftp_lookup_request_option (tdata->fromreq, "refresh_files", &refresh_files);

  if (refresh_files && tdata->curfle && tdata->curfle->next &&
      compare_request (tdata->toreq, 
                       ((gftp_window_data *) tdata->towdata)->request, 1))
    refresh (tdata->towdata);
}


static void
get_trans_password (gftp_request * request, gftp_dialog_data * ddata)
{
  gftp_set_password (request, gtk_entry_get_text (GTK_ENTRY (ddata->edit)));
  request->stopable = 0;
}


static void
cancel_get_trans_password (gftp_transfer * tdata, gftp_dialog_data * ddata)
{
  if (tdata->fromreq->stopable == 0)
    return;

  g_static_mutex_lock (&tdata->structmutex);
  if (tdata->started)
    {
      tdata->cancel = 1;
      tdata->fromreq->cancel = 1;
      tdata->toreq->cancel = 1;
    }
  else
    tdata->done = 1;

  tdata->fromreq->stopable = 0;
  tdata->toreq->stopable = 0;
  g_static_mutex_unlock (&tdata->structmutex);

  ftp_log (gftp_logging_misc, NULL, _("Stopping the transfer of %s\n"),
	   ((gftp_file *) tdata->curfle->data)->file);
}


static void
show_transfer (gftp_transfer * tdata)
{
  GdkPixmap * closedir_pixmap, * opendir_pixmap;
  GdkBitmap * closedir_bitmap, * opendir_bitmap;
  gftp_curtrans_data * transdata;
  gftp_file * tempfle;
  char *pos, *text[2];
  GList * templist;

  gftp_get_pixmap (dlwdw, "open_dir.xpm", &opendir_pixmap, &opendir_bitmap);
  gftp_get_pixmap (dlwdw, "dir.xpm", &closedir_pixmap, &closedir_bitmap);

  text[0] = tdata->fromreq->hostname;
  text[1] = _("Waiting...");
  tdata->user_data = gtk_ctree_insert_node (GTK_CTREE (dlwdw), NULL, NULL, 
                                       text, 5,
                                       closedir_pixmap, closedir_bitmap, 
                                       opendir_pixmap, opendir_bitmap, 
                                       FALSE, 
                                       tdata->numdirs + tdata->numfiles < 50);
  transdata = g_malloc (sizeof (*transdata));
  transdata->transfer = tdata;
  transdata->curfle = NULL;
  gtk_ctree_node_set_row_data (GTK_CTREE (dlwdw), tdata->user_data, transdata);
  tdata->show = 0;
  tdata->curfle = tdata->updfle = tdata->files;

  tdata->total_bytes = 0;
  for (templist = tdata->files; templist != NULL; templist = templist->next)
    {
      tempfle = templist->data;
      if ((pos = strrchr (tempfle->file, '/')) == NULL)
	pos = tempfle->file;
      else
	pos++;
      text[0] = pos;
      if (tempfle->transfer_action == GFTP_TRANS_ACTION_SKIP)
        text[1] = _("Skipped");
      else
        {
          tdata->total_bytes += tempfle->size;
          text[1] = _("Waiting...");
        }

      tempfle->user_data = gtk_ctree_insert_node (GTK_CTREE (dlwdw), 
                                             tdata->user_data, 
                                             NULL, text, 5, NULL, NULL, NULL, 
                                             NULL, FALSE, FALSE);
      transdata = g_malloc (sizeof (*transdata));
      transdata->transfer = tdata;
      transdata->curfle = templist;
      gtk_ctree_node_set_row_data (GTK_CTREE (dlwdw), tempfle->user_data, 
                                   transdata);
    }

  if (!tdata->toreq->stopable && tdata->toreq->need_userpass &&
      (tdata->toreq->password == NULL || *tdata->toreq->password == '\0'))
    {
      tdata->toreq->stopable = 1;
      MakeEditDialog (_("Enter Password"),
		      _("Please enter your password for this site"), NULL, 0,
		      NULL, gftp_dialog_button_connect, 
                      get_trans_password, tdata->toreq,
		      cancel_get_trans_password, tdata);
    }

  if (!tdata->fromreq->stopable && tdata->fromreq->need_userpass &&
      (tdata->fromreq->password == NULL || *tdata->fromreq->password == '\0'))
    {
      tdata->fromreq->stopable = 1;
      MakeEditDialog (_("Enter Password"),
		      _("Please enter your password for this site"), NULL, 0,
		      NULL, gftp_dialog_button_connect, 
                      get_trans_password, tdata->fromreq,
		      cancel_get_trans_password, tdata);
    }
}


static void
transfer_done (GList * node)
{
  gftp_curtrans_data * transdata;
  gftp_request * fromreq;
  gftp_transfer * tdata;
  gftp_file * tempfle;
  GList * templist;

  tdata = node->data;
  if (tdata->started)
    {
      fromreq = tdata->fromwdata != NULL ? ((gftp_window_data *) tdata->fromwdata)->request : NULL;
      if (!tdata->fromreq->stopable && tdata->fromwdata &&
          ((fromreq->datafd < 0 && fromreq->cached) || fromreq->always_connected) &&
          (tdata->fromreq->datafd > 0 || tdata->fromreq->always_connected) &&
          compare_request (tdata->fromreq, fromreq, 0))
	{
          gftp_swap_socks (((gftp_window_data *) tdata->towdata)->request, 
                           tdata->toreq);
          gftp_swap_socks (((gftp_window_data *) tdata->fromwdata)->request, 
                           tdata->fromreq);
	}
      else
        {
	  gftp_disconnect (tdata->fromreq);
          gftp_disconnect (tdata->toreq);
        }

      if (tdata->towdata != NULL && compare_request (tdata->toreq, 
                           ((gftp_window_data *) tdata->towdata)->request, 1)) 
	refresh (tdata->towdata);

      num_transfers_in_progress--;
    }

  if (!tdata->show && tdata->started)
    {
      transdata = gtk_ctree_node_get_row_data (GTK_CTREE (dlwdw), 
                                               tdata->user_data);
      if (transdata != NULL)
        g_free (transdata);

      for (templist = tdata->files; templist != NULL; templist = templist->next)
        {
          tempfle = templist->data;
          transdata = gtk_ctree_node_get_row_data (GTK_CTREE (dlwdw), 
                                                   tempfle->user_data);
          if (transdata != NULL)
            g_free (transdata);
        }
          
      gtk_ctree_remove_node (GTK_CTREE (dlwdw), tdata->user_data);
    }

  pthread_mutex_lock (&transfer_mutex);
  gftp_file_transfers = g_list_remove_link (gftp_file_transfers, node);
  pthread_mutex_unlock (&transfer_mutex);

  free_tdata (tdata);
}


static void
create_transfer (gftp_transfer * tdata)
{
  pthread_t tid;

  if (!tdata->fromreq->stopable)
    {
      if (tdata->fromwdata && 
          (((gftp_window_data *) tdata->fromwdata)->request->datafd > 0 ||
           ((gftp_window_data *) tdata->fromwdata)->request->always_connected) && 
          !((gftp_window_data *) tdata->fromwdata)->request->stopable &&
          compare_request (tdata->fromreq, ((gftp_window_data *) tdata->fromwdata)->request, 0))
	{
          gftp_swap_socks (tdata->toreq, 
                           ((gftp_window_data *) tdata->towdata)->request);
          gftp_swap_socks (tdata->fromreq, 
                           ((gftp_window_data *) tdata->fromwdata)->request);
	  update_window_info ();
	}
      num_transfers_in_progress++;
      tdata->started = 1;
      tdata->stalled = 1;
      gtk_ctree_node_set_text (GTK_CTREE (dlwdw), tdata->user_data, 1,
			       _("Connecting..."));
      pthread_create (&tid, NULL, gftp_gtk_transfer_files, tdata);
    }
}


static void
update_file_status (gftp_transfer * tdata)
{
  char totstr[100], dlstr[100], gotstr[50], ofstr[50];
  int hours, mins, secs, pcent, st;
  double remaining;
  gftp_file * tempfle;
  struct timeval tv;

  g_static_mutex_lock (&tdata->statmutex);
  tempfle = tdata->curfle->data;

  gettimeofday (&tv, NULL);
  if ((remaining = (double) (tv.tv_sec - tdata->starttime.tv_sec) + ((double) (tv.tv_usec - tdata->starttime.tv_usec) / 1000000.0)) == 0)
    remaining = 1.0;

  remaining = ((double) (tdata->total_bytes - tdata->trans_bytes - tdata->resumed_bytes)) / 1024.0 / tdata->kbs;
  hours = (off_t) remaining / 3600;
  remaining -= hours * 3600;
  mins = (off_t) remaining / 60;
  remaining -= mins * 60;
  secs = (off_t) remaining;

  if (hours < 0 || mins < 0 || secs < 0)
    {
      g_static_mutex_unlock (&tdata->statmutex);
      return;
    }

  pcent = (int) ((double) (tdata->trans_bytes + tdata->resumed_bytes) / (double) tdata->total_bytes * 100.0);
  if (pcent < 0 || pcent > 100)
    pcent = 0;

  g_snprintf (totstr, sizeof (totstr),
	_("%d%% complete, %02d:%02d:%02d est. time remaining. (File %ld of %ld)"),
	pcent, hours, mins, secs, tdata->current_file_number,
	tdata->numdirs + tdata->numfiles);

  *dlstr = '\0';
  if (!tdata->stalled)
    {
      insert_commas (tdata->curtrans + tdata->curresumed, gotstr, sizeof (gotstr));
      insert_commas (tempfle->size, ofstr, sizeof (ofstr));
      st = 1;
      if (tv.tv_sec - tdata->lasttime.tv_sec <= 5)
        {
          if (tdata->curfle->next != NULL)
            {
              remaining = ((double) (tempfle->size - tdata->curtrans - tdata->curresumed)) / 1024.0 / tdata->kbs;
              hours = (off_t) remaining / 3600;
              remaining -= hours * 3600;
              mins = (off_t) remaining / 60;
              remaining -= mins * 60;
              secs = (off_t) remaining;
            }

          if (!(hours < 0 || mins < 0 || secs < 0))
            {
              g_snprintf (dlstr, sizeof (dlstr),
                          _("Recv %s of %s at %.2fKB/s, %02d:%02d:%02d est. time remaining"), gotstr, ofstr, tdata->kbs, hours, mins, secs);
              st = 0;
            }
        }

      if (st)
        {
          tdata->stalled = 1;
          g_snprintf (dlstr, sizeof (dlstr),
	  	  _("Recv %s of %s, transfer stalled, unknown time remaining"),
		  gotstr, ofstr);
        }
    }

  g_static_mutex_unlock (&tdata->statmutex);

  gtk_ctree_node_set_text (GTK_CTREE (dlwdw), tdata->user_data, 1, totstr);

  if (*dlstr != '\0')
    gtk_ctree_node_set_text (GTK_CTREE (dlwdw), tempfle->user_data, 1, dlstr);
}

static void
update_window_transfer_bytes (gftp_window_data * wdata)
{
  char *tempstr, *temp1str;

  if (wdata->request->gotbytes == -1)
    {
      update_window_info ();
      wdata->request->gotbytes = 0;
    }
  else
    {
      tempstr = insert_commas (wdata->request->gotbytes, NULL, 0);
      temp1str = g_strdup_printf (_("Retrieving file names...%s bytes"), 
                                  tempstr);
      gtk_label_set (GTK_LABEL (wdata->hoststxt), temp1str);
      g_free (tempstr);
      g_free (temp1str);
    }
}


gint
update_downloads (gpointer data)
{
  int do_one_transfer_at_a_time, start_file_transfers;
  GList * templist, * next;
  gftp_transfer * tdata;

  if (gftp_file_transfer_logs != NULL)
    display_cached_logs ();

  if (window1.request->gotbytes != 0)
    update_window_transfer_bytes (&window1);
  if (window2.request->gotbytes != 0)
    update_window_transfer_bytes (&window2);

  if (viewedit_process_done)
    check_done_process ();

  for (templist = gftp_file_transfers; templist != NULL;)
    {
      tdata = templist->data;
      if (tdata->ready)
        {
          g_static_mutex_lock (&tdata->structmutex);

	  if (tdata->next_file)
	    on_next_transfer (tdata);
     	  else if (tdata->show) 
	    show_transfer (tdata);
	  else if (tdata->done)
	    {
	      next = templist->next;
              g_static_mutex_unlock (&tdata->structmutex);
	      transfer_done (templist);
	      templist = next;
	      continue;
	    }

	  if (tdata->curfle != NULL)
	    {
              gftp_lookup_global_option ("one_transfer", 
                                         &do_one_transfer_at_a_time);
              start_file_transfers = 1; /* FIXME */

	      if (!tdata->started && start_file_transfers &&
                 (num_transfers_in_progress == 0 || !do_one_transfer_at_a_time))
                create_transfer (tdata);

	      if (tdata->started)
                update_file_status (tdata);
	    }
          g_static_mutex_unlock (&tdata->structmutex);
        }
      templist = templist->next;
    }

  gtk_timeout_add (500, update_downloads, NULL);
  return (0);
}


void
start_transfer (gpointer data)
{
  gftp_curtrans_data * transdata;
  GtkCTreeNode * node;

  if (GTK_CLIST (dlwdw)->selection == NULL)
    {
      ftp_log (gftp_logging_misc, NULL,
	       _("There are no file transfers selected\n"));
      return;
    }
  node = GTK_CLIST (dlwdw)->selection->data;
  transdata = gtk_ctree_node_get_row_data (GTK_CTREE (dlwdw), node);

  g_static_mutex_lock (&transdata->transfer->structmutex);
  if (!transdata->transfer->started)
    create_transfer (transdata->transfer);
  g_static_mutex_unlock (&transdata->transfer->structmutex);
}


void
stop_transfer (gpointer data)
{
  gftp_curtrans_data * transdata;
  GtkCTreeNode * node;

  if (GTK_CLIST (dlwdw)->selection == NULL)
    {
      ftp_log (gftp_logging_misc, NULL,
	      _("There are no file transfers selected\n"));
      return;
    }
  node = GTK_CLIST (dlwdw)->selection->data;
  transdata = gtk_ctree_node_get_row_data (GTK_CTREE (dlwdw), node);

  g_static_mutex_lock (&transdata->transfer->structmutex);
  if (transdata->transfer->started)
    {
      transdata->transfer->cancel = 1;
      transdata->transfer->fromreq->cancel = 1;
      transdata->transfer->toreq->cancel = 1;
      transdata->transfer->skip_file = 0;
    }
  else
    transdata->transfer->done = 1;
  g_static_mutex_unlock (&transdata->transfer->structmutex);

  ftp_log (gftp_logging_misc, NULL, _("Stopping the transfer on host %s\n"),
	   transdata->transfer->fromreq->hostname);
}


void
skip_transfer (gpointer data)
{
  gftp_curtrans_data * transdata;
  GtkCTreeNode * node;
  gftp_file * curfle;
  char *file;

  if (GTK_CLIST (dlwdw)->selection == NULL)
    {
      ftp_log (gftp_logging_misc, NULL,
	      _("There are no file transfers selected\n"));
      return;
    }
  node = GTK_CLIST (dlwdw)->selection->data;
  transdata = gtk_ctree_node_get_row_data (GTK_CTREE (dlwdw), node);

  g_static_mutex_lock (&transdata->transfer->structmutex);
  if (transdata->transfer->curfle != NULL)
    {
      curfle = transdata->transfer->curfle->data;
      if (transdata->transfer->started)
        {
          transdata->transfer->cancel = 1;
          transdata->transfer->fromreq->cancel = 1;
          transdata->transfer->toreq->cancel = 1;
          transdata->transfer->skip_file = 1;
        }

      curfle->transfer_action = GFTP_TRANS_ACTION_SKIP;
      file = curfle->file;
    }
  else
    file = NULL;
  g_static_mutex_unlock (&transdata->transfer->structmutex);

  ftp_log (gftp_logging_misc, NULL, _("Skipping file %s on host %s\n"), 
           file, transdata->transfer->fromreq->hostname);
}


void
remove_file_transfer (gpointer data)
{
  gftp_curtrans_data * transdata;
  GtkCTreeNode * node;
  gftp_file * curfle;

  if (GTK_CLIST (dlwdw)->selection == NULL)
    {
      ftp_log (gftp_logging_misc, NULL,
              _("There are no file transfers selected\n"));
      return;
    }

  node = GTK_CLIST (dlwdw)->selection->data;
  transdata = gtk_ctree_node_get_row_data (GTK_CTREE (dlwdw), node);


  if (transdata->curfle == NULL || transdata->curfle->data == NULL)
    return;

  curfle = transdata->curfle->data;

  if (curfle->transfer_action & GFTP_TRANS_ACTION_SKIP)
    return;

  g_static_mutex_lock (&transdata->transfer->structmutex);

  curfle->transfer_action = GFTP_TRANS_ACTION_SKIP;

  if (transdata->transfer->started &&
      transdata->curfle == transdata->transfer->curfle)
    {
      transdata->transfer->cancel = 1;
      transdata->transfer->fromreq->cancel = 1;
      transdata->transfer->toreq->cancel = 1;
      transdata->transfer->skip_file = 1;
    }
  else if (transdata->curfle != transdata->transfer->curfle &&
           !curfle->transfer_done)
    {
      gtk_ctree_node_set_text (GTK_CTREE (dlwdw), curfle->user_data, 1,
                               _("Skipped"));
      transdata->transfer->total_bytes -= curfle->size;
    }

  g_static_mutex_unlock (&transdata->transfer->structmutex);

  ftp_log (gftp_logging_misc, NULL, _("Skipping file %s on host %s\n"),
           curfle->file, transdata->transfer->fromreq->hostname);
}


void
move_transfer_up (gpointer data)
{
  GList * firstentry, * secentry, * lastentry;
  gftp_curtrans_data * transdata;
  GtkCTreeNode * node;

  if (GTK_CLIST (dlwdw)->selection == NULL)
    {
      ftp_log (gftp_logging_misc, NULL,
	      _("There are no file transfers selected\n"));
      return;
    }
  node = GTK_CLIST (dlwdw)->selection->data;
  transdata = gtk_ctree_node_get_row_data (GTK_CTREE (dlwdw), node);

  if (transdata->curfle == NULL)
    return;

  g_static_mutex_lock (&transdata->transfer->structmutex);
  if (transdata->curfle->prev != NULL && (!transdata->transfer->started ||
      (transdata->transfer->curfle != transdata->curfle && 
       transdata->transfer->curfle != transdata->curfle->prev)))
    {
      if (transdata->curfle->prev->prev == NULL)
        {
          firstentry = transdata->curfle->prev;
          lastentry = transdata->curfle->next;
          transdata->transfer->files = transdata->curfle;
          transdata->curfle->next = firstentry;
          transdata->transfer->files->prev = NULL;
          firstentry->prev = transdata->curfle;
          firstentry->next = lastentry;
          if (lastentry != NULL)
            lastentry->prev = firstentry;
        }
      else
        {
          firstentry = transdata->curfle->prev->prev;
          secentry = transdata->curfle->prev;
          lastentry = transdata->curfle->next;
          firstentry->next = transdata->curfle;
          transdata->curfle->prev = firstentry;
          transdata->curfle->next = secentry;
          secentry->prev = transdata->curfle;
          secentry->next = lastentry;
          if (lastentry != NULL)
            lastentry->prev = secentry;
        }

      gtk_ctree_move (GTK_CTREE (dlwdw), 
                      ((gftp_file *) transdata->curfle->data)->user_data,
                      transdata->transfer->user_data, 
                      transdata->curfle->next != NULL ?
                          ((gftp_file *) transdata->curfle->next->data)->user_data: NULL);
    }
  g_static_mutex_unlock (&transdata->transfer->structmutex);
}

void
move_transfer_down (gpointer data)
{
  GList * firstentry, * secentry, * lastentry;
  gftp_curtrans_data * transdata;
  GtkCTreeNode * node;

  if (GTK_CLIST (dlwdw)->selection == NULL)
    {
      ftp_log (gftp_logging_misc, NULL,
	      _("There are no file transfers selected\n"));
      return;
    }
  node = GTK_CLIST (dlwdw)->selection->data;
  transdata = gtk_ctree_node_get_row_data (GTK_CTREE (dlwdw), node);

  if (transdata->curfle == NULL)
    return;

  g_static_mutex_lock (&transdata->transfer->structmutex);
  if (transdata->curfle->next != NULL && (!transdata->transfer->started ||
      (transdata->transfer->curfle != transdata->curfle && 
       transdata->transfer->curfle != transdata->curfle->next)))
    {
      if (transdata->curfle->prev == NULL)
        {
          firstentry = transdata->curfle->next;
          lastentry = transdata->curfle->next->next;
          transdata->transfer->files = firstentry;
          transdata->transfer->files->prev = NULL;
          transdata->transfer->files->next = transdata->curfle;
          transdata->curfle->prev = transdata->transfer->files;
          transdata->curfle->next = lastentry;
          if (lastentry != NULL)
            lastentry->prev = transdata->curfle;
        }
      else
        {
          firstentry = transdata->curfle->prev;
          secentry = transdata->curfle->next;
          lastentry = transdata->curfle->next->next;
          firstentry->next = secentry;
          secentry->prev = firstentry;
          secentry->next = transdata->curfle;
          transdata->curfle->prev = secentry;
          transdata->curfle->next = lastentry;
          if (lastentry != NULL)
            lastentry->prev = transdata->curfle;
        }

      gtk_ctree_move (GTK_CTREE (dlwdw), 
                      ((gftp_file *) transdata->curfle->data)->user_data,
                      transdata->transfer->user_data, 
                      transdata->curfle->next != NULL ?
                          ((gftp_file *) transdata->curfle->next->data)->user_data: NULL);
    }
  g_static_mutex_unlock (&transdata->transfer->structmutex);
}


static void
trans_selectall (GtkWidget * widget, gpointer data)
{
  gftp_transfer * tdata;
  tdata = data;

  gtk_clist_select_all (GTK_CLIST (tdata->clist));
}


static void
trans_unselectall (GtkWidget * widget, gpointer data)
{
  gftp_transfer * tdata;
  tdata = data;

  gtk_clist_unselect_all (GTK_CLIST (tdata->clist));
}


static void
overwrite (GtkWidget * widget, gpointer data)
{
  GList * templist, * filelist;
  gftp_transfer * tdata;
  gftp_file * tempfle;
  int curpos;

  tdata = data;
  curpos = 0;
  filelist = tdata->files;
  templist = GTK_CLIST (tdata->clist)->selection;
  while (templist != NULL)
    {
      templist = get_next_selection (templist, &filelist, &curpos);
      tempfle = filelist->data;
      tempfle->transfer_action = GFTP_TRANS_ACTION_OVERWRITE;
      gtk_clist_set_text (GTK_CLIST (tdata->clist), curpos, 3, _("Overwrite"));
    }
}


static void
resume (GtkWidget * widget, gpointer data)
{
  GList * templist, * filelist;
  gftp_transfer * tdata;
  gftp_file * tempfle;
  int curpos;

  tdata = data;
  curpos = 0;
  filelist = tdata->files;
  templist = GTK_CLIST (tdata->clist)->selection;
  while (templist != NULL)
    {
      templist = get_next_selection (templist, &filelist, &curpos);
      tempfle = filelist->data;
      tempfle->transfer_action = GFTP_TRANS_ACTION_RESUME;
      gtk_clist_set_text (GTK_CLIST (tdata->clist), curpos, 3, _("Resume"));
    }
}


static void
skip (GtkWidget * widget, gpointer data)
{
  GList * templist, * filelist;
  gftp_transfer * tdata;
  gftp_file * tempfle;
  int curpos;

  tdata = data;
  curpos = 0;
  filelist = tdata->files;
  templist = GTK_CLIST (tdata->clist)->selection;
  while (templist != NULL)
    {
      templist = get_next_selection (templist, &filelist, &curpos);
      tempfle = filelist->data;
      tempfle->transfer_action = GFTP_TRANS_ACTION_SKIP;
      gtk_clist_set_text (GTK_CLIST (tdata->clist), curpos, 3, _("Skip"));
    }
}


static void
ok (GtkWidget * widget, gpointer data)
{
  gftp_transfer * tdata;
  gftp_file * tempfle;
  GList * templist;

  tdata = data;
  g_static_mutex_lock (&tdata->structmutex);
  for (templist = tdata->files; templist != NULL; templist = templist->next)
    {
      tempfle = templist->data;
      if (tempfle->transfer_action != GFTP_TRANS_ACTION_SKIP)
        break;
    }

  if (templist == NULL)
    {
      tdata->show = 0; 
      tdata->ready = tdata->done = 1;
    }
  else
    tdata->show = tdata->ready = 1;
  g_static_mutex_unlock (&tdata->structmutex);
}


static void
cancel (GtkWidget * widget, gpointer data)
{
  gftp_transfer * tdata;

  tdata = data;
  g_static_mutex_lock (&tdata->structmutex);
  tdata->show = 0;
  tdata->done = tdata->ready = 1;
  g_static_mutex_unlock (&tdata->structmutex);
}


#if GTK_MAJOR_VERSION > 1
static void
transfer_action (GtkWidget * widget, gint response, gpointer user_data)
{
  switch (response)
    {
      case GTK_RESPONSE_OK:
        ok (widget, user_data);
        gtk_widget_destroy (widget);
        break;
      case GTK_RESPONSE_CANCEL:
        cancel (widget, user_data);
        /* no break */
      default:
        gtk_widget_destroy (widget);
    }
}   
#endif


void
gftp_gtk_ask_transfer (gftp_transfer * tdata)
{
  char *dltitles[4], *add_data[4] = { NULL, NULL, NULL, NULL },
       tempstr[50], temp1str[50], *pos, *title;
  GtkWidget * tempwid, * scroll, * hbox;
  int i, overwrite_by_default;
  gftp_file * tempfle;
  GList * templist;
  size_t len;

  dltitles[0] = _("Filename");
  dltitles[1] = _("Local Size");
  dltitles[2] = _("Remote Size");
  dltitles[3] = _("Action");
  title = tdata->transfer_direction == GFTP_DIRECTION_DOWNLOAD ?  
                               _("Download Files") : _("Upload Files");

#if GTK_MAJOR_VERSION == 1
  dialog = gtk_dialog_new ();
  gtk_grab_add (dialog);
  gtk_window_set_title (GTK_WINDOW (dialog), title);
  gtk_container_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->action_area), 5);
  gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->action_area), 35);
  gtk_box_set_homogeneous (GTK_BOX (GTK_DIALOG (dialog)->action_area), TRUE);

  gtk_signal_connect_object (GTK_OBJECT (dialog), "delete_event",
                             GTK_SIGNAL_FUNC (gtk_widget_destroy),
                             GTK_OBJECT (dialog));
#else
  dialog = gtk_dialog_new_with_buttons (title, NULL, 0, 
                                        GTK_STOCK_OK,
                                        GTK_RESPONSE_OK,
                                        GTK_STOCK_CANCEL,
                                        GTK_RESPONSE_CANCEL,
                                        NULL);
#endif
  gtk_window_set_wmclass (GTK_WINDOW(dialog), "transfer", "gFTP");
  gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
  gtk_container_border_width (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), 10);
  gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 5);

  tempwid = gtk_label_new (_("The following file(s) exist on both the local and remote computer\nPlease select what you would like to do"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), tempwid, FALSE,
		      FALSE, 0);
  gtk_widget_show (tempwid);

  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_widget_set_size_request (scroll, 450, 200);

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  tdata->clist = gtk_clist_new_with_titles (4, dltitles);
  gtk_container_add (GTK_CONTAINER (scroll), tdata->clist);

#if GTK_MAJOR_VERSION == 1
  gtk_clist_set_selection_mode (GTK_CLIST (tdata->clist),
				GTK_SELECTION_EXTENDED);
#else
  gtk_clist_set_selection_mode (GTK_CLIST (tdata->clist),
				GTK_SELECTION_MULTIPLE);
#endif
  gtk_clist_set_column_width (GTK_CLIST (tdata->clist), 0, 100);
  gtk_clist_set_column_justification (GTK_CLIST (tdata->clist), 1,
				      GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_width (GTK_CLIST (tdata->clist), 1, 85);
  gtk_clist_set_column_justification (GTK_CLIST (tdata->clist), 2,
				      GTK_JUSTIFY_RIGHT);
  gtk_clist_set_column_width (GTK_CLIST (tdata->clist), 2, 85);
  gtk_clist_set_column_width (GTK_CLIST (tdata->clist), 3, 85);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), scroll, TRUE, TRUE,
		      0);
  gtk_widget_show (tdata->clist);
  gtk_widget_show (scroll);

  gftp_lookup_request_option (tdata->fromreq, "overwrite_by_default",
                              &overwrite_by_default);

  for (templist = tdata->files; templist != NULL; 
       templist = templist->next)
    {
      tempfle = templist->data;
      if (tempfle->startsize == 0 || tempfle->isdir)
        {
           tempfle->shown = 0;
           continue;
        }
      tempfle->shown = 1;

      pos = tempfle->destfile;
      len = strlen (tdata->toreq->directory);
      if (strncmp (pos, tdata->toreq->directory, len) == 0)
        pos = tempfle->destfile + len + 1;
      add_data[0] = pos;

      if (overwrite_by_default)
        add_data[3] = _("Overwrite");
      else
        {
          if (tempfle->startsize >= tempfle->size)
            {
              add_data[3] = _("Skip");
              tempfle->transfer_action = GFTP_TRANS_ACTION_SKIP;
            }
          else
            {
              add_data[3] = _("Resume");
              tempfle->transfer_action = GFTP_TRANS_ACTION_RESUME;
            }
        }

      if (tdata->transfer_direction == GFTP_DIRECTION_DOWNLOAD)
        {
          add_data[2] = insert_commas (tempfle->size, tempstr,
                                       sizeof (tempstr));
          add_data[1] = insert_commas (tempfle->startsize, temp1str,
                                       sizeof (temp1str));
        }
      else
        {
          add_data[1] = insert_commas (tempfle->size, tempstr,
                                       sizeof (tempstr));
          add_data[2] = insert_commas (tempfle->startsize, temp1str,
                                       sizeof (temp1str));
        }
      i = gtk_clist_append (GTK_CLIST (tdata->clist), add_data);
      gtk_clist_set_row_data (GTK_CLIST (tdata->clist), i, tempfle);
    }

  gtk_clist_select_all (GTK_CLIST (tdata->clist));

  hbox = gtk_hbox_new (TRUE, 20);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox, TRUE, TRUE, 0);
  gtk_widget_show (hbox);

  tempwid = gtk_button_new_with_label (_("Overwrite"));
  gtk_box_pack_start (GTK_BOX (hbox), tempwid, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (tempwid), "clicked",
		      GTK_SIGNAL_FUNC (overwrite), (gpointer) tdata);
  gtk_widget_show (tempwid);

  tempwid = gtk_button_new_with_label (_("Resume"));
  gtk_box_pack_start (GTK_BOX (hbox), tempwid, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (tempwid), "clicked",
		      GTK_SIGNAL_FUNC (resume), (gpointer) tdata);
  gtk_widget_show (tempwid);

  tempwid = gtk_button_new_with_label (_("Skip File"));
  gtk_box_pack_start (GTK_BOX (hbox), tempwid, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", GTK_SIGNAL_FUNC (skip),
		      (gpointer) tdata);
  gtk_widget_show (tempwid);

  hbox = gtk_hbox_new (TRUE, 20);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), hbox, TRUE, TRUE, 0);
  gtk_widget_show (hbox);

  tempwid = gtk_button_new_with_label (_("Select All"));
  gtk_box_pack_start (GTK_BOX (hbox), tempwid, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (tempwid), "clicked",
		      GTK_SIGNAL_FUNC (trans_selectall), (gpointer) tdata);
  gtk_widget_show (tempwid);

  tempwid = gtk_button_new_with_label (_("Deselect All"));
  gtk_box_pack_start (GTK_BOX (hbox), tempwid, TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (tempwid), "clicked",
		      GTK_SIGNAL_FUNC (trans_unselectall), (gpointer) tdata);
  gtk_widget_show (tempwid);

#if GTK_MAJOR_VERSION == 1
  tempwid = gtk_button_new_with_label (_("OK"));
  GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), tempwid,
		      TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (tempwid), "clicked", GTK_SIGNAL_FUNC (ok),
		      (gpointer) tdata);
  gtk_signal_connect_object (GTK_OBJECT (tempwid), "clicked",
			     GTK_SIGNAL_FUNC (gtk_widget_destroy),
			     GTK_OBJECT (dialog));
  gtk_widget_grab_default (tempwid);
  gtk_widget_show (tempwid);

  tempwid = gtk_button_new_with_label (_("  Cancel  "));
  GTK_WIDGET_SET_FLAGS (tempwid, GTK_CAN_DEFAULT);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), tempwid,
		      TRUE, TRUE, 0);
  gtk_signal_connect (GTK_OBJECT (tempwid), "clicked",
		      GTK_SIGNAL_FUNC (cancel), (gpointer) tdata);
  gtk_signal_connect_object (GTK_OBJECT (tempwid), "clicked",
			     GTK_SIGNAL_FUNC (gtk_widget_destroy),
			     GTK_OBJECT (dialog));
  gtk_widget_show (tempwid);
#else
  g_signal_connect (GTK_OBJECT (dialog), "response",
                    G_CALLBACK (transfer_action), (gpointer) tdata);
#endif

  gtk_widget_show (dialog);
  dialog = NULL;
}

