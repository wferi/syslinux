/* ----------------------------------------------------------------------- *
 *
 *   Copyright 2009 Erwan Velu - All Rights Reserved
 *
 *   Permission is hereby granted, free of charge, to any person
 *   obtaining a copy of this software and associated documentation
 *   files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use,
 *   copy, modify, merge, publish, distribute, sublicense, and/or
 *   sell copies of the Software, and to permit persons to whom
 *   the Software is furnished to do so, subject to the following
 *   conditions:
 *
 *   The above copyright notice and this permission notice shall
 *   be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *   OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *   OTHER DEALINGS IN THE SOFTWARE.
 *
 * -----------------------------------------------------------------------
 */

#include <stdlib.h>
#include <string.h>
#include <syslinux/config.h>
#include <getkey.h>
#include "hdt-cli.h"
#include "hdt-common.h"

#define DEBUG 0
#if DEBUG
# define dprintf printf
#else
# define dprintf(f, ...) ((void)0)
#endif


#define MAX_MODES 2
struct commands_mode *list_modes[] = {
	&hdt_mode,
	&dmi_mode,
};
struct s_cli hdt_cli;

static void set_mode(cli_mode_t mode,
		     struct s_hardware *hardware)
{
	switch (mode) {
	case EXIT_MODE:
		hdt_cli.mode = mode;
		break;

	case HDT_MODE:
		hdt_cli.mode = mode;
		snprintf(hdt_cli.prompt, sizeof(hdt_cli.prompt), "%s> ", CLI_HDT);
		break;

	case PXE_MODE:
		if (hardware->sv->filesystem != SYSLINUX_FS_PXELINUX) {
			more_printf("You are not currently using PXELINUX\n");
			break;
		}
		hdt_cli.mode = mode;
		snprintf(hdt_cli.prompt, sizeof(hdt_cli.prompt), "%s> ", CLI_PXE);
		break;

	case KERNEL_MODE:
		detect_pci(hardware);
		hdt_cli.mode = mode;
		snprintf(hdt_cli.prompt, sizeof(hdt_cli.prompt), "%s> ", CLI_KERNEL);
		break;

	case SYSLINUX_MODE:
		hdt_cli.mode = mode;
		snprintf(hdt_cli.prompt, sizeof(hdt_cli.prompt), "%s> ",
			 CLI_SYSLINUX);
		break;

	case VESA_MODE:
		hdt_cli.mode = mode;
		snprintf(hdt_cli.prompt, sizeof(hdt_cli.prompt), "%s> ", CLI_VESA);
		break;

	case PCI_MODE:
		hdt_cli.mode = mode;
		snprintf(hdt_cli.prompt, sizeof(hdt_cli.prompt), "%s> ", CLI_PCI);
		if (!hardware->pci_detection)
			cli_detect_pci(hardware);
		break;

	case CPU_MODE:
		hdt_cli.mode = mode;
		snprintf(hdt_cli.prompt, sizeof(hdt_cli.prompt), "%s> ", CLI_CPU);
		if (!hardware->dmi_detection)
			detect_dmi(hardware);
		if (!hardware->cpu_detection)
			cpu_detect(hardware);
		break;

	case DMI_MODE:
		detect_dmi(hardware);
		if (!hardware->is_dmi_valid) {
			more_printf("No valid DMI table found, exiting.\n");
			break;
		}
		hdt_cli.mode = mode;
		snprintf(hdt_cli.prompt, sizeof(hdt_cli.prompt), "%s> ", CLI_DMI);
		break;
	}
}

static void handle_hdt_commands(char *cli_line, struct s_hardware *hardware)
{
	/* hdt cli mode specific commands */
	if (!strncmp(cli_line, CLI_SHOW, sizeof(CLI_SHOW) - 1)) {
		main_show(strstr(cli_line, "show") + sizeof(CLI_SHOW),
			  hardware);
		return;
	}
}

