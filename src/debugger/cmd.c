/*
Copyright (C) 2004-2005, 2007-2009, 2011,
              2014-2015, 2020 R. Bernstein
<rocky@gnu.org>
This file is part of GNU Make (remake variant).

GNU Make is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU Make is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Make; see the file COPYING.  If not, write to
the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* debugger command interface. */

#include "makeint.h"
#include "msg.h"
#include "debug.h"
#include "debugger.h"
#include "file.h"
#include "print.h"
#include "break.h"
#include "cmd.h"
#include "fns.h"
#include "function.h"
#include "info.h"
#include "stack.h"
#include "commands.h"
#include "expand.h"
#include "debug.h"
#include "file2line.h"

#ifdef HAVE_READLINE_READLINE_H
#include <readline/readline.h>
#endif

#ifdef HAVE_READLINE_HISTORY_H
#include <readline/history.h>
#endif

#ifdef _GNU_SOURCE
# define ATTRIBUTE_UNUSED __attribute__((unused))
#else
# define ATTRIBUTE_UNUSED
#endif

/**
   Think of the below not as an enumeration but as #defines done in a
   way that we'll be able to use the value in a gdb.
 **/
enum {
  MAX_FILE_LENGTH   = 1000,
} debugger_enum1;


/**
   Command-line args after the command-name part. For example in:
   break foo
   the below will be "foo".
 **/
char *psz_debugger_args;

debug_enter_reason_t last_stop_reason;

#ifdef HAVE_LIBREADLINE
#include <stdio.h>
#include <stdlib.h>
/* The following line makes Solaris' gcc/cpp not puke. */
#undef HAVE_READLINE_READLINE_H
#include <readline/readline.h>

/* From readline. ?? Should this be in configure?  */
#ifndef whitespace
#define whitespace(c) (((c) == ' ') || ((c) == '\t'))
#endif /* HAVE_LIBREADLINE */

/* A structure which contains information on the commands this program
   can understand. */

static debug_return_t dbg_cmd_set_var (char *psz_arg, int expand);

/* Should be in alphabetic order by command name. */
long_cmd_t commands[] = {
  { "break",    'b' },
  { "cd",       'C' },
  { "comment",  '#' },
  { "continue", 'c' },
  { "delete",   'd' },
  { "down",     'D' },
  { "edit" ,    'e' },
  { "expand" ,  'x' },
  { "finish"  , 'F' },
  { "frame"   , 'f' },
  { "help"    , 'h' },
  { "info"    , 'i' },
  { "list"    , 'l' },
  { "load"    , 'M' },
  { "next"    , 'n' },
  { "print"   , 'p' },
  { "pwd"     , 'P' },
  { "quit"    , 'q' },
  { "run"     , 'R' },
  { "set"     , '=' },
  { "setq"    , '"' },
  { "setqx"   , '`' },
  { "shell"   , '!' },
  { "show"    , 'S' },
  { "skip"    , 'k' },
  { "source"  , '<' },
  { "step"    , 's' },
  { "target"  , 't' },
  { "up"      , 'u' },
  { "where"   , 'T' },
  { "write"   , 'w' },
  { (char *)NULL, ' '}
};

typedef struct {
  const char *command;	        /* real command name. */
  const char *alias;	        /* alias for command. */
} alias_cmd_t;

/* Should be in alphabetic order by ALIAS name. */

alias_cmd_t aliases[] = {
  { "shell",    "!!" },
  { "help",     "?" },
  { "break",    "L" },
  { "where",    "backtrace" },
  { "where",    "bt" },
  { "quit",     "exit" },
  { "run",      "restart" },
  { "quit",     "return" },
  { (char *)NULL, (char *) NULL}
};


short_cmd_t short_command[256] = { { NULL,
                                     (const char *) '\0',
                                     (const char *) '\0',
                                     (uint8_t) 255,
                                    }, };

/* Look up NAME as the name of a command, and return a pointer to that
   command.  Return a NULL pointer if NAME isn't a command name. */
