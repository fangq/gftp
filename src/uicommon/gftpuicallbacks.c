/*****************************************************************************/
/*  gftpui.c - UI related functions for gFTP. All of these functions must be */
/*             reentrant.                                                    */
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

#include "gftpui.h"
static const char cvsid[] = "$Id$";

int
gftpui_common_run_mkdir (gftpui_callback_data * cdata)
{
  return (gftp_make_directory (cdata->request, cdata->input_string));
}


int
gftpui_common_run_rename (gftpui_callback_data * cdata)
{
  return (gftp_rename_file (cdata->request, cdata->source_string,
                            cdata->input_string));
}


int
gftpui_common_run_site (gftpui_callback_data * cdata)
{
  return (gftp_site_cmd (cdata->request, 1, cdata->input_string));
}


int
gftpui_common_run_chdir (gftpui_callback_data * cdata)
{
  return (gftp_set_directory (cdata->request, cdata->input_string));
}


int
gftpui_common_run_chmod (gftpui_callback_data * cdata)
{
  return (gftp_chmod (cdata->request, cdata->source_string,
                      strtol (cdata->input_string, NULL, 10)));
}


int
gftpui_common_run_ls (gftpui_callback_data * cdata)
{
  int got, matched_filespec, have_dotdot, ret;
  char *sortcol_var, *sortasds_var;
  intptr_t sortcol, sortasds;
  gftp_file * fle;

  ret = gftp_list_files (cdata->request);
  if (ret < 0)
    return (ret);

  have_dotdot = 0;
  cdata->request->gotbytes = 0;
  cdata->files = NULL;
  fle = g_malloc0 (sizeof (*fle));
  while ((got = gftp_get_next_file (cdata->request, NULL, fle)) > 0 ||
         got == GFTP_ERETRYABLE)
    {
      if (cdata->source_string == NULL)
        matched_filespec = 1;
      else
        matched_filespec = gftp_match_filespec (fle->file,
                                                cdata->source_string);

      if (got < 0 || strcmp (fle->file, ".") == 0 || !matched_filespec)
        {
          gftp_file_destroy (fle);
          continue;
        }
      else if (strcmp (fle->file, "..") == 0)
        have_dotdot = 1;

      cdata->request->gotbytes += got;
      cdata->files = g_list_prepend (cdata->files, fle);
      fle = g_malloc0 (sizeof (*fle));
    }
  g_free (fle);

  gftp_end_transfer (cdata->request);
  cdata->request->gotbytes = -1;

  if (!have_dotdot)
    {
      fle = g_malloc0 (sizeof (*fle));
      fle->file = g_strdup ("..");
      fle->user = g_malloc0 (1);
      fle->group = g_malloc0 (1);
      fle->attribs = g_malloc0 (1);
      *fle->attribs = '\0';
      fle->isdir = 1;
      cdata->files = g_list_prepend (cdata->files, fle);
    }

  if (cdata->files != NULL)
    {
      if (cdata->request->protonum == GFTP_LOCAL_NUM)
        {
          sortasds_var = "local_sortasds";
          sortcol_var = "local_sortcol";
        }
      else
        {
          sortasds_var = "remote_sortasds";
          sortcol_var = "remote_sortcol";
        }

      gftp_lookup_global_option (sortcol_var, &sortcol);
      gftp_lookup_global_option (sortasds_var, &sortasds);
    
      cdata->files = gftp_sort_filelist (cdata->files, sortcol, sortasds);
    }

  return (1);
}


int
gftpui_common_run_delete (gftpui_callback_data * cdata)
{
  int ret;

  if (cdata->input_string != NULL)
    {
      ret = gftp_remove_file (cdata->request, cdata->input_string);
    }
  else
    ret = 0; /* FIXME */

  return (ret);
}


int
gftpui_common_run_rmdir (gftpui_callback_data * cdata)
{
  int ret;

  if (cdata->input_string != NULL)
    {
      ret = gftp_remove_directory (cdata->request, cdata->input_string);
    }
  else
    ret = 0; /* FIXME */

  return (ret);
}


int
gftpui_common_run_connect (gftpui_callback_data * cdata)
{ 
  return (gftp_connect (cdata->request));
}