/**
 * find_commands_mode - find the commands_mode struct associated to a mode
 * @mode:	mode to look for
 *
 * Given a mode name, return a pointer to the associated commands_mode structure.
 * Note: the current mode name is stored in hdt_cli.mode.
 **/
static struct commands_mode* find_commands_mode(cli_mode_t mode)
{
	int modes_iter = 0;

	while (modes_iter < MAX_MODES &&
	       list_modes[modes_iter]->mode != hdt_cli.mode)
		modes_iter++;

	/* Shouldn't get here... */
	if (modes_iter == MAX_MODES)
		return NULL;
	else
		return list_modes[modes_iter];

}

/**
 * parse_command_line - low level parser for the command line
 * @command:	command line to parse
 *
 * The format of the command line is:
 *	<main command> [<module on which to operate> [<args>]]
 **/
static void parse_command_line(char *line, char **command, char **module,
			       char **argv)
{
	int argc, argc_iter, args_pos, token_found, len;
	char *pch = NULL, *pch_next = NULL;

	*command = NULL;
	*module = NULL;

	pch = line;
	while (pch != NULL) {
		pch_next = strchr(pch + 1, ' ');

		/* End of line guaranteed to be zeroed */
		if (pch_next == NULL)
			len = (int) (strchr(pch + 1, '\0') - pch);
		else
			len = (int) (pch_next - pch);

		if (token_found == 0) {
			/* Main command to execute */
			*command = malloc((len + 1) * sizeof(char));
			strncpy(*command, pch, len);
			(*command)[len] = NULL;
			dprintf("CLI DEBUG: command = %s\n", *command);
			args_pos += len + 1;
		} else if (token_found == 1) {
			/* Module */
			*module = malloc((len + 1) * sizeof(char));
			strncpy(*module, pch, len);
			(*module)[len] = NULL;
			dprintf("CLI DEBUG: module  = %s\n", *module);
			args_pos += len + 1;
		} else
			argc++;

		if (pch_next != NULL)
			pch_next++;

		token_found++;
		pch = pch_next;
	}
	dprintf("CLI DEBUG: argc    = %d\n", argc);

	/* Skip arguments handling if none is supplied */
	if (!argc)
		return;

	/* Transform the arguments string into an array */
	argv = malloc(argc * sizeof(char *));
	pch = strtok(line + args_pos, CLI_SPACE);
	while (pch != NULL) {
		dprintf("CLI DEBUG: argv[%d] = %s\n", argc_iter, pch);
		argv[argc_iter] = pch;
		argc_iter++;
		pch = strtok(NULL, CLI_SPACE);
	}
}

/**
 * find_module_callback - find a callback in a list of modules
 * @module_name:	Name of the module to find
 * @modules_list:	Lits of modules among which to find @module_name
 * @module_found:	Pointer to the matched module, NULL if not found
 *
 * Given a module name and a list of possible modules, find the corresponding
 * module structure that matches the module name and store it in @module_found.
 **/
static void find_module_callback(char* module_name,
				 struct commands_module_descr* modules_list,
				 struct commands_module** module_found)
{
	int modules_iter = 0;
	int module_len = strlen(module_name);

	if (modules_list == NULL)
		goto not_found;

	/* Find the callback to execute */
	while (modules_iter < modules_list->nb_modules
	       && strncmp(module_name,
			  modules_list->modules[modules_iter].name,
			  module_len) != 0)
		modules_iter++;

	if (modules_iter != modules_list->nb_modules) {
		*module_found = &(modules_list->modules[modules_iter]);
		return;
	}

not_found:
	*module_found = NULL;
	return;
}

/**
 * show_cli_help - shared helper to show available commands
 **/