static short_cmd_t *
find_command (const char *psz_name)
{
  unsigned int i;

  for (i = 0; aliases[i].alias; i++) {
    const int cmp = strcmp (psz_name, aliases[i].alias);
    if ( 0 == cmp ) {
      psz_name = aliases[i].command;
      break;
    } else
      /* Words should be in alphabetic order by alias name.
	 Have we gone too far? */
      if (cmp < 0) break;
  }

  for (i = 0; commands[i].long_name; i++) {
    const int cmp = strcmp (psz_name, commands[i].long_name);
    if ( 0 == cmp ) {
      return &(short_command[(uint8_t) commands[i].short_name]);
    } else
      /* Words should be in alphabetic order by command name.
	 Have we gone too far? */
      if (cmp < 0) break;
  }

  return ((short_cmd_t *)NULL);
}

#include "command/break.h"
#include "command/chdir.h"
#include "command/comment.h"
#include "command/continue.h"
#include "command/delete.h"
#include "command/down.h"
#include "command/edit.h"
#include "command/expand.h"
#include "command/finish.h"
#include "command/frame.h"
#include "command/info.h"
#include "command/load.h"
#include "command/next.h"
#include "command/list.h"
#include "command/print.h"
#include "command/pwd.h"
#include "command/quit.h"
#include "command/run.h"
#include "command/set.h"
#include "command/setq.h"
#include "command/setqx.h"
#include "command/shell.h"
#include "command/show.h"
#include "command/skip.h"
#include "command/source.h"
#include "command/step.h"
#include "command/target.h"
#include "command/up.h"
#include "command/where.h"
#include "command/write.h"

/* Needs to come after command/show.h */
#include "command/help.h"

#include "command/help/break.h"
#include "command/help/chdir.h"
#include "command/help/comment.h"
#include "command/help/continue.h"
#include "command/help/delete.h"
#include "command/help/down.h"
#include "command/help/edit.h"
#include "command/help/expand.h"
#include "command/help/finish.h"
#include "command/help/frame.h"
#include "command/help/help.h"
#include "command/help/info.h"
#include "command/help/load.h"
#include "command/help/next.h"
#include "command/help/list.h"
#include "command/help/print.h"
#include "command/help/pwd.h"
#include "command/help/quit.h"
#include "command/help/run.h"
#include "command/help/set.h"
#include "command/help/setq.h"
#include "command/help/setqx.h"
#include "command/help/shell.h"
#include "command/help/show.h"
#include "command/help/skip.h"
#include "command/help/step.h"
#include "command/help/target.h"
#include "command/help/up.h"
#include "command/help/where.h"
#include "command/help/write.h"


