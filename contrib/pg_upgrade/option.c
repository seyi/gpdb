/*
 *	opt.c
 *
 *	options functions
 *
 *	Copyright (c) 2010-2011, PostgreSQL Global Development Group
 *	contrib/pg_upgrade/option.c
 */

#include "pg_upgrade.h"

#include "getopt_long.h"

#ifdef WIN32
#include <io.h>
#endif


static void usage(void);
static void validateDirectoryOption(char **dirpath,
				   char *envVarName, char *cmdLineOption, char *description);
static void get_pkglibdirs(void);
static char *get_pkglibdir(const char *bindir);


UserOpts	user_opts;


/*
 * parseCommandLine()
 *
 *	Parses the command line (argc, argv[]) and loads structures
 */
void
parseCommandLine(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"old-datadir", required_argument, NULL, 'd'},
		{"new-datadir", required_argument, NULL, 'D'},
		{"old-bindir", required_argument, NULL, 'b'},
		{"new-bindir", required_argument, NULL, 'B'},
		{"old-port", required_argument, NULL, 'p'},
		{"new-port", required_argument, NULL, 'P'},

		{"user", required_argument, NULL, 'u'},
		{"check", no_argument, NULL, 'c'},
		{"debug", no_argument, NULL, 'g'},
		{"debugfile", required_argument, NULL, 'G'},
		{"link", no_argument, NULL, 'k'},
		{"logfile", required_argument, NULL, 'l'},
		{"verbose", no_argument, NULL, 'v'},
		{"progress", no_argument, NULL, 'X'},
		{"add-checksum", no_argument, NULL, 'J'},
		{"remove-checksum", no_argument, NULL, 'j'},

		{"dispatcher-mode", no_argument, NULL, 1},

		{NULL, 0, NULL, 0}
	};
	int			option;			/* Command line option */
	int			optindex = 0;	/* used by getopt_long */
	int			os_user_effective_id;

	user_opts.transfer_mode = TRANSFER_MODE_COPY;

	os_info.progname = get_progname(argv[0]);

	/* Process libpq env. variables; load values here for usage() output */
	old_cluster.port = getenv("PGPORT") ? atoi(getenv("PGPORT")) : DEF_PGPORT;
	new_cluster.port = getenv("PGPORT") ? atoi(getenv("PGPORT")) : DEF_PGPORT;

	os_user_effective_id = get_user_info(&os_info.user);
	/* we override just the database user name;  we got the OS id above */
	if (getenv("PGUSER"))
	{
		pg_free(os_info.user);
		/* must save value, getenv()'s pointer is not stable */
		os_info.user = pg_strdup(getenv("PGUSER"));
	}

	if (argc > 1)
	{
		if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 ||
			strcmp(argv[1], "-?") == 0)
		{
			usage();
			exit(0);
		}
		if (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)
		{
			puts("pg_upgrade (PostgreSQL) " PG_VERSION);
			exit(0);
		}
	}

	/* Allow help and version to be run as root, so do the test here. */
	if (os_user_effective_id == 0)
		pg_log(PG_FATAL, "%s: cannot be run as root\n", os_info.progname);

	getcwd(os_info.cwd, MAXPGPATH);

	while ((option = getopt_long(argc, argv, "d:D:b:B:cgG:jJkl:p:P:u:v",
								 long_options, &optindex)) != -1)
	{
		switch (option)
		{
			case 'b':
				old_cluster.bindir = pg_strdup(optarg);
				break;

			case 'B':
				new_cluster.bindir = pg_strdup(optarg);
				break;

			case 'c':
				user_opts.check = true;
				break;

			case 'd':
				old_cluster.pgdata = pg_strdup(optarg);
				break;

			case 'D':
				new_cluster.pgdata = pg_strdup(optarg);
				break;

			case 'g':
				pg_log(PG_REPORT, "Running in debug mode\n");
				log_opts.debug = true;
				break;

			case 'G':
				if ((log_opts.debug_fd = fopen(optarg, "w")) == NULL)
				{
					pg_log(PG_FATAL, "cannot open debug file\n");
					exit(1);
				}
				break;

			case 'j':
				user_opts.checksum_mode = CHECKSUM_REMOVE;
				break;

			case 'J':
				user_opts.checksum_mode = CHECKSUM_ADD;
				break;

			case 'k':
				user_opts.transfer_mode = TRANSFER_MODE_LINK;
				break;

			case 'l':
				log_opts.filename = pg_strdup(optarg);
				break;

			case 'p':
				if ((old_cluster.port = atoi(optarg)) <= 0)
				{
					pg_log(PG_FATAL, "invalid old port number\n");
					exit(1);
				}
				break;

			case 'P':
				if ((new_cluster.port = atoi(optarg)) <= 0)
				{
					pg_log(PG_FATAL, "invalid new port number\n");
					exit(1);
				}
				break;

			case 'u':
				pg_free(os_info.user);
				os_info.user = pg_strdup(optarg);

				/*
				 * Push the user name into the environment so pre-9.1
				 * pg_ctl/libpq uses it.
				 */
				pg_putenv("PGUSER", os_info.user);
				break;

			case 'v':
				pg_log(PG_REPORT, "Running in verbose mode\n");
				log_opts.verbose = true;
				break;

			case 'X':
				pg_log(PG_REPORT, "Running in progress report mode\n");
				log_opts.progress = true;
				break;

			case 1:		/* --dispatcher-mode */
				/*
				 * XXX: Ideally, we could tell just by looking at the data
				 * directory, whether it's a QD node, or a QE node. You might
				 * think that we could look at Gp_role or gp_contentid, but
				 * alas, we pass those options on the pg_ctl command line,
				 * they are not stored permanently in the data directory
				 * itself, and we don't want to dig into the auxiliary
				 * config files created by gpinitsystem. So for now, the
				 * caller of pg_upgrade must use the --dispatcher-mode
				 * option, when upgrading the QD node.
				 */
				user_opts.dispatcher_mode = true;
				break;

			default:
				pg_log(PG_FATAL,
					   "Try \"%s --help\" for more information.\n",
					   os_info.progname);
				break;
		}
	}

	if (log_opts.filename != NULL)
	{
		/*
		 * We must use append mode so output generated by child processes via
		 * ">>" will not be overwritten, and we want the file truncated on
		 * start.
		 */
		/* truncate */
		if ((log_opts.fd = fopen(log_opts.filename, "w")) == NULL)
			pg_log(PG_FATAL, "cannot write to log file %s\n", log_opts.filename);
		fclose(log_opts.fd);
		if ((log_opts.fd = fopen(log_opts.filename, "a")) == NULL)
			pg_log(PG_FATAL, "cannot write to log file %s\n", log_opts.filename);
	}
	else
		log_opts.filename = strdup(DEVNULL);

	/* if no debug file name, output to the terminal */
	if (log_opts.debug && !log_opts.debug_fd)
	{
		log_opts.debug_fd = fopen(DEVTTY, "w");
		if (!log_opts.debug_fd)
			pg_log(PG_FATAL, "cannot write to terminal\n");
	}

	/* Ensure we are only adding checksums in copy mode */
	if (user_opts.transfer_mode != TRANSFER_MODE_COPY &&
		user_opts.checksum_mode != CHECKSUM_NONE)
		pg_log(PG_FATAL, "Adding and removing checksums only supported in copy mode.\n");

	/* Get values from env if not already set */
	validateDirectoryOption(&old_cluster.bindir, "OLDBINDIR", "-b",
							"old cluster binaries reside");
	validateDirectoryOption(&new_cluster.bindir, "NEWBINDIR", "-B",
							"new cluster binaries reside");
	validateDirectoryOption(&old_cluster.pgdata, "OLDDATADIR", "-d",
							"old cluster data resides");
	validateDirectoryOption(&new_cluster.pgdata, "NEWDATADIR", "-D",
							"new cluster data resides");

	get_pkglibdirs();
}