static void show_cli_help(int argc, char** argv, struct s_hardware *hardware)
{
	int j;
	struct commands_mode *current_mode;
	struct commands_module* associated_module = NULL;

	current_mode = find_commands_mode(hdt_cli.mode);

	more_printf("Available commands are:\n");

	/* List first default modules of the mode */
	if (current_mode->default_modules != NULL ) {
		for (j = 0; j < current_mode->default_modules->nb_modules; j++) {
			more_printf("%s ",
				    current_mode->default_modules->modules[j].name);
		}
	}

	/* List secondly the show modules of the mode */
	if (current_mode->show_modules != NULL ) {
		more_printf("show commands:\n");
		for (j = 0; j < current_mode->show_modules->nb_modules; j++) {
			more_printf("     %s\n",
				    current_mode->show_modules->modules[j].name);
		}
	}

	/* List finally the default modules of the hdt mode */
	if (current_mode->mode != hdt_mode.mode && hdt_mode.default_modules != NULL ) {
		for (j = 0; j < hdt_mode.default_modules->nb_modules; j++) {
			/*
			 * Any default command that is present in hdt mode but
			 * not in the current mode is available. A default command
			 * can be redefined in the current mode though. This next
			 * call test this use case: if it is overwritten, do not
			 * print it again.
			 */
			find_module_callback(hdt_mode.default_modules->modules[j].name,
					     current_mode->default_modules,
					     &associated_module);
			if (associated_module == NULL)
				more_printf("%s ",
					    hdt_mode.default_modules->modules[j].name);
		}
		more_printf("\n");
	}
}


static void exec_command(char *line,
			 struct s_hardware *hardware)
{
	int argc = 0;
	char *command = NULL, *module = NULL;
	char **argv = NULL;
	struct commands_module* current_module = NULL;
	struct commands_mode *current_mode;

	/* We use sizeof BLAH - 1 to remove the last \0 */
//  command[strlen(command) - 1] = '\0';

	/* XXX Extract this with a new grammar, e.g. set mode dmi */

	if (!strncmp(line, CLI_PCI, sizeof(CLI_PCI) - 1)) {
		set_mode(PCI_MODE, hardware);
		return;
	}

	if (!strncmp(line, CLI_CPU, sizeof(CLI_CPU) - 1)) {
		set_mode(CPU_MODE, hardware);
		return;
	}

	if (!strncmp(line, CLI_DMI, sizeof(CLI_DMI) - 1)) {
		set_mode(DMI_MODE, hardware);
		return;
	}

	if (!strncmp(line, CLI_PXE, sizeof(CLI_PXE) - 1)) {
		set_mode(PXE_MODE, hardware);
		return;
	}

	if (!strncmp(line, CLI_KERNEL, sizeof(CLI_KERNEL) - 1)) {
		set_mode(KERNEL_MODE, hardware);
		return;
	}

	if (!strncmp(line, CLI_SYSLINUX, sizeof(CLI_SYSLINUX) - 1)) {
		set_mode(SYSLINUX_MODE, hardware);
		return;
	}

	if (!strncmp(line, CLI_VESA, sizeof(CLI_VESA) - 1)) {
		set_mode(VESA_MODE, hardware);
		return;
	}

	/*
	 * All commands before that line are common for all cli modes.
	 * The following will be specific for every mode.
	 */

	/* Find the mode selected */
	current_mode = find_commands_mode(hdt_cli.mode);
	if (current_mode == NULL) {
		/* Shouldn't get here... */
		printf("!!! BUG: Mode '%s' unknown.\n", hdt_cli.mode);
		// XXX Will return; when done with refactoring.
		goto old_cli;
	}

	/* This will allocate memory that will need to be freed */
	parse_command_line(line, &command, &module, argv);

	if (module == NULL) {
		/*
		 * A single word was specified: look at the list of default
		 * commands in the current mode to see if there is a match.
		 * If not, it may be a generic function (exit, help, ...). These
		 * are stored in the list of default commands of the hdt mode.
		 */
		find_module_callback(command, current_mode->default_modules,
				     &current_module);
		if (current_module != NULL)
			current_module->exec(argc, argv, hardware);
		else {
			find_module_callback(command, hdt_mode.default_modules,
					     &current_module);
			if (current_module != NULL) {
				current_module->exec(argc, argv, hardware);
				return;
			}

			printf("Command '%s' unknown.\n", command);
		}
	}


