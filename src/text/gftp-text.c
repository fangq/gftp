/*****************************************************************************/
/*  gftp-text.c - text port of gftp                                          */
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

#include "gftp-text.h"
static const char cvsid[] = "$Id$";

static gftp_request * gftp_text_locreq = NULL;
static gftp_request * gftp_text_remreq = NULL;

int
gftp_text_get_win_size (void)
{
  struct winsize size;
  int ret;

  if (ioctl (0, TIOCGWINSZ, (char *) &size) < 0)
    ret = 80;
  else
    ret = size.ws_col;

  return (ret);
}


void
gftp_text_log (gftp_logging_level level, gftp_request * request, 
               const char *string, ...)
{
  char tempstr[512], *stpos, *endpos, *utf8_str = NULL, *outstr;
  va_list argp;
  int sw;

  g_return_if_fail (string != NULL);

  switch (level)
    {
      case gftp_logging_send:
        printf ("%s", GFTPUI_COMMON_COLOR_GREEN);
        break;
      case gftp_logging_recv:
        printf ("%s", GFTPUI_COMMON_COLOR_YELLOW);
        break;
      case gftp_logging_error:
        printf ("%s", GFTPUI_COMMON_COLOR_RED);
        break;
      default:
        printf ("%s", GFTPUI_COMMON_COLOR_DEFAULT);
        break;
    }

  va_start (argp, string);
  g_vsnprintf (tempstr, sizeof (tempstr), string, argp);
  va_end (argp);

#if GLIB_MAJOR_VERSION > 1
  if (!g_utf8_validate (tempstr, -1, NULL))
    utf8_str = gftp_string_to_utf8 (request, tempstr);
#endif

  if (utf8_str != NULL)
    outstr = utf8_str;
  else
    outstr = tempstr;

  if (gftp_logfd != NULL && level != gftp_logging_misc_nolog)
    {
      fwrite (outstr, 1, strlen (outstr), gftp_logfd);
      if (ferror (gftp_logfd))
        {
          fclose (gftp_logfd);
          gftp_logfd = NULL;
        }
      else
        fflush (gftp_logfd);
    }

  sw = gftp_text_get_win_size ();
  stpos = outstr;
  endpos = outstr + 1;
  do
    {
      if (strlen (stpos) <= sw)
        {
          printf ("%s", stpos);
          break;
        }
      for (endpos = stpos + sw - 1; *endpos != ' ' && endpos > stpos; endpos--);
      if (endpos != stpos)
        {
          *endpos = '\0';
        }
      printf ("%s\n", stpos);
      stpos = endpos + 1;
    }
  while (stpos != endpos);
  
  printf ("%s", GFTPUI_COMMON_COLOR_DEFAULT);

  if (utf8_str != NULL)
    g_free (utf8_str);
}


char *
gftp_text_ask_question (const char *question, int echo, char *buf, size_t size)
{
  char *pos, *termname, singlechar;
  struct termios term, oldterm;
  sigset_t sig, sigsave;
  FILE *infd;

  if (!echo)
    {
      sigemptyset (&sig);
      sigaddset (&sig, SIGINT);
      sigaddset (&sig, SIGTSTP);
      sigprocmask (SIG_BLOCK, &sig, &sigsave);

      termname = ctermid (NULL);
      if ((infd = fopen (termname, "r+")) == NULL)
        {
          
          gftp_text_log (gftp_logging_error, NULL, 
                         _("Cannot open controlling terminal %s\n"), termname);
          return (NULL);
        }

      tcgetattr (0, &term);
      oldterm = term;
      term.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL); 
      tcsetattr (fileno (infd), TCSAFLUSH, &term);
    }
  else
    infd = stdin;

  printf ("%s%s%s ", GFTPUI_COMMON_COLOR_BLUE, question, GFTPUI_COMMON_COLOR_DEFAULT);

  if (size == 1)
    {
      singlechar = fgetc (infd);
      *buf = singlechar;
    }
  else
    {
      if (fgets (buf, size, infd) == NULL)
        return (NULL);

      if (size > 1)
        buf[size - 1] = '\0';
    }

  if (!echo)
    {
      printf ("\n");
      tcsetattr (fileno (infd), TCSAFLUSH, &oldterm);
      fclose (infd);
      sigprocmask (SIG_SETMASK, &sigsave, NULL);
    }

  if (size > 1)
    {
      for (pos = buf + strlen (buf) - 1; *pos == ' ' || *pos == '\r' ||
                                         *pos == '\n'; pos--);
      *(pos+1) = '\0';

      for (pos = buf; *pos == ' '; pos++);  

      if (*pos == '\0')
        return (NULL);

      return (pos);
    }
  else
    return (buf);
}