static void
usage(void)
{
	printf(_("pg_upgrade upgrades a PostgreSQL cluster to a different major version.\n\
\nUsage:\n\
  pg_upgrade [OPTIONS]...\n\
\n\
Options:\n\
  -b, --old-bindir=OLDBINDIR    old cluster executable directory\n\
  -B, --new-bindir=NEWBINDIR    new cluster executable directory\n\
  -c, --check                   check clusters only, don't change any data\n\
  -d, --old-datadir=OLDDATADIR  old cluster data directory\n\
  -D, --new-datadir=NEWDATADIR  new cluster data directory\n\
  -g, --debug                   enable debugging\n\
  -G, --debugfile=FILENAME      output debugging activity to file\n\
  -j, --remove-checksum         remove data checksums when creating new cluster\n\
  -J, --add-checksum            add data checksumming to the new cluster\n\
  -k, --link                    link instead of copying files to new cluster\n\
  -l, --logfile=FILENAME        log session activity to file\n\
  -p, --old-port=OLDPORT        old cluster port number (default %d)\n\
  -P, --new-port=NEWPORT        new cluster port number (default %d)\n\
  -u, --user=NAME               clusters superuser (default \"%s\")\n\
  -v, --verbose                 enable verbose output\n\
  -X, --progress                enable progress reporting\n\
  -V, --version                 display version information, then exit\n\
  -h, --help                    show this help, then exit\n\
\n\
Before running pg_upgrade you must:\n\
  create a new database cluster (using the new version of initdb)\n\
  shutdown the postmaster servicing the old cluster\n\
  shutdown the postmaster servicing the new cluster\n\
\n\
When you run pg_upgrade, you must provide the following information:\n\
  the data directory for the old cluster  (-d OLDDATADIR)\n\
  the data directory for the new cluster  (-D NEWDATADIR)\n\
  the \"bin\" directory for the old version (-b OLDBINDIR)\n\
  the \"bin\" directory for the new version (-B NEWBINDIR)\n\
\n\
For example:\n\
  pg_upgrade -d oldCluster/data -D newCluster/data -b oldCluster/bin -B newCluster/bin\n\
or\n"), old_cluster.port, new_cluster.port, os_info.user);
#ifndef WIN32
	printf(_("\
  $ export OLDDATADIR=oldCluster/data\n\
  $ export NEWDATADIR=newCluster/data\n\
  $ export OLDBINDIR=oldCluster/bin\n\
  $ export NEWBINDIR=newCluster/bin\n\
  $ pg_upgrade\n"));
#else
	printf(_("\
  C:\\> set OLDDATADIR=oldCluster/data\n\
  C:\\> set NEWDATADIR=newCluster/data\n\
  C:\\> set OLDBINDIR=oldCluster/bin\n\
  C:\\> set NEWBINDIR=newCluster/bin\n\
  C:\\> pg_upgrade\n"));
#endif
	printf(_("\nReport bugs to <bugs@greenplum.org>.\n"));
}