	/*
	 * Find the type of command.
	 *
	 * The syntax of the cli is the following:
	 *    <type of command> <module on which to operate> <args>
	 * e.g.
	 *    dmi> show system
	 *    dmi> show bank 1
	 *    dmi> show memory 0 1
	 *    pci> show device 12
	 */
	if (!strncmp(line, CLI_SHOW, sizeof(CLI_SHOW) - 1)) {
		find_module_callback(module, current_mode->show_modules, &current_module);
		/* Execute the callback */
		if (current_module != NULL)
			current_module->exec(argc, argv, hardware);
		/* XXX Add a default help option for empty commands */
	}
	/* Handle here other keywords such as 'set', ... */

	/* Let's not forget to clean ourselves */
	free(command);
	free(module);
	free(argv);

old_cli:
	/* Legacy cli */
	switch (hdt_cli.mode) {
	case PCI_MODE:
		handle_pci_commands(line, hardware);
		break;
	case HDT_MODE:
		handle_hdt_commands(line, hardware);
		break;
	case CPU_MODE:
		handle_cpu_commands(line, hardware);
		break;
	case PXE_MODE:
		handle_pxe_commands(line, hardware);
		break;
	case VESA_MODE:
		handle_vesa_commands(line, hardware);
		break;
	case SYSLINUX_MODE:
		handle_syslinux_commands(line, hardware);
		break;
	case KERNEL_MODE:
		handle_kernel_commands(line, hardware);
		break;
	case EXIT_MODE:
		break;		/* should not happen */
	}
}

static void reset_prompt()
{
	/* No need to display the prompt if we exit */
	if (hdt_cli.mode != EXIT_MODE) {
		printf("%s", hdt_cli.prompt);
		/* Reset the line */
		memset(hdt_cli.input, '\0', MAX_LINE_SIZE);
		hdt_cli.cursor_pos = 0;
	}
}