int
main (int argc, char **argv)
{
  char *startup_directory;
#if HAVE_LIBREADLINE
  char *tempstr, prompt[20];
#else
  char tempstr[512];
#endif

  gftpui_common_init (&argc, &argv, gftp_text_log);

  /* SSH doesn't support reading the password with askpass via the command 
     line */

  gftp_text_remreq = gftp_request_new ();
  gftp_set_request_option (gftp_text_remreq, "ssh_use_askpass", 
                           GINT_TO_POINTER(0));
  gftp_set_request_option (gftp_text_remreq, "sshv2_use_sftp_subsys", 
                           GINT_TO_POINTER(0));
  gftp_text_remreq->logging_function = gftp_text_log;

  gftp_text_locreq = gftp_request_new ();
  gftp_set_request_option (gftp_text_locreq, "ssh_use_askpass", 
                           GINT_TO_POINTER(0));
  gftp_set_request_option (gftp_text_locreq, "sshv2_use_sftp_subsys", 
                           GINT_TO_POINTER(0));

  gftp_text_locreq->logging_function = gftp_text_log;
  if (gftp_protocols[GFTP_LOCAL_NUM].init (gftp_text_locreq) == 0)
    {
      gftp_lookup_request_option (gftp_text_locreq, "startup_directory", 
                                  &startup_directory);
      if (*startup_directory != '\0')
        gftp_set_directory (gftp_text_locreq, startup_directory);

      gftp_connect (gftp_text_locreq);
    }

  gftpui_common_about (gftp_text_log, NULL);
  gftp_text_log (gftp_logging_misc, NULL, "\n");

/* FIXME
  if (argc == 3 && strcmp (argv[1], "-d") == 0)
    {
      if ((pos = strrchr (argv[2], '/')) != NULL)
        *pos = '\0';
      gftp_text_open (gftp_text_remreq, argv[2], NULL);

      if (pos != NULL)
        *pos = '/';

      gftp_text_mget_file (gftp_text_remreq, pos + 1, NULL);
      exit (0);
    }
  else if (argc == 2)
    gftp_text_open (gftp_text_remreq, argv[1], NULL);
*/

#if HAVE_LIBREADLINE
  g_snprintf (prompt, sizeof (prompt), "%sftp%s> ", GFTPUI_COMMON_COLOR_BLUE, GFTPUI_COMMON_COLOR_DEFAULT);
  while ((tempstr = readline (prompt)))
    {
      if (gftpui_common_process_command (NULL, gftp_text_locreq,
                                         NULL, gftp_text_remreq, tempstr) == 0)
        break;
   
      add_history (tempstr);
      free (tempstr);
    }
#else
  printf ("%sftp%s> ", GFTPUI_COMMON_COLOR_BLUE, GFTPUI_COMMON_COLOR_DEFAULT);
  while (fgets (tempstr, sizeof (tempstr), stdin) != NULL)
    {
      if (gftpui_common_process_command (tempstr) == 0)
        break;

      printf ("%sftp%s> ", GFTPUI_COMMON_COLOR_BLUE, GFTPUI_COMMON_COLOR_DEFAULT);
    }
#endif
 
  return (0);
}