/*
 * validateDirectoryOption()
 *
 * Validates a directory option.
 *	dirpath		  - the directory name supplied on the command line
 *	envVarName	  - the name of an environment variable to get if dirpath is NULL
 *	cmdLineOption - the command line option corresponds to this directory (-o, -O, -n, -N)
 *	description   - a description of this directory option
 *
 * We use the last two arguments to construct a meaningful error message if the
 * user hasn't provided the required directory name.
 */
static void
validateDirectoryOption(char **dirpath,
					char *envVarName, char *cmdLineOption, char *description)
{
	if (*dirpath == NULL || (strlen(*dirpath) == 0))
	{
		const char *envVar;

		if ((envVar = getenv(envVarName)) && strlen(envVar))
			*dirpath = pg_strdup(envVar);
		else
		{
			pg_log(PG_FATAL, "You must identify the directory where the %s\n"
				   "Please use the %s command-line option or the %s environment variable\n",
				   description, cmdLineOption, envVarName);
		}
	}

	/*
	 * Trim off any trailing path separators
	 */
#ifndef WIN32
	if ((*dirpath)[strlen(*dirpath) - 1] == '/')
#else
	if ((*dirpath)[strlen(*dirpath) - 1] == '/' ||
		(*dirpath)[strlen(*dirpath) - 1] == '\\')
#endif
		(*dirpath)[strlen(*dirpath) - 1] = 0;
}


static void
get_pkglibdirs(void)
{
	/*
	 * we do not need to know the libpath in the old cluster, and might not
	 * have a working pg_config to ask for it anyway.
	 */
	old_cluster.libpath = NULL;
	new_cluster.libpath = get_pkglibdir(new_cluster.bindir);
}


static char *
get_pkglibdir(const char *bindir)
{
	char		cmd[MAXPGPATH];
	char		bufin[MAX_STRING];
	FILE	   *output;
	int			i;

	snprintf(cmd, sizeof(cmd), "\"%s/pg_config\" --pkglibdir", bindir);

	if ((output = popen(cmd, "r")) == NULL)
		pg_log(PG_FATAL, "Could not get pkglibdir data: %s\n",
			   getErrorText(errno));

	fgets(bufin, sizeof(bufin), output);

	if (output)
		pclose(output);

	/* Remove trailing newline */
	i = strlen(bufin) - 1;

	if (bufin[i] == '\n')
		bufin[i] = '\0';

	return pg_strdup(bufin);
}