/* Code that manages the cli mode */
void start_cli_mode(struct s_hardware *hardware)
{
	int current_key = 0;
	char temp_command[MAX_LINE_SIZE];
	set_mode(HDT_MODE, hardware);

	printf("Entering CLI mode\n");

	/* Display the cursor */
	fputs("\033[?25h", stdout);
	reset_prompt();
	while (hdt_cli.mode != EXIT_MODE) {

		//fgets(cli_line, sizeof cli_line, stdin);
		current_key = get_key(stdin, 0);
		switch (current_key) {

			/* clear until then end of line */
		case KEY_CTRL('k'):
			/* Clear the end of the line */
			fputs("\033[0K", stdout);
			memset(&hdt_cli.input[hdt_cli.cursor_pos], 0,
			       strlen(hdt_cli.input) - hdt_cli.cursor_pos);
			break;

			/* quit current line */
		case KEY_CTRL('c'):
			more_printf("\n");
			reset_prompt();
			break;
		case KEY_TAB:
			break;

			/* Let's move to left */
		case KEY_LEFT:
			if (hdt_cli.cursor_pos > 0) {
				fputs("\033[1D", stdout);
				hdt_cli.cursor_pos--;
			}
			break;

			/* Let's move to right */
		case KEY_RIGHT:
			if (hdt_cli.cursor_pos < (int)strlen(hdt_cli.input)) {
				fputs("\033[1C", stdout);
				hdt_cli.cursor_pos++;
			}
			break;

			/* Returning at the begining of the line */
		case KEY_CTRL('e'):
		case KEY_END:
			/* Calling with a 0 value will make the cursor move */
			/* So, let's move the cursor only if needed */
			if ((strlen(hdt_cli.input) - hdt_cli.cursor_pos) > 0) {
				memset(temp_command, 0, sizeof(temp_command));
				sprintf(temp_command, "\033[%dC",
					strlen(hdt_cli.input) - hdt_cli.cursor_pos);
				/* Return to the begining of line */
				fputs(temp_command, stdout);
				hdt_cli.cursor_pos = strlen(hdt_cli.input);
			}
			break;

			/* Returning at the begining of the line */
		case KEY_CTRL('a'):
		case KEY_HOME:
			/* Calling with a 0 value will make the cursor move */
			/* So, let's move the cursor only if needed */
			if (hdt_cli.cursor_pos > 0) {
				memset(temp_command, 0, sizeof(temp_command));
				sprintf(temp_command, "\033[%dD",
					hdt_cli.cursor_pos);
				/* Return to the begining of line */
				fputs(temp_command, stdout);
				hdt_cli.cursor_pos = 0;
			}
			break;

		case KEY_ENTER:
			more_printf("\n");
			exec_command(skipspace(hdt_cli.input), hardware);
			reset_prompt();
			break;
		case KEY_BACKSPACE:
			/* Don't delete prompt */
			if (hdt_cli.cursor_pos == 0)
				break;

			for (int c = hdt_cli.cursor_pos - 1;
			     c < (int)strlen(hdt_cli.input) - 1; c++)
				hdt_cli.input[c] = hdt_cli.input[c + 1];
			hdt_cli.input[strlen(hdt_cli.input) - 1] = '\0';

			/* Get one char back */
			fputs("\033[1D", stdout);
			/* Clear the end of the line */
			fputs("\033[0K", stdout);

			/* Print the resulting buffer */
			printf("%s", hdt_cli.input + hdt_cli.cursor_pos - 1);

			/* Realing to the place we were */
			memset(temp_command, 0, sizeof(temp_command));
			sprintf(temp_command, "\033[%dD",
				strlen(hdt_cli.input + hdt_cli.cursor_pos - 1));
			fputs(temp_command, stdout);
			fputs("\033[1C", stdout);
			/* Don't decrement the position unless
			 * if we are at then end of the line*/
			if (hdt_cli.cursor_pos > (int)strlen(hdt_cli.input))
				hdt_cli.cursor_pos--;
			break;
		case KEY_F1:
			more_printf("\n");
			exec_command(CLI_HELP, hardware);
			reset_prompt();
			break;
		default:
			/* Prevent overflow */
			if (hdt_cli.cursor_pos > MAX_LINE_SIZE - 2)
				break;
			/* If we aren't at the end of the input line, let's insert */
			if (hdt_cli.cursor_pos < (int)strlen(hdt_cli.input)) {
				char key[2];
				int trailing_chars =
				    strlen(hdt_cli.input) - hdt_cli.cursor_pos;
				memset(temp_command, 0, sizeof(temp_command));
				strncpy(temp_command, hdt_cli.input,
					hdt_cli.cursor_pos);
				sprintf(key, "%c", current_key);
				strncat(temp_command, key, 1);
				strncat(temp_command,
					hdt_cli.input + hdt_cli.cursor_pos,
					trailing_chars);
				memset(hdt_cli.input, 0, sizeof(hdt_cli.input));
				snprintf(hdt_cli.input, sizeof(hdt_cli.input), "%s",
					 temp_command);

				/* Clear the end of the line */
				fputs("\033[0K", stdout);

				/* Print the resulting buffer */
				printf("%s", hdt_cli.input + hdt_cli.cursor_pos);
				sprintf(temp_command, "\033[%dD",
					trailing_chars);
				/* Return where we must put the new char */
				fputs(temp_command, stdout);

			} else {
				putchar(current_key);
				hdt_cli.input[hdt_cli.cursor_pos] = current_key;
			}
			hdt_cli.cursor_pos++;
			break;
		}
	}
}

/**
 * do_exit - shared helper to exit a mode
 **/
static void do_exit(int argc, char** argv, struct s_hardware *hardware)
{
	int new_mode = HDT_MODE;

	switch (hdt_cli.mode) {
	case HDT_MODE:
		new_mode = EXIT_MODE;
		break;
	case KERNEL_MODE:
	case PXE_MODE:
	case SYSLINUX_MODE:
	case PCI_MODE:
	case DMI_MODE:
	case VESA_MODE:
	case CPU_MODE:
		new_mode = HDT_MODE;
		break;
	case EXIT_MODE:
		new_mode = EXIT_MODE;	/* should not happen */
		break;
	}

	dprintf("CLI DEBUG: Switching from mode %d to mode %d\n", hdt_cli.mode,
								  new_mode);
	set_mode(new_mode, hardware);
}