#define DBG_CMD_INIT(CMD, LETTER)                       \
  dbg_cmd_ ## CMD ## _init(LETTER);                     \
  short_command[LETTER].doc = _(CMD ## _HELP_TEXT);   \
  short_command[LETTER].id = id++

static void
cmd_initialize(void)
{
  int id=0;
  DBG_CMD_INIT(break, 'b');
  DBG_CMD_INIT(chdir, 'C');
  DBG_CMD_INIT(comment, '#');
  DBG_CMD_INIT(continue, 'c');
  DBG_CMD_INIT(delete, 'd');
  DBG_CMD_INIT(down, 'D');
  DBG_CMD_INIT(edit, 'e');
  DBG_CMD_INIT(expand, 'x');
  DBG_CMD_INIT(finish, 'F');
  DBG_CMD_INIT(frame, 'f');
  DBG_CMD_INIT(help, 'h');
  DBG_CMD_INIT(info, 'i');
  DBG_CMD_INIT(list, 'l');
  DBG_CMD_INIT(load, 'M');
  DBG_CMD_INIT(next, 'n');
  DBG_CMD_INIT(print, 'p');
  DBG_CMD_INIT(pwd, 'P');
  DBG_CMD_INIT(quit, 'q');
  DBG_CMD_INIT(run, 'R');
  DBG_CMD_INIT(set, '=');
  DBG_CMD_INIT(setq, '"');
  DBG_CMD_INIT(setqx, '`');
  DBG_CMD_INIT(shell, '!');
  DBG_CMD_INIT(show, 'S');
  DBG_CMD_INIT(skip, 'k');
  DBG_CMD_INIT(source, '<');
  DBG_CMD_INIT(step, 's');
  DBG_CMD_INIT(target, 't');
  DBG_CMD_INIT(up, 'u');
  DBG_CMD_INIT(where, 'T');
  DBG_CMD_INIT(write, 'w');
}

/* Execute a command line. */
extern debug_return_t
execute_line (char *psz_line)
{
  unsigned int i = 0;
  short_cmd_t *command;
  char *psz_word = get_word(&psz_line);

  if (1 == strlen(psz_word)) {
    if ( NULL != short_command[(uint8_t) psz_word[0]].func )
      command = &short_command[(uint8_t) psz_word[0]];
    else
      command = NULL;
  } else {
    command = find_command (psz_word);
  }
  if (!command)
    {
      dbg_errmsg(_("No such debugger command: %s."), psz_word);
      return debug_readloop;
    }

  /* Get argument to command, if any. */
  while (whitespace (psz_line[i]))
    i++;

  psz_debugger_args = psz_line + i;

  /* Call the function. */
  return ((*(command->func)) (psz_debugger_args));
}

/* Show history. */
debug_return_t
dbg_cmd_show_command (const char
                      *psz_args ATTRIBUTE_UNUSED)
{
 /*
  if (!psz_arg || *psz_arg) {
    ;
    } */

#ifdef HAVE_READLINE_HISTORY_H
  HIST_ENTRY **hist_list = history_list();
  unsigned int i;
  UNUSED_ARGUMENT(psz_args);
  if (!hist_list) return debug_readloop;
  for (i=1; hist_list[i]; i++) {
    dbg_msg("%5u  %s", i, hist_list[i]->line);
  }
#endif /* HAVE_READLINE_HISTORY_H */
  return debug_readloop;
}

/* Set a variable. Set "expand' to 1 if you want variable
   definitions inside the value getting passed in to be expanded
   before assigment. */
static debug_return_t dbg_cmd_set_var (char *psz_args, int expand)
{
  if (!psz_args || 0==strlen(psz_args)) {
    dbg_msg(_("You need to supply a variable name."));
  } else {
    variable_t *p_v;
    char *psz_varname = get_word(&psz_args);
    unsigned int u_len = strlen(psz_varname);

    while (*psz_args && whitespace (*psz_args))
      *psz_args +=1;

    p_v = lookup_variable (psz_varname, u_len);

    if (p_v) {
      char *psz_value =  expand ? variable_expand(psz_args) : psz_args;

      define_variable_in_set(p_v->name, u_len, psz_value,
			     o_debugger, 0, NULL,
			     &(p_v->fileinfo));
      dbg_msg(_("Variable %s now has value '%s'"), psz_varname,
	     psz_value);
    } else {
      p_v = try_without_dollar(psz_varname);
      if (p_v) {
        char *psz_value =  expand ? variable_expand(psz_args) : psz_args;
        define_variable_in_set(p_v->name, u_len, psz_value,
                               o_debugger, 0, NULL,
                               &(p_v->fileinfo));
        dbg_msg(_("Variable %s now has value '%s'"), psz_varname,
                psz_value);
      }
    }
  }
  return debug_readloop;
}

#endif /* HAVE_LIBREADLINE */

#define PROMPT_LENGTH 300

#include <setjmp.h>
jmp_buf debugger_loop;

/* Should be less that PROMPT_LENGTH / 2 - strlen("remake ") + log(history)
   We will make it much less that since people can't count more than
   10 or so nested <<<<>>>>'s easily.
*/
#define MAX_NEST_DEPTH 10

debug_return_t enter_debugger (target_stack_node_t *p,
			       file_t *p_target, int errcode,
			       debug_enter_reason_t reason)
{
  volatile debug_return_t debug_return = debug_readloop;
  static bool b_init = false;
  static bool b_readline_init = false;
  char open_depth[MAX_NEST_DEPTH];
  char close_depth[MAX_NEST_DEPTH];
  unsigned int i = 0;

  last_stop_reason = reason;

  if ( in_debugger == DEBUGGER_QUIT_RC ) {
    return continue_execution;
  }

  if ( i_debugger_stepping > 1 || i_debugger_nexting > 1 ) {
    /* Don't stop unless we are here from a breakpoint. But
       do decrement the step count. */
    if (i_debugger_stepping)  i_debugger_stepping--;
    if (i_debugger_nexting)   i_debugger_nexting--;
    if (!p_target->tracing) return continue_execution;
  } else if ( !debugger_on_error
	      && !(i_debugger_stepping || i_debugger_nexting)
	      && p_target && !p_target->tracing && -2 != errcode )
    return continue_execution;

  /* Clear temporary breakpoints. */
  if (p_target && p_target->tracing & BRK_TEMP)
    switch(last_stop_reason)
      {
      case DEBUG_BRKPT_AFTER_CMD:
      case DEBUG_BRKPT_BEFORE_PREREQ:
      case DEBUG_BRKPT_AFTER_PREREQ:
        p_target->tracing = BRK_NONE;
      default:
        ;
      }

#ifdef HAVE_LIBREADLINE
  if (use_readline_flag && !b_readline_init) {
      rl_initialize ();
      using_history ();
      add_history ("");
      b_readline_init = true;
  }
#endif /* HAVE_LIBREADLINE */
  if (!b_init) {
    cmd_initialize();
    file2lines.ht_size = 0;
    b_init = true;
  }


  /* Set initial frame position reporting area: 0 is bottom. */
  p_target_loc    = NULL;
  psz_target_name = (char *) "";
  i_stack_pos      = 0;

  p_stack = p_stack_top = p;
  p_floc_stack = p_stack_floc_top;

  /* Get the target name either from the stack top (preferred) or
     the passed in target.
   */
  if (p && p->p_target) {
    p_target_loc    = &(p->p_target->floc);
    psz_target_name = (char *) p->p_target->name;
  } else if (p_target) {
    p_target_loc    = &(p_target->floc);
    psz_target_name = (char *) p_target->name;
  }

  for (i=0; i<=makelevel && i < MAX_NEST_DEPTH-5; i++) {
    open_depth[i]  = '<';
    close_depth[i] = '>';
  }

  if ( MAX_NEST_DEPTH - 5 == i ) {
    close_depth[i] = open_depth[i]  = '.'; i++;
    close_depth[i] = open_depth[i]  = '.'; i++;
    close_depth[i] = open_depth[i]  = '.'; i++;
  }

  open_depth[i] = close_depth[i] = '\0';

  in_debugger = true;

  if (errcode) {
    if (-1 == errcode) {
      printf("\n***Entering debugger because we encountered an error.\n");
    } else if (-2 == errcode) {
      if (0 == makelevel) {
	printf("\nMakefile terminated.\n");
	dbg_msg("Use q to quit or R to restart");
      } else {
	printf("\nMakefile finished at level %u. Use R to restart\n",
	       makelevel);
	dbg_msg("the makefile at this level or 's', 'n', or 'F' to continue "
	       "in parent");
	in_debugger = DEBUGGER_QUIT_RC;
      }
    } else {
      printf("\n***Entering debugger because we encountered a fatal error.\n");
      dbg_errmsg("Exiting the debugger will exit make with exit code %d.",
                 errcode);
    }
  }

  print_debugger_location(p_target, reason, NULL);

  /* Loop reading and executing lines until the user quits. */
  for ( debug_return = debug_readloop;
	debug_return == debug_readloop || debug_return == debug_cmd_error; ) {
    char prompt[PROMPT_LENGTH];
    char *line=NULL;
    char *s;

    if (setjmp(debugger_loop))
      dbg_errmsg("Internal error jumped back to debugger loop");
    else {

#ifdef HAVE_LIBREADLINE
      if (use_readline_flag) {
        snprintf(prompt, PROMPT_LENGTH, "remake%s%d%s ",
                 open_depth, where_history(), close_depth);

        line = readline (prompt);
      } else
#endif /* HAVE_LIBREADLINE */
        {
          snprintf(prompt, PROMPT_LENGTH, "remake%s0%s ", open_depth,
                   close_depth);
          printf("%s", prompt);
          if (line == NULL) line = calloc(1, 2048);
          line = fgets(line, 2048, stdin);
          if (NULL != line) chomp(line);
        }

      if ( line ) {
        if ( *(s=stripwhite(line)) ) {
          add_history (s);
          debug_return=execute_line(s);
        } else {
          add_history ("step");
          debug_return=dbg_cmd_step((char *) "");
        }
        free (line);
      } else {
        dbg_cmd_quit(NULL);
      }
    }
  }

  if (in_debugger != DEBUGGER_QUIT_RC)
    in_debugger=false;

  return debug_return;
}

/*
 * Local variables:
 * eval: (c-set-style "gnu")
 * indent-tabs-mode: nil
 * End:
 */