static void main_show_summary(struct s_hardware *hardware)
{
	detect_pci(hardware);	/* pxe is detected in the pci */
	detect_dmi(hardware);
	cpu_detect(hardware);
	clear_screen();
	main_show_cpu(hardware);
	if (hardware->is_dmi_valid) {
		more_printf("System\n");
		more_printf(" Manufacturer : %s\n",
			    hardware->dmi.system.manufacturer);
		more_printf(" Product Name : %s\n",
			    hardware->dmi.system.product_name);
		more_printf(" Serial       : %s\n",
			    hardware->dmi.system.serial);
		more_printf("Bios\n");
		more_printf(" Version      : %s\n", hardware->dmi.bios.version);
		more_printf(" Release      : %s\n",
			    hardware->dmi.bios.release_date);

		int argc = 2;
		char *argv[2] = { "0", "0" };
		show_dmi_memory_modules(argc, argv, hardware);
	}
	main_show_pci(hardware);

	if (hardware->is_pxe_valid)
		main_show_pxe(hardware);

	main_show_kernel(hardware);
}

void show_main_help(struct s_hardware *hardware)
{
	more_printf("Show supports the following commands : \n");
	more_printf(" %s\n", CLI_SUMMARY);
	more_printf(" %s\n", CLI_PCI);
	more_printf(" %s\n", CLI_DMI);
	more_printf(" %s\n", CLI_CPU);
	more_printf(" %s\n", CLI_KERNEL);
	more_printf(" %s\n", CLI_SYSLINUX);
	more_printf(" %s\n", CLI_VESA);
	if (hardware->sv->filesystem == SYSLINUX_FS_PXELINUX)
		more_printf(" %s\n", CLI_PXE);
}

void main_show(char *item, struct s_hardware *hardware)
{
	if (!strncmp(item, CLI_SUMMARY, sizeof(CLI_SUMMARY))) {
		main_show_summary(hardware);
		return;
	}
	if (!strncmp(item, CLI_PCI, sizeof(CLI_PCI))) {
		main_show_pci(hardware);
		return;
	}
	if (!strncmp(item, CLI_DMI, sizeof(CLI_DMI))) {
		main_show_dmi(hardware);
		return;
	}
	if (!strncmp(item, CLI_CPU, sizeof(CLI_CPU))) {
		main_show_cpu(hardware);
		return;
	}
	if (!strncmp(item, CLI_PXE, sizeof(CLI_PXE))) {
		main_show_pxe(hardware);
		return;
	}
	if (!strncmp(item, CLI_SYSLINUX, sizeof(CLI_SYSLINUX))) {
		main_show_syslinux(hardware);
		return;
	}
	if (!strncmp(item, CLI_KERNEL, sizeof(CLI_KERNEL))) {
		main_show_kernel(hardware);
		return;
	}
	if (!strncmp(item, CLI_VESA, sizeof(CLI_VESA))) {
		main_show_vesa(hardware);
		return;
	}
	show_main_help(hardware);
}

/* Default hdt mode */
struct commands_module list_hdt_default_modules[] = {
	{
		.name = CLI_CLEAR,
		.exec = clear_screen,
	},
	{
		.name = CLI_EXIT,
		.exec = do_exit,
	},
	{
		.name = CLI_HELP,
		.exec = show_cli_help,
	},
};

struct commands_module_descr hdt_default_modules = {
	.modules = list_hdt_default_modules,
	.nb_modules = 3,
};

struct commands_mode hdt_mode = {
	.mode = HDT_MODE,
	.default_modules = &hdt_default_modules,
	.show_modules = NULL,
};