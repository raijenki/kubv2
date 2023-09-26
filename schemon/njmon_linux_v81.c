/*
 * njmon.c -- collects Linux performance data and generates JSON format data
 *                      or InfluxDB Line Protocol format data.
 * Developer: Nigel Griffiths.
 * (C) Copyright 2018 Nigel Griffiths

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    Find the GNU General Public License here <http://www.gnu.org/licenses/>.
 */

/* Compile example: cc -O4 -g -o njmon njmon_linux.c */
#define VERSION "njmon4Linux-v81-" OSNAME "-" OSVERSION "-" HW "-" __FILE__ " (" __DATE__ ")"

#define NJMON 6
#define NIMON 42
#define NSMON 99 /* future splunk mode */

int mode = NJMON;

char *njmon_command;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <mntent.h>
#include <dirent.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/statfs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include <memory.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>

#define PRINT_FALSE 0
#define PRINT_TRUE 1

#define DEBUG   if(debug)
#define FLUSH	if(debug) fflush(NULL);

#define VERBOSE if(verbose)
#define FUNCTION_START DEBUG fprintf(stderr,"%s called line %d\n",__func__, __LINE__);

char ** btrfs_disks_list = 0;
char* buffer45[1024];
long btrfs_disks_count;

long loop;
char   filename_ff2[512];
int debug = 0;
int verbose = 0;
int elastic = 0;
int warnings_on = 1;
int file_output = 0;

/* These three variables are for InfluxDB 2.0 */
char influx_org[64+1] = "default";
char influx_token[256+1];
int influx_version = 1;

char saved_serial_no[9];
char saved_lpar_num_name[31];
char saved_machine_type[31];
char saved_uname_node[31];

char target_host[1024 + 1] = { 0 };
char target_ip[16 + 1] = { 0 };
long target_port = 0;

uid_t uid = (uid_t) 123456;

void nwarning(char *buf)
{
    if (warnings_on)
	fprintf(stderr, "WARNING: \"%s\" errno=%d\n", buf, errno);
}

void nwarning2( char *s1, char *s2)
{
char    msgbuf[1024];

        sprintf(msgbuf, s1, s2);
        nwarning(msgbuf);
}


int sockfd = 1;			/*default is stdout, only changed if we are using a remote socket */
char errorbuf[8 * 1024];

void error(char *buf)
{
    fprintf(stderr, "ERROR: \"%s\" errno=%d meaning=\"%s\"\n", buf, errno, strerror(errno));
    close(sockfd);
    sleep(2);			/* this can help the socket close cleanly at the remote end */
    exit(1);
}

char fullhostname[256] = { 0 };
int  fullhostname_tag = 0;
char hostname[256] = { 0 };	/* this will be the shortened name */
char alias_hostname[1024] = { 0 };
char source_ip[64+1] = { 0 };	/* local IP address */

void interrupt(int signum)
{
    switch (signum) {
    case SIGUSR1:
    case SIGUSR2:
	fflush(NULL);
	close(sockfd);
	sleep(1);
	exit(0);
	break;
    }
}

int create_socket()
{				/* returns 1 for error and 0 for OK */
    static struct sockaddr_in serv_addr;

    if(debug || verbose) DEBUG fprintf(stderr, "socket: trying to connect to \"%s\":%ld\n",
		target_ip, target_port);
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
	error("njmon:socket() call failed");
	return 0;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(target_ip);
    serv_addr.sin_port = htons(target_port);

    /* Connect tot he socket offered by the web server */
    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
	DEBUG fprintf(stderr, "njmon: connect() call failed errno=%d", errno);
	return 0;
    }
    return 1;
}

/* collect stats on the metrix */
int njmon_internal_stats = 0;
int njmon_sections = 0;
int njmon_subsections = 0;
int njmon_string = 0;
int njmon_long = 0;
int njmon_double = 0;
int njmon_hex = 0;

/* Output JSON test buffering to ensure ist a single write and allow EOL comma removal */
#define INITIAL_BUFFER_SIZE (1024 * 1024)	/* 64 MB */

char *output;
long output_size = 0;
long output_char = 0;

void buffer_check()
{
    long size;
    if (output_char > (long) (output_size * 0.95)) {	/* within 5% of the end */
	size = output_size + (1024 * 1024);	/* add another MB */
	output = realloc((void *) output, size);
	output_size = size;
    }
}

void remove_ending_comma_if_any()
{
    if (output[output_char - 1] == ',') {
	output[output_char - 1] = 0;	/* remove the char */
	output_char--;
    }

}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/*
*    NIMON mode p functions to generate InfluxDB Line Protocol output
*        the Line Protocol is appended to the buffer "output" so
*    NJMON mode p functions to generate JSON output
*        the JSON is appended to the buffer "output" so
*            we can remove the trailing "," before we close the entry with a "}"

*    psection(name) and psectionend()
*    Adds
*	   "name": {
*
*	   }
*
*    psub(name) and psubend()
*           similar to psection/psectionend but one level deeper
*           example
*               "disks": { 
*                   {"name": "hdisk0", "size": 400, "reads": 123.2, ...... }
*               }
*
*
*    pstring(name,"abc")
*    plong(name, 1234) 
*    pdouble(name, 1234.546)
*    phex(name, hedadecimal number)
*    praw(name) for other stuff in a raw format
*       add "name": data,
*
*    the JSON is appended to the buffer "output" so
*	we can remove the trailing "," before we close the entry with a "}"
*	we can write the whole record in a single write (push()) to help down stream tools
*/

int psubended = 0;		/* stop psubend and psectionend both enig the measure */
int first_sub = 0;		/* need to remove the psection measure before adding psub measure */

/* the Influx login details */
char influx_database[64];
char influx_username[64];
char influx_password[64];

char *saved_arch;

int telegraf_mode = 0;

char saved_section[1024];
char saved_sub[1024];

int ispower = 0;		/* from lscpu cmd */
int isamd64 = 0;		/* from lscpu cmd */
int isarm = 0;			/* from lscpu cmd */
int ispowervm = 0;		/* from lparcfg file */

char saved_system_type[256 + 1];	/* from lparcfg */
char saved_serial_number[256 + 1];	/* from lparcfg */

char saved_dev_model[256 + 1];		/* from /proc/devive-tree */
char saved_dev_system_id[256 + 1];	/* from /proc/devive-tree */
char saved_dmi_serial_number[256 + 1];	/* from /sys/.../dmi */
char saved_dmi_model[256 + 1];		/* from /sys/.../dmi */

char saved_os[256] = "unknown";

char tag_hostname[256 + 1];
char tag_arch[256 + 1] = "unknown";
char tag_mtm[256 + 1]  = "unknown";
char tag_os[256 + 1]   = "unknown";
char tag_sn[256 + 1]   = "unknown";

void tag_set(char *target, char *source)
{
    int i;
    int j;
    char temp[256 + 1];

    if (!strncmp("IBM,", source, 4))	/* POWER lslparcfg */
	strncpy(temp, &source[4], 256);
    else
	strncpy(temp, &source[0], 256);
    for (i = strlen(temp) - 1; i > 0; i--) {	/* remove trailing scraps */
	if (!isalnum(temp[i]))
	    temp[i] = 0;
	else
	    break;
    }
    for (i = 0; i < strlen(temp); i++) {	/* remove clutter and bad tag chars */
	if (isalnum(temp[i]))
	    continue;
	if (temp[i] == '.')
	    continue;
	if (temp[i] == '-')
	    continue;
	temp[i] = '-';
    }
    for (i = 0, j = 0; i < strlen(temp); i++) {
	if (temp[i] == '-' && temp[i + 1] == '-')
	    continue;
	target[j++] = temp[i];
    }
    target[j] = 0;
}

#ifdef TAG_TEST
void print_tags()
{
    printf("power=%d AMD=%d Arm=%d PowerVM=%d\n", ispower, isamd64, isarm,
	   ispowervm);

    printf("lparcfg SystemType=[%s]\n", saved_system_type);
    printf("lparcfg system_id =[%s]\n", saved_serial_number);

    printf("Devtree Model=[%s]\n", saved_dev_model);
    printf("Devtree Sysid=[%s]\n", saved_dev_system_id);

    printf("DMI serial_number =[%s]\n", saved_dmi_serial_number);
    printf("DMI model	 =[%s]\n", saved_dmi_model);
    printf("os	=[%s]\n", saved_os);

    printf("tag_hostname=[%s]\n", tag_hostname);
    printf("tag_arch    =[%s]\n", tag_arch);
    printf("tag_os      =[%s]\n", tag_os);
    printf("tag_mtm     =[%s]\n", tag_mtm);
    printf("tag_sn      =[%s]\n", tag_sn);
}
#endif				/* TAG_TEST */

void file_read_one_stat_save(char *file, char *name, char *output)
{
    FILE *fp;
    static char buf[1024 + 1];

    buf[0] = 0;
    if ((fp = fopen(file, "r")) != NULL) {
	if (fgets(buf, 1024, fp) != NULL) {
	    if (buf[strlen(buf) - 1] == '\n')	/* remove last char = newline */
		buf[strlen(buf) - 1] = 0;
	}
	strncpy(output, buf, 256);
	fclose(fp);
    }
}
char additional_tags[256];

void save_tags()
{
    FILE *fp = 0;
    int data_col = -1;
    int len = 0;
    char buf[1024 + 1];
    char label[1024];
    char string[1024];
    int i;

    if ((fp = popen("/usr/bin/lscpu", "r")) != NULL) {

	buf[0] = 0;
	while (fgets(buf, 1024, fp) != NULL) {
	    buf[strlen(buf) - 1] = 0;	/* remove newline */
	    len = strlen(buf);
	    if(data_col == -1) { /* work out the first non space character */
	      for (data_col = strlen("Architecture: "); data_col < len; data_col++) {
		if (isalnum(buf[data_col]))
		    break;
	      }
	    }
	    /*            01234567890123 */
	    if (!strncmp("Architecture:", buf, strlen("Architecture:"))) {
		if (!strncmp("ppc64", &buf[data_col], strlen("ppc64")))
		    ispower++;
		if (!strncmp("x86_64", &buf[data_col], strlen("x86_64")))
		    isamd64++;
		if (!strncmp("arm", &buf[data_col], strlen("arm")))
		    isarm++;
	        DEBUG fprintf(stderr, "Arch=%s ispower=%d isamd64=%d isarm=%d\n",&buf[data_col],ispower,isamd64, isarm);
		break;
	    }
	}
	pclose(fp);
	tag_set(tag_arch, &buf[data_col]);
    }

    len = 0;
    if (ispower) {
	if ((fp = fopen("/proc/ppc64/lparcfg", "r")) == NULL) {
	    ispowervm = 0;
            if (access("/proc/device-tree", R_OK) == 0) {
                file_read_one_stat_save("/proc/device-tree/model", "model",
                                    saved_dev_model);
                file_read_one_stat_save("/proc/device-tree/system-id",
                                    "system-id", saved_dev_system_id);
                tag_set(tag_mtm, saved_dev_model);
                    strncpy(saved_serial_number, string, 256);
                    switch(strlen(saved_serial_number)) {
                    case 13: i = 6; break;
                    case  9: i = 2; break;
                    default: i = 0; break;
                    }
                tag_set(tag_sn, &saved_dev_system_id[i]);
    	    }
	} else {
	    ispowervm = 1;
	    while (fgets(buf, 1000, fp) != NULL) {
		for (i = 0; i < strlen(buf); i++) {
		    if (buf[i] == '=')
			buf[i] = ' ';
		}
		if (!strncmp(buf, "system_type", strlen("system_type"))) {
		    sscanf(buf, "%s %s", label, string);
		    strncpy(saved_system_type, string, 256);
		    tag_set(tag_mtm, saved_system_type);
		}
		if (!strncmp (buf, "serial_number", strlen("serial_number"))) {
		    sscanf(buf, "%s %s", label, string);
		    strncpy(saved_serial_number, string, 256);
		    switch(strlen(saved_serial_number)) {
		    case 13: i = 6; break;
		    case  9: i = 2; break;
		    default: i = 0; break;
		    }
			/* Skip the first 2 digits and any "IBM," to match AIX serial numbers */
		    tag_set(tag_sn, &saved_serial_number[i]); 
		}
	    }
	    fclose(fp);
	}
    }

    if (!ispower) {
	/* x86_64 and AMD64 and Arm - dmi files requires root user */
	if (access("/sys/devices/virtual/dmi/id/", R_OK) == 0) {
	    file_read_one_stat_save
		("/sys/devices/virtual/dmi/id/product_serial",
		 "serial-number", saved_dmi_serial_number);
	    file_read_one_stat_save
		("/sys/devices/virtual/dmi/id/product_name", "model",
		 saved_dmi_model);
	    tag_set(tag_mtm, saved_dmi_model);
	    if (strlen(saved_dmi_serial_number) != 0)
		tag_set(tag_sn, saved_dmi_serial_number);
	} 
    }
    if ((fp = fopen("/etc/os-release", "r")) != NULL) {
	while (fgets(buf, 256, fp) != NULL) {
	    buf[strlen(buf) - 1] = 0;	/* remove end newline */
	    for (i = 0; i < strlen(buf); i++)
		if (buf[i] == '"' || buf[i] == '\'')
		    buf[i] = ' ';	/* replace with spaces all double and single quotes */

	    if (!strncmp(buf, "NAME=", strlen("NAME="))) {
		strncpy(saved_os, &buf[strlen("NAME=") + 1], 255);
		if (!strncmp
		    (saved_os, "Red Hat Enterprise",
		     strlen("Red Hat Enterprise")))
		    strcpy(saved_os, "RHEL");
		if (!strncmp
		    (saved_os, "SUSE Linux Enterprise",
		     strlen("SUSE Linux Enterprise")))
		    strcpy(saved_os, "SLES");
		for (i = 0; i < strlen(saved_os); i++) {
		    if (saved_os[i] == ' ') {
			saved_os[i] = 0;
			break;
		    }
		}
	    }
	}
	fclose(fp);
    }
    tag_set(tag_os, saved_os);
#ifdef MAINFRAME
    {
        char mf_type[1024] = "X";
        char mf_model1[1024] = "Y";
        char mf_model2[1024] = "Z";

        if ((fp = fopen("/proc/sysinfo", "r")) != NULL) {
            while (fgets(buf, 1000, fp) != NULL) {
                if (!strncmp(buf, "Type", strlen("Type"))) {
                    sscanf(buf, "%s %s", label, mf_type);
                }
                if (!strncmp (buf, "Model", strlen("Model"))) {
                    sscanf(buf, "%s %s %s", label, mf_model1, mf_model2);
                }
                if(strlen(mf_type) > 1 && strlen(mf_model1) > 1)
                    break;
            }
            fclose(fp);
            DEBUG fprintf(stderr,"type=%s model=%s and %s\n",mf_type, mf_model1, mf_model2);
            sprintf(string, "%s-%s-%s", mf_type, mf_model1, mf_model2);
        }
	tag_set(tag_mtm, string);
    }
    {
	char label[1024];
	char serialno[1024];
	char serialno_done = 0;
	if ((fp = fopen("/proc/cpuinfo", "r")) != NULL) {
    	    while (fgets(buf, 1024, fp) != NULL) {
	    buf[strlen(buf) - 1] = 0;	/* remove newline */
              if (!strncmp("processor ", buf, strlen("processor "))) {
                sscanf(buf, "%s %s %s %s %s %s %s %s", label, label, label, label, label, label, label, serialno);
	        if(serialno_done == 0) {
      	            for(i=0;i<strlen(serialno);i++)
	    	        if(serialno[i] == ',') 
		            serialno[i] = 0;
		    serialno_done++;
		    tag_set(tag_sn,serialno);
	        }
              }
            }
        }
    }
#endif /* MAINFRAME */
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

void push()
{
    char result[1024];
    char buffer[1024 * 8];
    int ret;
    int i;
    long total;
    int sent;
    int code;
    char filename[1024];
    int outfile;

    FUNCTION_START;
    buffer_check();
    if (output_char == 0)	/* noting to send so skip this operation */
	return;
    if(mode == NJMON) {
	    if (target_port) {
		DEBUG fprintf(stderr, "push() NJMON socket mode size=%ld\n", output_char);
		if (create_socket() == 1) {
		    if (write(sockfd, output, output_char) < 0) {
			/* if socket failed there is not much we can do but carry on */
			nwarning("njmon write to stdout failed, stopping now.");
		    }
		    fflush(NULL);	/* force I/O output now */
		    if (target_port)
			close(sockfd);
		    DEBUG fprintf(stderr, "push complete\n");
		    VERBOSE fprintf(stderr, "push complete\n");
		} else {
			if( debug || verbose) 
				fprintf(stderr, "socket create failed\n");
		}
	    } else {
		DEBUG fprintf(stderr, "push() NJMON file mode size=%ld\n", output_char);
                if(file_output >= 2) { /* -ff mode open file in the series */
		    sprintf( filename, "%s_%06ld.json", filename_ff2, loop);
		    /*if( (outfile = open(filename,  O_WRONLY | O_CREAT)) == -1 ) {*/
		    if( (outfile = open(filename,  O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1 ) { 
			nwarning2("opening file filename=%s\n", filename);
			exit(33333);
		    }
		    if ( write(outfile, output, output_char) < 0) {
			/* if stdout failed there is not much we can do hopefully more disk space next time */
			nwarning("njmon write to stdout failed, stopping now.");
		    }
		    close(outfile);
		} else {
		    if ( write(sockfd, output, output_char) < 0) {
		        /* if stdout failed there is not much we can do hopefully more disk space next time */
			nwarning("njmon write to stdout failed, stopping now.");
		    }
		}	
#ifdef JUNK
		if (write(sockfd, output, output_char) < 0) {
		    /* if stdout failed there is not much we can do hopefully more disk space next time */
		    nwarning("njmon write to stdout failed, stopping now.");
		}
#endif
		fflush(NULL);		/* force I/O output now */
	    }
    } else { /* NIMON */
	    if (target_port) {
		VERBOSE fprintf(stderr, "push() NIMON socket influx_version=%d Data size=%ld\n", influx_version, output_char);
		if (create_socket() == 1) {

		    if(!telegraf_mode) {
			if(influx_version == 1) {
			    sprintf(buffer, "POST /write?db=%s&u=%s&p=%s HTTP/1.1\r\nHost: %s:%ld\r\nContent-Length: %ld\r\n\r\n",
			        influx_database, influx_username, influx_password, target_host, target_port, output_char);
			} else { /* InfluxDB = 2 */
			    sprintf(buffer, "POST /api/v2/write?bucket=%s&org=%s&precision=s HTTP/1.1\r\nHost: %s:%ld\r\nAuthorization: Token %s\r\nContent-Type: text/plain; charset=utf-8\r\nAccept: application/json\r\nContent-Length: %ld\r\n\r\n",
				influx_database, influx_org, target_host, target_port, influx_token, output_char);
			}
			VERBOSE fprintf(stderr, "InfluxDB Header buffer size=%ld buffer=\n==========\n<%s>\n==========\n", strlen(buffer), buffer);
			if ((ret = write(sockfd, buffer, strlen(buffer))) != strlen(buffer)) {
				nwarning("njmon write POST to sockfd failed.");
			}
		    }
		    total = output_char;
		    sent = 0;
		    if (verbose == 2)
			fprintf(stderr, "output size=%ld output=\n<%s>\n", total, output);
		    while (sent < total) {
			ret = write(sockfd, &output[sent], total - sent);
			VERBOSE fprintf(stderr, "Written=%d bytes sent=%d total=%ld\n", ret, sent, total);
			if (ret < 0) {
			    nwarning("njmon write body to sockfd failed.");
			    break;
			}
			sent = sent + ret;
		    }
		    for (i = 0; i < 1024; i++)
			result[i] = 0;
		    if ((ret = read(sockfd, result, sizeof(result))) > 0) {
			result[ret] = 0;
			VERBOSE fprintf(stderr, "received bytes=%d data=<%s>\n", ret, result);

			sscanf(result, "HTTP/1.1 %d", &code);
			for (i = 13; i < 1024; i++)
			    if (result[i] == '\r')
				result[i] = 0;
			if (verbose == 2)
			    fprintf(stderr, "HTTP Code=%d Text=%s\n", code,
				    &result[13]);
			if (code != 204)
			    fprintf(stderr, "Code %d -->%s<--\n", code, result);
		    }
		    close(sockfd);
		    sockfd = 0;
		    VERBOSE fprintf(stderr, "push complete\n");
		} else {
		    VERBOSE fprintf(stderr, "socket create failed\n");
		}
	    } else {
		VERBOSE fprintf(stderr, "push() NIMON socket size=%ld\n", output_char);
		/* save to local file */
		if (write(1, output, output_char) < 0) {
		    /* if stderr failed there is not much we can do hopefully more disk space next time */
		    VERBOSE fprintf(stderr, "file output failed\n");
		    nwarning("njmon write to stderr failed, stopping now.");
		}
		fflush(NULL);		/* force I/O output now */

	    }
    }
    output[0] = 0;
    output_char = 0;
}

void replace_curly_with_square()
{
    if (output[output_char - 1] == '{') {
	output[output_char - 1] = '[';
    }
}

void praw(char *string)
{
    output_char += sprintf(&output[output_char], "%s", string);
}

void psample()
{
    DEBUG fprintf(stderr, "---- psample()\n) count=%ld\n", output_char);
    if(mode == NJMON)
	praw("{");			/* start of sample */
}

void psampleend()
{
    DEBUG fprintf(stderr, "---- psampleend()\n) count=%ld\n", output_char);
    remove_ending_comma_if_any();
    if(mode == NJMON)
	praw("}\n");
}

void psection(char *section)
{
    char *h;

    DEBUG fprintf(stderr, "++++ psection(%s) count=%ld\n", section, output_char);

    buffer_check();
    njmon_sections++;

    if(mode == NJMON) {
	    output_char += sprintf(&output[output_char], "\"%s\": {", section);
    } else {
            if(alias_hostname[0] != 0 ) {
		h = alias_hostname;
            } else {
                if (fullhostname_tag)
                    h = fullhostname;
                else
                    h = hostname;
	    }
	    tag_set(tag_hostname, h);

	    output_char += sprintf(&output[output_char],
			"%s,host=%s,os=%s,architecture=%s,serial_no=%s,mtm=%s%s ",
			section, h, tag_os, tag_arch, tag_sn, tag_mtm, additional_tags);
	    strcpy(saved_section, section);
	    first_sub = 1;
	    psubended = 0;
    }
}

int sub_array = 0;

void psub(char *resource)
{
    int i;
    char *h;

    buffer_check();
    DEBUG fprintf(stderr, ">> psubend(%s) count=%ld\n", resource, output_char); 
    njmon_subsections++;
    if(mode == NJMON) {
	    if (elastic) {
		replace_curly_with_square();
		output_char +=
		    sprintf(&output[output_char], "{ \"item\": \"%s\",", resource);
		sub_array = 1;
	    } else {
		output_char +=
		    sprintf(&output[output_char], "\"%s\": {", resource);
	    }
    } else {
            if(alias_hostname[0] != 0 ) {
		h = alias_hostname;
	    } else {
                if (fullhostname_tag)
                    h = fullhostname;
                else
                    h = hostname;
            }

	    /* remove previously added section */
	    if (first_sub) {
		for (i = output_char - 1; i > 0; i--) {
		    if (output[i] == '\n') {
			output[i + 1] = 0;
			output_char = i + 1;
			break;
		    }
		}
	    }
	    first_sub = 0;

	    /* remove the trailing es and s */
	    strcpy(saved_sub, saved_section);
	    if(!strcmp("processes", saved_sub)) {
		strcpy(saved_sub,"process");
	    } else {
		if (saved_sub[strlen(saved_sub) - 1] == 's') {
		    saved_sub[strlen(saved_sub) - 1] = 0;
		}
	    }

	    output_char += sprintf(&output[output_char],
			"%s,host=%s,os=%s,architecture=%s,serial_no=%s,mtm=%s,%s_name=%s%s ",
			saved_section, h, tag_os, tag_arch, tag_sn, tag_mtm, saved_sub, resource, additional_tags);
	    psubended = 0;

    }
}

void psubend()
{
    DEBUG fprintf(stderr, "<< psubend() count=%ld\n", output_char);
    remove_ending_comma_if_any();
    if(mode == NJMON) {
	    praw("},");
    } else {
	    if(file_output >= 2)
		 output_char += sprintf(&output[output_char], " %ld000000000\n", (long)time(0));
	    else
		 output_char += sprintf(&output[output_char], "   \n");
	    psubended = 1;
    }
}


void psectionend()
{
    remove_ending_comma_if_any();
    DEBUG fprintf(stderr, "---- psectiondend() count=%ld\n", output_char);
    if(mode == NJMON) {
	    if (sub_array)
		praw("],");
	    else
		praw("},");
	    sub_array = 0;
    } else {
	    if (!psubended) {
		if(file_output >= 2)
		    output_char += sprintf(&output[output_char], " %ld000000000\n", (long)time(0));
		else
		    output_char += sprintf(&output[output_char], "   \n");
	    }
	    psubended = 0;
    }
}

void phex(char *name, long long value)
{
    njmon_hex++;
    if( mode == NJMON) {
	    output_char += sprintf(&output[output_char], "\"%s\": \"0x%08llx\",", name, value);
    } else {
	    output_char += sprintf(&output[output_char], "%s=\"0x%08llx\",", name, value);
    }
    DEBUG fprintf(stderr, "phex(%s,0x%08llx) count=%ld\n", name, value, output_char);
}

void plong(char *name, long long value)
{
    njmon_long++;
    if(mode == NJMON) {
	    output_char += sprintf(&output[output_char], "\"%s\": %lld,", name, value);
    } else {
	    output_char += sprintf(&output[output_char], "%s=%lldi,", name, value);
    }
    DEBUG fprintf(stderr, "plong(%s,%lld) count=%ld\n", name, value, output_char);
}

void pdouble(char *name, double value)
{
    njmon_double++;
    if (isnan(value) || isinf(value)) { /* Not-a-number or infinity */
	DEBUG fprintf(stderr, "pdouble(%s,%.1f) - NaN error\n", name, value);
	return;
    } 
    if(mode == NJMON) {
	output_char += sprintf(&output[output_char], "\"%s\": %.3f,", name, value);
	DEBUG fprintf(stderr, "pdouble(%s,%.1f) count=%ld\n", name, value, output_char);
    } else {
	output_char += sprintf(&output[output_char], "%s=%.3f,", name, value);
	DEBUG fprintf(stderr, "pdouble(%s,%.1f) count=%ld\n", name, value, output_char);
    }
}

void pstring(char *name, char *value)
{
    int i;
    int len;

    buffer_check();
    njmon_string++;
    if (value == (char *) 0)
	value = "(null)";
    len = strlen(value);
    for (i = 0; i < len; i++) {
	if (value[i] == '\n' || iscntrl(value[i]))
	    value[i] = '?';
    }
    if(mode == NJMON) {
	    output_char += sprintf(&output[output_char], "\"%s\": \"%s\",", name, value);
    } else {
	    output_char += sprintf(&output[output_char], "%s=\"%s\",", name, value);
    }
    DEBUG fprintf(stderr, "pstring(%s,%s) count=%ld\n", name, value, output_char);
}
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef EXTRA

#include "extra.c"

#endif /* EXTRA */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

void pstats()
{
    psection("njmon_internal_stats");
    plong("section", njmon_sections);
    plong("subsections", njmon_subsections);
    plong("string", njmon_string);
    plong("long", njmon_long);
    plong("double", njmon_double);
    plong("hex", njmon_hex);
    psectionend("njmon_internal_stats");
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int has_dots(char *name)
{
    int i;
    int len;
    int ret = 0;

        len = strlen(name);
        for (i = 0; i < len; i++) {
            if (name[i] == '.') {
                ret++;
            }
        }
        return ret;
}

void    get_hostname()
{
    static int    set = 0;
    int    i;
    int    len;
    struct hostent *he;

    FILE * pop;
    char   string[1024];
    char   hostn_command[1024];
    char   *ip;

    FUNCTION_START;
    if (set == 1)
        return;
    set = 1;

    strcpy(hostname,     "not-found");
    strcpy(fullhostname, "not.found");
    strcpy(source_ip,    "unknown");

    if ( gethostname(hostname, sizeof(hostname)) == 0) {
        strcpy(fullhostname, hostname);
        len = strlen(hostname);
        for (i = 0; i < len; i++) {  /* shorten the short name */
            if (hostname[i] == '.') {
                hostname[i] = 0;
                break;
            }
        }
	if((he = gethostbyname(hostname)) != NULL) {
            DEBUG printf("gethostbyhostname \"%s\"\n", he->h_name);
            if((ip = inet_ntoa(*((struct in_addr*) he->h_addr_list[0]))) != NULL)
                strncpy(source_ip,ip,64);
        }

        DEBUG fprintf(stderr, "    hostname is \"%s\"\n", hostname);
        DEBUG fprintf(stderr, "fullhostname is \"%s\"\n", fullhostname);
        DEBUG fprintf(stderr, "source_ip is    \"%s\"\n", source_ip);
        if(has_dots(fullhostname) > 0) /* We did got a FQDN */
            return;
    }
    if((he = gethostbyname(hostname)) != NULL) {
        DEBUG fprintf(stderr, "gethostbyhostname \"%s\"\n", he->h_name);
	if((ip = inet_ntoa(*((struct in_addr*) he->h_addr_list[0]))) != NULL)
            strncpy(source_ip,ip,64);

        if(has_dots(he->h_name) > 0) { /* We did got a FQDN */
            strcpy(fullhostname,he->h_name);
            return;
        }
    }

    sprintf(hostn_command, "host -n %s 2>/dev/null", hostname);
    if ( (pop = popen(hostn_command, "r") ) != NULL ) {
        if ( fgets(string, 1023, pop) != NULL) {
            len = strlen(string);
            for(i=0;i<len;i++) {
                if(string[i] == ' ') {
                    string[i] = 0;
                    break;
                }
            }
            DEBUG fprintf(stderr, "\"%s\" returned first word of \"%s\"\n", hostn_command, string);
            if(has_dots(string))
                strcpy(fullhostname,string);
        }
        pclose(pop);
    }
}


time_t timer;			/* used to work out the time details */
struct tm *tim;			/* used to work out the local hour/min/second */

void get_time()
{

    timer = time(0);
}

void get_localtime()
{
    tim = localtime(&timer);
    tim->tm_year += 1900;	/* read localtime() manual page! */
    tim->tm_mon += 1;		/* because it is 0 to 11 */
}

void get_utc()
{
    tim = gmtime(&timer);
    tim->tm_year += 1900;	/* read gmtime() manual page! */
    tim->tm_mon += 1;		/* because it is 0 to 11 */
}

void date_time(long seconds, long loop, long maxloops,double sleeping, double sleep_overrun, double execute_time, double elapsed)
{
    char buffer[256];

    FUNCTION_START;
    /* This is ISO 8601 datatime string format - ughly but get over it! :-) */
    get_time();
    get_localtime();
    psection("timestamp");
    sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d",
	    tim->tm_year,
	    tim->tm_mon,
	    tim->tm_mday, tim->tm_hour, tim->tm_min, tim->tm_sec);
    pstring("datetime", buffer);
    get_utc();
    sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d",
	    tim->tm_year,
	    tim->tm_mon,
	    tim->tm_mday, tim->tm_hour, tim->tm_min, tim->tm_sec);
    pstring("UTC", buffer);
    plong("snapshot_seconds", seconds);
    plong("snapshot_maxloops", maxloops);
    plong("snapshot_loop", loop);
    pdouble("sleeping", sleeping);
    pdouble("execute_time",  execute_time);
    pdouble("sleep_overrun", sleep_overrun);
    pdouble("elapsed", elapsed);

    psectionend();
}

/* - - - - - NFS - - - - */

#define NFS_V2_NAMES_COUNT 18
char *nfs_v2_names[NFS_V2_NAMES_COUNT] = {
    "null", "getattr", "setattr", "root", "lookup", "readlink",
    "read", "wrcache", "write", "create", "remove", "rename",
    "link", "symlink", "mkdir", "rmdir", "readdir", "fsstat"
};

#define NFS_V3_NAMES_COUNT 22
char *nfs_v3_names[22] = {
    "null", "getattr", "setattr", "lookup", "access", "readlink",
    "read", "write", "create", "mkdir", "symlink", "mknod",
    "remove", "rmdir", "rename", "link", "readdir", "readdirplus",
    "fsstat", "fsinfo", "pathconf", "commit"
};

#define NFS_V4S_NAMES_COUNT 59
char *nfs_v4s_names[NFS_V4S_NAMES_COUNT] = {	/* get these names from nfsstat as they are NOT documented */
    "op0-unused", "op1-unused", "op2-future", "access", "close", "commit",	/* 1 - 6 */
    "create", "delegpurge", "delegreturn", "getattr", "getfh", "link",	/* 7 - 12 */
    "lock", "lockt", "locku", "lookup", "lookup_root", "nverify",	/* 13 - 18 */
    "open", "openattr", "open_conf", "open_dgrd", "putfh", "putpubfh",	/* 19 - 24 */
    "putrootfh", "read", "readdir", "readlink", "remove", "rename",	/* 25 - 30 */
    "renew", "restorefh", "savefh", "secinfo", "setattr", "setcltid",	/* 31 - 36 */
    "setcltidconf", "verify", "write", "rellockowner", "bc_ctl", "blind_conn",	/* 37 - 42 */
    "exchange_id", "create_ses", "destroy_ses", "free_statid", "getdirdelag", "getdevinfo",	/* 43 - 48 */
    "getdevlist", "layoutcommit", "layoutget", "layoutreturn", "secunfononam", "sequence",	/* 49 - 54 */
    "set_ssv", "test_stateid", "want_deleg", "destory_clid", "reclaim_comp"	/* 55 - 59 */
};

#define NFS_V4C_NAMES_COUNT 48
char *nfs_v4c_names[NFS_V4C_NAMES_COUNT] = {	/* get these names from nfsstat as they are NOT documented */
    "null", "read", "write", "commit", "open", "open_conf",	/* 1 - 6 */
    "open_noat", "open_dgrd", "close", "setattr", "fsinfo", "renew",	/* 7 - 12 */
    "setclntid", "confirm", "lock", "lockt", "locku", "access",	/* 13 - 18 */
    "getattr", "lookup", "lookup_root", "remove", "rename", "link",	/* 19 - 24 */
    "symlink", "create", "pathconf", "statfs", "readlink", "readdir",	/* 25 - 30 */
    "server_caps", "delegreturn", "getacl", "setacl", "fs_locations", "rel_lkowner",	/* 31 - 36 */
    "secinfo", "exchange_id", "create_ses", "destroy_ses", "sequence", "get_lease_t",	/* 37 - 42 */
    "reclaim_comp", "layoutget", "getdevinfo", "layoutcommit", "layoutreturn", "getdevlist"	/* 43 - 48 */
};

/* NFS data structures */
struct nfs_stat {
    long long v2c[NFS_V2_NAMES_COUNT];	/* version 2 client */
    long long v3c[NFS_V3_NAMES_COUNT];	/* version 3 client */
    long long v4c[NFS_V4C_NAMES_COUNT];	/* version 4 client */

    long long v2s[NFS_V2_NAMES_COUNT];	/* version 2 SERVER */
    long long v3s[NFS_V3_NAMES_COUNT];	/* version 3 SERVER */
    long long v4s[NFS_V4S_NAMES_COUNT];	/* version 4 SERVER */
} nfsa, nfsb;

/* pointers to the above */
struct nfs_stat *nfsp = &nfsa;
struct nfs_stat *nfsq = &nfsb;

/* files with the NFS data */
char *nfs_filename = "/proc/net/rpc/nfs";
char *nfsd_filename = "/proc/net/rpc/nfsd";

/* open flie pointers */
FILE *nfs_fp = NULL;
FILE *nfsd_fp = NULL;

void nfs_getdata()
{
    int i;
    int j;
    int len;
    int lineno;
    char buffer[4096];
    struct nfs_stat *temp;
    int ret;

    /* swap pointers */
    temp = nfsp;
    nfsp = nfsq;
    nfsq = temp;

    /* sample /proc/net/rpc/nfs
       net 0 0 0 0
       rpc 70137 0 0
       proc2 18 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
       proc3 22 0 27364 0 32 828 22 40668 0 1 0 0 0 0 0 0 0 0 1212 6 2 1 0
       proc4 2 5 196
     */
    if ((nfs_fp = fopen(nfs_filename, "r")) != NULL) {
	for (lineno = 0; fgets(buffer, 4095, nfs_fp) != NULL; lineno++) {
	    buffer[strlen(buffer) - 1] = 0;	/* ditch end of line  newline */
	    DEBUG fprintf(stderr, "get data client line=%d \"%s\"\n", lineno, buffer);

	    if (!strncmp("proc2 ", buffer, 6)) {
		/* client version 2 line readers "proc2 18 num num etc" */
		len = strlen(buffer);
		for (j = 0, i = 8; i < len && j < NFS_V2_NAMES_COUNT; i++) {
		    if (buffer[i] == ' ') {
			nfsp->v2c[j] = 0;
			nfsp->v2c[j] = atoll(&buffer[i + 1]);
			j++;
		    }
		}
	    }
	    if (!strncmp("proc3 ", buffer, 6)) {
		/* client version 3 line readers "proc3 22 num num etc" */
		len = strlen(buffer);
		for (j = 0, i = 8; i < len && j < NFS_V3_NAMES_COUNT; i++) {
		    if (buffer[i] == ' ') {
			nfsp->v3c[j] = 0;
			nfsp->v3c[j] = atoll(&buffer[i + 1]);
			j++;
		    }
		}
	    }
	    if (!strncmp("proc4 ", buffer, 6)) {
		/* client version 4 line readers "proc4 35 num num etc" */
		len = strlen(buffer);
		for (j = 0, i = 8; i < len && j < NFS_V4C_NAMES_COUNT; i++) {
		    if (buffer[i] == ' ') {
			nfsp->v4c[j] = 0;
			nfsp->v4c[j] = atoll(&buffer[i + 1]);
			j++;
		    }
		}
	    }
	}
	ret = fclose(nfs_fp);
	if (ret != 0)
	    DEBUG fprintf(stderr, "fclose(nfs_fp)=%d errno=%d\n", ret, errno);

    } else {			/* zero all the client counters */
	for (j = 0; j < NFS_V2_NAMES_COUNT; j++) {
	    nfsp->v2c[j] = 0;
	    nfsq->v2c[j] = 0;
	}
	for (j = 0; j < NFS_V3_NAMES_COUNT; j++) {
	    nfsp->v3c[j] = 0;
	    nfsq->v3c[j] = 0;
	}
	for (j = 0; j < NFS_V4C_NAMES_COUNT; j++) {
	    nfsp->v4c[j] = 0;
	    nfsq->v4c[j] = 0;
	}

    }
    /* sample /proc/net/rpc/nfsd 
       rc 0 0 0
       fh 0 0 0 0 0
       io 0 0
       th 4 0 0.000 0.000 0.000 0.000 0.000 0.000 0.000 0.000 0.000 0.000
       ra 32 0 0 0 0 0 0 0 0 0 0 0
       net 0 0 0 0
       rpc 0 0 0 0 0
       proc2 18 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
       proc3 22 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
       proc4 2 0 0
       proc4ops 72 0 0 0 30 0 0 0 0 0 97 4 0 0 0 0 3 0 0 0 0 0 0 125 0 2 0 29 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 2 1 0 0 0 0 0 0 0 0 1 193 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0
     */
    if ((nfsd_fp = fopen(nfsd_filename, "r")) != NULL) {
	for (lineno = 0; fgets(buffer, 4095, nfsd_fp) != NULL; lineno++) {
	    buffer[strlen(buffer) - 1] = 0;	/* ditch end of line  newline */

	    if (!strncmp("proc2 ", buffer, 6)) {
		DEBUG fprintf(stderr, "server proc2 found\n");
		/* server version 2 line readers "proc2 18 num num etc" */
		len = strlen(buffer);
		for (j = 0, i = 8; i < len && j < NFS_V2_NAMES_COUNT; i++) {
		    if (buffer[i] == ' ') {
			nfsp->v2s[j] = 0;
			nfsp->v2s[j] = atoll(&buffer[i + 1]);
			j++;
		    }
		}
	    }
	    if (!strncmp("proc3 ", buffer, 6)) {
		DEBUG fprintf(stderr, "server proc3 found\n");
		/* server version 3 line readers "proc3 22 num num etc" 
		   01234567890 so 8 for the first number */
		len = strlen(buffer);
		for (j = 0, i = 8; i < len && j < NFS_V2_NAMES_COUNT; i++) {
		    if (buffer[i] == ' ') {
			nfsp->v3s[j] = 0;
			nfsp->v3s[j] = atoll(&buffer[i + 1]);
			j++;
		    }
		}
	    }
	    if (!strncmp("proc4ops ", buffer, 9)) {
		DEBUG fprintf(stderr, "server proc4ops found\n");
		/* server version 4 line readers "proc4ops 40 num num etc"  
		   012345678901
		   NOTE: the "ops" hence starting in column 9 for the number of stats and 11 for the first stat */
		len = strlen(buffer);
		for (j = 0, i = 11; i < len && j < NFS_V4S_NAMES_COUNT;
		     i++) {
		    if (buffer[i] == ' ') {
			nfsp->v4s[j] = 0;
			nfsp->v4s[j] = atoll(&buffer[i + 1]);
			j++;
		    }
		}
	    }
	}
	ret = fclose(nfsd_fp);
	if (ret != 0)
	    DEBUG fprintf(stderr, "fclose(nfsd_fp)=%d errno=%d\n", ret, errno);
    } else {			/* zero all the server counters */
	for (j = 0; j < NFS_V2_NAMES_COUNT; j++) {
	    nfsp->v2s[j] = 0;
	    nfsq->v2s[j] = 0;
	}
	for (j = 0; j < NFS_V3_NAMES_COUNT; j++) {
	    nfsp->v3s[j] = 0;
	    nfsq->v3s[j] = 0;
	}
	for (j = 0; j < NFS_V4S_NAMES_COUNT; j++) {
	    nfsp->v4s[j] = 0;
	    nfsq->v4s[j] = 0;
	}
    }
}

void nfs_init()
{
    nfs_getdata();
    memcpy(nfsq, nfsp, (size_t) sizeof(struct nfs_stat));
}

void nfs(double elapsed)
{
    int i;
    long long total;

    nfs_getdata();
    /* Note: ignoring the first odd stat as this can have a low number and the rest be all zero */
    for (total = 0, i = 1; i < NFS_V2_NAMES_COUNT; i++)
	total += nfsp->v2c[i];
    if (total > 100) {
	psection("NFS2client");
	for (i = 0; i < NFS_V2_NAMES_COUNT; i++) {
	    pdouble(nfs_v2_names[i],
		    ((double) (nfsp->v2c[i] - nfsq->v2c[i])) / elapsed);
	}
	psectionend();
    }
    for (total = 0, i = 1; i < NFS_V2_NAMES_COUNT; i++)
	total += nfsp->v2s[i];
    if (total > 100) {
	psection("NFS2server");
	for (i = 0; i < NFS_V2_NAMES_COUNT; i++) {
	    pdouble(nfs_v2_names[i],
		    ((double) (nfsp->v2s[i] - nfsq->v2s[i])) / elapsed);
	}
	psectionend();

    }
    for (total = 0, i = 1; i < NFS_V3_NAMES_COUNT; i++)
	total += nfsp->v3c[i];
    if (total > 100) {
	psection("NFS3client");
	for (i = 0; i < NFS_V3_NAMES_COUNT; i++) {
	    pdouble(nfs_v3_names[i],
		    ((double) (nfsp->v3c[i] - nfsq->v3c[i])) / elapsed);
	}
	psectionend();
    }
    for (total = 0, i = 1; i < NFS_V3_NAMES_COUNT; i++)
	total += nfsp->v3s[i];
    if (total > 100) {
	psection("NFS3server");
	for (i = 0; i < NFS_V3_NAMES_COUNT; i++) {
	    pdouble(nfs_v3_names[i],
		    ((double) (nfsp->v3s[i] - nfsq->v3s[i])) / elapsed);
	}
	psectionend();
    }
    for (total = 0, i = 1; i < NFS_V4C_NAMES_COUNT; i++)
	total += nfsp->v4c[i];
    if (total > 100) {
	psection("NFS4client");
	for (i = 0; i < NFS_V4C_NAMES_COUNT; i++) {
	    pdouble(nfs_v4c_names[i],
		    ((double) (nfsp->v4c[i] - nfsq->v4c[i])) / elapsed);
	}
	psectionend();
    }
    for (total = 0, i = 1; i < NFS_V4S_NAMES_COUNT; i++)
	total += nfsp->v4s[i];
    if (total > 100) {
	psection("NFS4server");
	for (i = 0; i < NFS_V4S_NAMES_COUNT; i++) {
	    pdouble(nfs_v4s_names[i],
		    ((double) (nfsp->v4s[i] - nfsq->v4s[i])) / elapsed);
	}
	psectionend();
    }
}

/* - - - - - gpfs - - - - */
#ifndef NOGPFS
int gpfs_na = 0;		/* Not available, switches off any futher GPFS stats collection attempts */
char ip[1024];			/* IP address */
char nn[1024];			/* Node name! */
char cl[1024];			/* Cluster! */
char fs[1024];			/* Filesystem! */

/* this is the io_s stats data structure */
/* _io_s_ _n_ 192.168.50.20 _nn_ ems1-hs _rc_ 0 _t_ 1548346611 _tu_ 65624 _br_ 0 _bw_ 0 _oc_ 1 _cc_ 1 _rdc_ 0 _wc_ 0 _dir_ 1 _iu_ 0 */
struct gpfs_io {
    long long rc;
    long long t;
    long long tu;
    long long d;
    long long br;
    long long bw;
    long long oc;
    long long cc;
    long long rdc;
    long long wc;
    long long dir;
    long long iu;
} gpfs_io_prev, gpfs_io_curr;

/* this is the fs_io_s stats data structure */
/*_fs_io_s_ _n_ 192.168.50.20 _nn_ ems1-hs _rc_ 0 _t_ 1548519197 _tu_ 560916 _cl_ SBANK_ESS.gpfs.net _fs_ cesroot _d_ 4 _br_ 224331 _bw_ 225922 _oc_ 63 _cc_ 58 _rdc_ 35 _wc_ 34 _dir_ 2 _iu_ 14 */

#define MAX_FS 64

struct gpfs_fs {		/* this is the fs_io_s stats data structure */
    long long rc;
    long long t;
    long long tu;
    char cl[512];
    char fs[512];
    long long d;
    long long br;
    long long bw;
    long long oc;
    long long cc;
    long long rdc;
    long long wc;
    long long dir;
    long long iu;
} gpfs_fs_prev[MAX_FS], gpfs_fs_curr[MAX_FS];

int outfd[2];
int infd[2];
long int gpfs_pid = -999;

int gpfs_grab()
{
    int i = 0;
    int index = 0;
    int records = 0;
    int ret;
    int count;
    int loops = 0;
    char buffer[512 * MAX_FS]; /* 16 very large numbers max 20 digits = 320  so add a bit */

    FUNCTION_START;
    if (gpfs_na)
	return -1;

    if (gpfs_pid != -999) {		/* check we still have mmpmon running in the background */
            ret = kill(gpfs_pid, 0);	/* signal 0 means don't actually send a signal */
            DEBUG fprintf(stderr, "gpfs process kill(%ld, 0) returned =%d [0 is good]\n", gpfs_pid, ret);
	    FLUSH;
            if (ret != 0) {
                gpfs_na = 1;
                return -1;
            }
    }

    /* first the total I/O stats */
    count = write(outfd[1], "io_s\n", strlen("io_s\n"));
    if (count != strlen("io_s\n")) {
        DEBUG fprintf(stderr, "gpfs write io_s returned count=%d\n", count);
	FLUSH;
	gpfs_na = 1;
	return 0;
    }
reread:
    loops++;
    DEBUG fprintf(stderr, "gpfs reading io_s\n");
    FLUSH;
    count = read(infd[0], buffer, sizeof(buffer) - 1);
    if( count >= 0)
        buffer[count] = 0; /* end the return buffer */

    if(count > 6 && strncmp(buffer, "_io_s_", 6)) {
        DEBUG fprintf(stderr, "gpfs read io_s but GOT something else & ignoring it. Count=%d text=|%s|\n", count,buffer);
	FLUSH;
	if(loops < 65) /* Sanity check to stop infinite looping */
		goto reread;
    }
    DEBUG fprintf(stderr, "gpfs read io_s and got it Count=%d text=|%s|\n", count,buffer);
    FLUSH;
    if (count > 6) {
	buffer[count] = 0;
        DEBUG fprintf(stderr, "gpfs read io_s count=%d text=|%s|\n", count,buffer);
	FLUSH;
	ret = sscanf(buffer, "_io_s_ _n_ %s _nn_ %s _rc_ %lld _t_ %lld _tu_ %lld _br_ %lld _bw_ %lld _oc_ %lld _cc_ %lld _rdc_ %lld _wc_ %lld _dir_ %lld _iu_ %lld",
		   &ip[0],		/* 1 */
		   &nn[0],		/* 2 */
		   &gpfs_io_curr.rc,	/* 3 */
		   &gpfs_io_curr.t,     /* 4 */
		   &gpfs_io_curr.tu,	/* 5 */
		   &gpfs_io_curr.br,  	/* 6 */
		   &gpfs_io_curr.bw,	/* 7 */
		   &gpfs_io_curr.oc,    /* 8 */
		   &gpfs_io_curr.cc,	/* 9 */
		   &gpfs_io_curr.rdc,   /* 10 */
		   &gpfs_io_curr.wc,	/* 11 */
		   &gpfs_io_curr.dir,   /* 12 */
		   &gpfs_io_curr.iu);	/* 13 */
	if(ret != 13){
                DEBUG fprintf(stderr, "GPFS read io_s failed. Returned %d should have been 13\nGPFS=|%s|",ret,buffer);
	    	FLUSH;
		gpfs_na = 1;
		return 0;
	}
    } else {
        DEBUG fprintf(stderr, "gpfs read io_s bad count=%d\n",count);
	FLUSH;
	gpfs_na = 1;
        return 0;
    }

    /* second the 1 or more filesystem I/O stats */
    count = write(outfd[1], "fs_io_s\n", strlen("fs_io_s\n"));
    if (count > 7 && count != strlen("fs_io_s\n")) {
        DEBUG fprintf(stderr, "gpfs write fs_io_s returned=%d\n", count);
	FLUSH;
	gpfs_na = 1;
	return 0;
    }

    usleep(1000); /* mmpmon does NOT output the many lines of output in one go */
		 /* So pause then we can get them all in one read */
		 /* This works find on my 5 filesystems test bed system with usleep(100); */
		 /* 1000 = 1/1000th of a second might work if there are lots of GPFS filesystems */

    count = read(infd[0], buffer, sizeof(buffer) - 1);

    if (count > 0) {
        buffer[count] = 0;		/*ensure a zero string ending */
        DEBUG fprintf(stderr, "gpfs read fs_io_s retrieved count=%d text=|%s|\n", count,buffer);
	FLUSH;
	for (i = 0; i < count; i++) {
	    if (buffer[i] == '\n')
		records++;
	}
	if (records > MAX_FS)
	    records = MAX_FS;
        index = 0;
	for (i = 0; i < records; i++) {
		/*_fs_io_s_ _n_ 192.168.50.20 _nn_ ems1-hs _rc_ 0 _t_ 1548519197 _tu_ 560916 _cl_ SBANK_ESS.gpfs.net _fs_ cesroot _d_ 4 _br_ 224331 _bw_ 225922 _oc_ 63 _cc_ 58 _rdc_ 35 _wc_ 34 _dir_ 2 _iu_ 14 */

		ret = sscanf(&buffer[index],"_fs_io_s_ _n_ %s _nn_ %s _rc_ %lld _t_ %lld _tu_ %lld _cl_ %s _fs_ %s _d_ %lld _br_ %lld _bw_ %lld _oc_ %lld _cc_ %lld _rdc_ %lld _wc_ %lld _dir_ %lld _iu_ %lld",
		        &ip[0], 		/* 1 */
			&nn[0],			/* 2 */
			&gpfs_fs_curr[i].rc, 	/* 3 */
		        &gpfs_fs_curr[i].t,	/* 4 */
			&gpfs_fs_curr[i].tu, 	/* 5 */
		        &gpfs_fs_curr[i].cl[0],	/* 6 */
			&gpfs_fs_curr[i].fs[0],	/* 7 */
		        &gpfs_fs_curr[i].d,	/* 8 */
			&gpfs_fs_curr[i].br, 	/* 9 */
		        &gpfs_fs_curr[i].bw,	/* 10 */
			&gpfs_fs_curr[i].oc, 	/* 11 */
		        &gpfs_fs_curr[i].cc,	/* 13 */
			&gpfs_fs_curr[i].rdc, 	/* 13 */
		        &gpfs_fs_curr[i].wc,	/* 14 */
			&gpfs_fs_curr[i].dir, 	/* 15 */
		        &gpfs_fs_curr[i].iu);	/* 16  */
	    if( ret != 16 ) {
                DEBUG fprintf(stderr, "GPFS read fs_io_s failed. Returned %d should have been 16\nGPFS=|%s|",ret,buffer);
	    	FLUSH;
		gpfs_na = 1;
		records = 0;
		break; 
	    }
	    for (; index < count; index++) {
		if (buffer[index] == '\n') {	/* find newline = terminating the current record */
		    index++;	/* move to after the newline */
		    break;
		}
	    }
	    if (index == count)
		break;
	}
    } else {
        DEBUG fprintf(stderr, "gpfs read fs_io_s returned=%d\n", count);
	FLUSH;
	gpfs_na = 1;
    }
    DEBUG fprintf(stderr, "gpfs_grab() returning=%d\n", records);
    return records;
}

void gpfs_init()
{
    char *s;
    int filesystems = 0;
    struct stat sb;		/* to check if mmpmon is executable and gpfs is installed */

    /* call shell script to start mmpmon binary */
    char *argv[] = { "/usr/lpp/mmfs/bin/mmksh", "-c", "/usr/lpp/mmfs/bin/mmpmon -s -p", 0 };	/* */
    char *prog1 = "/usr/lpp/mmfs/bin/mmksh";
    char *prog2 = "/usr/lpp/mmfs/bin/mmpmon";

    /* Alternative: direct start of mmpmon */
    /* char *argv[]={ "/usr/lpp/mmfs/bin/tspmon", "1000", "1", "1", "0", "0", "60", "0", "/var/mmfs/mmpmon/mmpmonSocket", 0}; */

    FUNCTION_START;

    s = getenv("NOGPFS");
    if(s != 0) {
            if(atoi(s) != 0) {
                DEBUG fprintf(stderr, "GPFS off due to shell NOGPFS\n");
                gpfs_na = 1;
                return;
            }
    }

    if (uid != (uid_t) 0) {
        DEBUG fprintf(stderr, "GPFS off - not the root user \n");
	gpfs_na = 1;		/* not available = mmpmon required root user */
    }

    if (stat(prog1, &sb) != 0){
        DEBUG fprintf(stderr, "GPFS off - not found = %s\n", prog1);
	gpfs_na = 1;		/* not available = no file */
    }

    if (stat(prog2, &sb) != 0){
        DEBUG fprintf(stderr, "GPFS off - not found = %s\n", prog2);
	gpfs_na = 1;		/* not available = no file */
    }

    if (!(sb.st_mode & S_IXUSR)){
        DEBUG fprintf(stderr, "GPFS off - mmksh not executable\n");
	gpfs_na = 1;		/* not available = not executable */
    }

    if (gpfs_na)
	return;

    if (pipe(outfd) != 0) {	/* Where the parent is going to write outfd[1] to   child input outfd[0] */
        DEBUG fprintf(stderr, "GPFS off - pipe(out) failed\n");
	gpfs_na = 1;
	return;
    }
    if (pipe(infd) != 0) {	/* From where parent is going to read  infd[0] from child output infd[1] */
        DEBUG fprintf(stderr, "GPFS off - pipe(in) failed\n");
	gpfs_na = 1;
	return;
    }
    DEBUG fprintf(stderr, "forking to run GPFS mmpmon command\n");
    if ((gpfs_pid = fork()) == 0) {
	/* child process */
	close(0);
	dup2(outfd[0], 0);

	close(1);
	dup2(infd[1], 1);

	/* Not required for the child */
	close(outfd[0]);
	close(outfd[1]);
	close(infd[0]);
	close(infd[1]);

	execv(argv[0], argv);
	/* never returns */
    } else {
	if(gpfs_pid == -1) {
            DEBUG fprintf(stderr, "GPFS off - fork failed errono =%d\n",errno);
	    FLUSH;
            gpfs_na = 1;
	    return;
	}
	/* parent process */
	close(outfd[0]);	/* These are being used by the child */
	close(infd[1]);
	usleep(10000);		/* Pause 1/10th to let the child run and stop if GPFS is not running */

	filesystems = gpfs_grab();
        DEBUG fprintf(stderr, "GPFS ready with %d filesystems\n",filesystems);
	FLUSH;

	if( filesystems > 0) {
	    /* copy to the previous records for next time */
	    memcpy((void *) &gpfs_io_prev, (void *) &gpfs_io_curr,
	       sizeof(struct gpfs_io));
	    memcpy((void *) &gpfs_fs_prev[0], (void *) &gpfs_fs_curr[0],
	       sizeof(struct gpfs_fs) * filesystems);
        }
    }
}

void gpfs_data(double elapsed)
{
    int records;
    int i;

    FUNCTION_START;
    if (gpfs_na)
	return;

    records = gpfs_grab();

    if(records <= 0)
	return;

#define DELTA_GPFS(xxx)  ((double)(gpfs_io_curr.xxx - gpfs_io_prev.xxx)/elapsed)

    psection("gpfs_io_total");
    pstring("node", ip);
    pstring("name", nn);
    plong("rc", gpfs_io_curr.rc);	/* status */
    plong("time", gpfs_io_curr.t);	/* epoc seconds */
    plong("tu", DELTA_GPFS(tu));
    plong("readbytes", DELTA_GPFS(br));
    plong("writebytes", DELTA_GPFS(bw));
    plong("open", DELTA_GPFS(oc));
    plong("close", DELTA_GPFS(cc));
    plong("reads", DELTA_GPFS(rdc));
    plong("writes", DELTA_GPFS(wc));
    plong("directorylookup", DELTA_GPFS(dir));
    plong("inodeupdate", DELTA_GPFS(iu));
    psectionend();

    memcpy((void *) &gpfs_io_prev, (void *) &gpfs_io_curr,
	   sizeof(struct gpfs_io));

#define DELTA_GPFSFS(xxx)  ((double)(gpfs_fs_curr[i].xxx - gpfs_fs_prev[i].xxx)/elapsed)

    psection("gpfs_filesystems");
    for (i = 0; i < records; i++) {
	psub(gpfs_fs_curr[i].fs);
	pstring("node", ip);
	pstring("name", nn);
	plong("rc", gpfs_fs_curr[i].rc);	/* status */
	plong("time", gpfs_fs_curr[i].t);	/* epoc seconds */
	plong("tu", DELTA_GPFSFS(tu));
	pstring("cl", gpfs_fs_curr[i].cl);
	/*pstring("fs",	 gpfs_fs_curr[i].fs); */
	plong("disks", gpfs_fs_curr[i].d);
	plong("readbytes", DELTA_GPFSFS(br));
	plong("writebytes", DELTA_GPFSFS(bw));
	plong("open", DELTA_GPFSFS(oc));
	plong("close", DELTA_GPFSFS(cc));
	plong("reads", DELTA_GPFSFS(rdc));
	plong("writes", DELTA_GPFSFS(wc));
	plong("directorylookup", DELTA_GPFSFS(dir));
	plong("inodeupdate", DELTA_GPFSFS(iu));
	psubend();
    }
    psectionend();

    memcpy((void *) &gpfs_fs_prev[0], (void *) &gpfs_fs_curr[0],
	   sizeof(struct gpfs_fs) * records);
}
#endif				/* NOGPFS */


/* - - - Start of GPU - - - - */
#ifdef NVIDIA_GPU

typedef int nvmlReturn_t;
#define NVML_SUCCESS 0

typedef struct nvmlUtilization_st {
    unsigned int gpu;
    unsigned int memory;
} nvmlUtilization_t;

struct nvmlDevice_st;
typedef struct nvmlDevice_st *nvmlDevice_t;

nvmlReturn_t nvmlInit(void);
nvmlReturn_t nvmlShutdown(void);
nvmlReturn_t nvmlDeviceGetCount(unsigned int *count);
nvmlReturn_t nvmlDeviceGetHandleByIndex(unsigned int index,
					nvmlDevice_t * device);
nvmlReturn_t nvmlDeviceGetUtilizationRates(nvmlDevice_t device,
					   nvmlUtilization_t *
					   utilization);
nvmlReturn_t nvmlSystemGetDriverVersion(char *version, int count);
nvmlReturn_t nvmlSystemGetNVMLVersion(char *version, int count);
nvmlReturn_t nvmlDeviceGetName(nvmlDevice_t device, char *name, int count);
nvmlReturn_t nvmlDeviceGetTemperature(nvmlDevice_t device, int type,
				      unsigned int *temp);
nvmlReturn_t nvmlDeviceGetPowerUsage(nvmlDevice_t device,
				     unsigned int *watts);
nvmlReturn_t nvmlDeviceGetClockInfo(nvmlDevice_t device, int type,
				    unsigned int *mhz);

#define NVML_TEMPERATURE_GPU 0
#define NVML_CLOCK_GRAPHICS 0

#define GPUS 16			/* max number of GPUs */
nvmlDevice_t gpu_device[GPUS];
nvmlUtilization_t gpu_util[GPUS];
unsigned int gpu_devices = 4;

char gpu_desc[GPUS][1024];
unsigned int gpu_temp[GPUS];
unsigned int gpu_watts[GPUS];
unsigned int gpu_clock[GPUS];

char gpu_driver_version[1024];
char gpu_nvml_version[1024];

void gpu_init()
{
    int i;
    nvmlReturn_t nvres;
    char ebuf[1024];

    if ((nvres = nvmlInit()) != NVML_SUCCESS) {
	sprintf(ebuf, "nvmlInit failed %d\n", nvres);
	nwarning(ebuf);
	gpu_devices = 0;
	return;
    }

    if ((nvres =
	 nvmlSystemGetDriverVersion(&gpu_driver_version[0],
				    1024)) != NVML_SUCCESS) {
	sprintf(ebuf, "nvmlSystemGetDriverVersion failed %d\n", nvres);
	nwarning(ebuf);
	gpu_devices = 0;
	return;
    }
    if ((nvres =
	 nvmlSystemGetNVMLVersion(&gpu_nvml_version[0],
				  1024)) != NVML_SUCCESS) {
	sprintf(ebuf, "nvmlSystemGetNVMLVersion failed %d\n", nvres);
	nwarning(ebuf);
	gpu_devices = 0;
	return;
    }

    if ((nvres = nvmlDeviceGetCount(&gpu_devices)) != NVML_SUCCESS) {
	printf(ebuf, "nvmlDeviceGetCount failed %d\n", nvres);
	nwarning(ebuf);
	gpu_devices = 0;
	return;
    }
    if (gpu_devices > 16)
	gpu_devices = 16;

    for (i = 0; i < gpu_devices; i++) {
	if (nvmlDeviceGetHandleByIndex(i, &gpu_device[i]) != NVML_SUCCESS) {
	    sprintf(ebuf, "nvmlDeviceGetHandleByIndex(%d) failed %d\n", i,
		    nvres);
	    nwarning(ebuf);
	    gpu_devices = 0;
	    return;
	}
    }
}

void gpu_stats()
{
    static int first_time_gpu = 1;
    char buf[64];
    int i;

    FUNCTION_START;
    if (gpu_devices <= 0)
	return;

    for (i = 0; i < gpu_devices; i++) {
	if (first_time_gpu) {
	    if (nvmlDeviceGetName(gpu_device[i], &gpu_desc[i][0], 1024) !=
		NVML_SUCCESS)
		strcpy(gpu_desc[i], "NVML API Failed");
	}
	if (nvmlDeviceGetUtilizationRates(gpu_device[i], &gpu_util[i]) !=
	    NVML_SUCCESS) {
	    gpu_util[i].gpu = 999;
	    gpu_util[i].memory = 999;
	}
	if (nvmlDeviceGetTemperature
	    (gpu_device[i], NVML_TEMPERATURE_GPU,
	     &gpu_temp[i]) != NVML_SUCCESS)
	    gpu_temp[i] = 999;
	if (nvmlDeviceGetPowerUsage(gpu_device[i], &gpu_watts[i]) !=
	    NVML_SUCCESS)
	    gpu_watts[i] = 999;
	if (nvmlDeviceGetClockInfo
	    (gpu_device[i], NVML_CLOCK_GRAPHICS,
	     &gpu_clock[i]) != NVML_SUCCESS)
	    gpu_clock[i] = 999;
    }
    first_time_gpu = 0;

    psection("GPU");
    for (i = 0; i < gpu_devices; i++) {
	sprintf(buf, "GPU%d", i + 1);
	psub(buf);
	pstring("desciption", gpu_desc[i]);
	plong("gpu_mhz", gpu_clock[i]);
	plong("gpu_cpu_util", gpu_util[i].gpu);
	plong("gpu_mem_util", gpu_util[i].memory);
	plong("gpu_centigrade", gpu_temp[i]);
	plong("gpu_watts", gpu_watts[i] / 1000);
	psubend();
    }
    psectionend();
}
#endif				/*NVIDIA_GPU */


/*
Have to read files in the following formats
name number
name=number
name=numer kB
name=numer bytes
*/
extern long power_timebase;	/* lower down this file */
long long purr_prevous = 0;
long long purr_current = 0;
long long pool_idle_time_prevous = 0;
long long pool_idle_time_current = 0;
int lparcfg_found = 0;

void init_lparcfg()
{
    FILE *fp = 0;
    char line[1024];
    char label[1024];
    char number[1024];

    FUNCTION_START;
    if ((fp = fopen("/proc/ppc64/lparcfg", "r")) == NULL) {
	lparcfg_found = 0;
	return;
    } else
	lparcfg_found = 1;

    while (fgets(line, 1000, fp) != NULL) {
	if (!strncmp(line, "purr=", strlen("purr="))) {
	    sscanf(line, "%s=%s", label, number);
	    purr_current = atoll(number);
	}
	if (!strncmp(line, "pool_idle_time=", strlen("pool_idle_time="))) {
	    sscanf(line, "%s=%s", label, number);
	    pool_idle_time_current = atoll(number);
	}
    }
}

int any_alpha(char *number)
{
    int i;
    int len;

    len = strlen(number);
    for (i = 0; i < len; i++) {
	if (isdigit(number[i]))
	    continue;
	if (number[i] == '.')
	    continue;
	return 1;
    }
    return 0;
}

void read_lparcfg(double elapsed)
{
    static FILE *fp = 0;
    static char line[1024];
    char label[1024];
    char number[1024];
    int i;
    int len = 0;

    if (lparcfg_found == 0)
	return;
    FUNCTION_START;
    if (fp == 0) {
	if ((fp = fopen("/proc/ppc64/lparcfg", "r")) == NULL) {
	    return;
	}
    } else
	rewind(fp);

    psection("ppc64_lparcfg");
    while (fgets(line, 1000, fp) != NULL) {

	len = strlen(line);
	if (line[len - 1] == '\n')
	    line[len - 1] = 0;	/* remove newline */

	/* lparcfg version strangely with no = */
	if (!strncmp("lparcfg ", line, 8)) {
	    pstring("lparcfg_version", &line[8]);
	    continue;
	}
	/* lparcfg version strangely with no = */
	if (!strncmp("serial_number", line, strlen("serial_number"))) {
	    pstring("serial_number", &line[14]);
	    continue;
	}
	/* skip the dumb-ass blank line! */
	if (strlen(line) < 2)	/* include a single newline */
	    continue;

	len = strlen(line);
	/* remove dumb-ass line ending " bytes" */
	if (line[len - 1] == 's' &&
	    line[len - 2] == 'e' &&
	    line[len - 3] == 't' &&
	    line[len - 4] == 'y' &&
	    line[len - 5] == 'b' && line[len - 6] == ' ')
	    line[len - 6] = 0;

	for (i = 0; i < len; i++)	/* strip out the equals sign */
	    if (line[i] == '=')
		line[i] = ' ';

	sscanf(line, "%s %s", label, number);
	if (any_alpha(number))
	    pstring(label, number);
	else {
	    plong(label, atoll(number));
	    if (!strncmp(line, "purr ", strlen("purr "))) {
		purr_prevous = purr_current;
		purr_current = atoll(number);
		if (purr_prevous != 0 && purr_current != 0 && power_timebase != 0)
		    pdouble("physical_consumed",
			    (double) (purr_current -
				      purr_prevous) /
			    (double) power_timebase / elapsed);
	    }
	    if (!strncmp(line, "pool_idle_time", strlen("pool_idle_time"))) {
		pool_idle_time_prevous = pool_idle_time_current;
		pool_idle_time_current = atoll(number);
		if (pool_idle_time_prevous != 0
		    && pool_idle_time_current != 0)
		    pdouble("pool_idle_cpu",
			    (double) (pool_idle_time_current -
				      pool_idle_time_prevous) /
			    (double) power_timebase / elapsed);
	    }
	}
    }
    psectionend();
}

#define ADD_LABEL(ch) label[labelch++]   = ch
#define ADD_NUM(ch)   numstr[numstrch++] = ch
/*
read files in format
name number
name: number
name: numer kB
*/

/* increamenting counters */
struct rate {
	char label_orig[32];
	char label_rate[32];
	long long saved;
} rates[] = {
	/* swap */
	{"pswpin",   "pswpon_rate",  0 },
	{"pswpout",  "pswpout_rate", 0 },

	/* paging */
	{"pgpgin", "pgpgin_rate", 0},
	{"pgpgout", "pgpgout_rate", 0},
	{"pgalloc_dma", "pgalloc_dma_rate", 0},
	{"pgalloc_dma32", "pgalloc_dma32_rate", 0},
	{"pgalloc_normal", "pgalloc_normal_rate", 0},
	{"pgalloc_movable", "pgalloc_movable_rate", 0},
	{"pgskip_dma", "pgskip_dma_rate", 0},
	{"pgskip_dma32", "pgskip_dma32_rate", 0},
	{"pgskip_normal", "pgskip_normal_rate", 0},
	{"pgskip_movable", "pgskip_movable_rate", 0},
	{"pgfree", "pgfree_rate", 0},
	{"pgactivate", "pgactivate_rate", 0},
	{"pgdeactivate", "pgdeactivate_rate", 0},
	{"pglazyfree", "pglazyfree_rate", 0},
	{"pgfault", "pgfault_rate", 0},
	{"pgmajfault", "pgmajfault_rate", 0},
	{"pglazyfreed", "pglazyfreed_rate", 0},
	{"pgrefill", "pgrefill_rate", 0},
	{"pgsteal_kswapd", "pgsteal_kswapd_rate", 0},
	{"pgsteal_direct", "pgsteal_direct_rate", 0},
	{"pgscan_kswapd", "pgscan_kswapd_rate", 0},
	{"pgscan_direct", "pgscan_direct_rate", 0},
	{"pgscan_direct_throttle", "pgscan_direct_throttle_rate", 0},
	{"pgscan_anon", "pgscan_anon_rate", 0},
	{"pgscan_file", "pgscan_file_rate", 0},
	{"pgsteal_anon", "pgsteal_anon_rate", 0},
	{"pgsteal_file", "pgsteal_file_rate", 0},
	{"pginodesteal", "pginodesteal_rate", 0},
	{"pgrotated", "pgrotated_rate", 0},
	{"pgmigrate_success", "pgmigrate_success_rate", 0},
	{"pgmigrate_fail", "pgmigrate_fail_rate", 0},
	{"unevictable_pgs_culled", "unevictable_pgs_culled_rate", 0},
	{"unevictable_pgs_scanned", "unevictable_pgs_scanned_rate", 0},
	{"unevictable_pgs_rescued", "unevictable_pgs_rescued_rate", 0},
	{"unevictable_pgs_mlocked", "unevictable_pgs_mlocked_rate", 0},
	{"unevictable_pgs_munlocked", "unevictable_pgs_munlocked_rate", 0},
	{"unevictable_pgs_cleared", "unevictable_pgs_cleared_rate", 0},
	{"unevictable_pgs_stranded", "unevictable_pgs_stranded_rate", 0}
	};



void read_data_number(char *statname, double elapsed)
{
    FILE *fp = 0;
    char line[2048];
    char filename[1024];
    char label[2048];
    char number[512];
    int i;
    int len;

    FUNCTION_START;
    sprintf(filename, "/proc/%s", statname);
    if ((fp = fopen(filename, "r")) == NULL) {
	sprintf(line, "read_data_number: failed to open file %s", filename);
	nwarning(line);
	return;
    }
    sprintf(label, "proc_%s", statname);
    psection(label);
    while (fgets(line, 1000, fp) != NULL) {
	len = strlen(line);
	for (i = 0; i < len; i++) {
	    if (line[i] == '(')
		line[i] = '_';
	    if (line[i] == ')')
		line[i] = ' ';
	    if (line[i] == ':')
		line[i] = ' ';
	    if (line[i] == '\n')
		line[i] = 0;
	}
	sscanf(line, "%s %s", label, number);

	/*printf("read_data_numer(%s) |%s| |%s|=%lld\n", statname,label,numstr,atoll(numstr)); */
	plong(label, atoll(number));

	for(i = 0;i < (sizeof(rates)/sizeof(struct rate));i++) {
	    if( !strcmp(label, rates[i].label_orig) ) {
		if(rates[i].saved != 0)
		    plong(rates[i].label_rate, (atoll(number) - rates[i].saved) / elapsed);
		rates[i].saved = atoll(number);
		break;
	    }
	}
    }
    psectionend();
    (void) fclose(fp);
}

void proc_stat(double elapsed, int print, int reduced_stats)
{				/* read /proc/stat and unpick */
    long long user;
    long long nice;
    long long sys;
    long long idle;
    long long iowait;
    long long hardirq;
    long long softirq;
    long long steal;
    long long guest;
    long long guestnice;
    int cpu_total = 0;
    int count;
    int cpuno;
    long long value;
    static FILE *fp = 0;
    static char line[8192];
    static int max_cpuno;

    struct utilisation {
	long long user;
	long long nice;
	long long sys;
	long long idle;
	long long iowait;
	long long hardirq;
	long long softirq;
	long long steal;
	long long guest;
	long long guestnice;
    };
#define MAX_LOGICAL_CPU 256
    static long long old_ctxt;
    static long long old_processes;
    static struct utilisation total_cpu;
    static struct utilisation logical_cpu[MAX_LOGICAL_CPU];
    char label[512];

    FUNCTION_START;
    /* printf("DEBUG\t--> proc_stat(%.4f, %d) max_cpuno=%d\n",elapsed, print,max_cpuno); */
    if (fp == 0) {
	if ((fp = fopen("/proc/stat", "r")) == NULL) {
	    sprintf(errorbuf, "failed to open file /proc/stat errno=%d", errno);
	    nwarning(errorbuf);
	    fp = 0;
	    return;
	}
    } else
	rewind(fp);

    while (fgets(line, 1000, fp) != NULL) {

	if (!strncmp(line, "cpu", 3)) {
	    if (!strncmp(line, "cpu ", 4)) {	/* this is the first line and is the average total CPU stats */
		cpu_total = 1;
		count = sscanf(&line[4],	/* cpu USER */
			       "%lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
			       &user, &nice, &sys, &idle,
			       &iowait, &hardirq, &softirq,
			       &steal, &guest, &guestnice);
		if (print) {
#define DELTA_TOTAL(stat) ((double)((double)stat - (double)total_cpu.stat)/(double)elapsed/((double)(max_cpuno + 1.0)))
		    psection("cpu_total");
		    pdouble("user", DELTA_TOTAL(user));	/* incrementing counter */
		    pdouble("nice", DELTA_TOTAL(nice));	/* incrementing counter */
		    pdouble("sys", DELTA_TOTAL(sys));	/* incrementing counter */
		    pdouble("idle", DELTA_TOTAL(idle));	/* incrementing counter */
/*			pdouble("DEBUG IDLE idle: %lld %lld %lld\n", total_cpu.idle, idle, idle-total_cpu.idle); */
		    pdouble("iowait", DELTA_TOTAL(iowait));	/* incrementing counter */
		    pdouble("hardirq", DELTA_TOTAL(hardirq));	/* incrementing counter */
		    pdouble("softirq", DELTA_TOTAL(softirq));	/* incrementing counter */
		    pdouble("steal", DELTA_TOTAL(steal));	/* incrementing counter */
		    pdouble("guest", DELTA_TOTAL(guest));	/* incrementing counter */
		    pdouble("guestnice", DELTA_TOTAL(guestnice));	/* incrementing counter */
		    psectionend();
		}
		total_cpu.user = user;
		total_cpu.nice = nice;
		total_cpu.sys = sys;
		total_cpu.idle = idle;
		total_cpu.iowait = iowait;
		total_cpu.hardirq = hardirq;
		total_cpu.softirq = softirq;
		total_cpu.steal = steal;
		total_cpu.guest = guest;
		total_cpu.guestnice = guestnice;
		continue;
	    } else {
		if(reduced_stats)
		    continue;
		if (cpu_total == 1)	/* first cpuNNN line */
		    if (print)
			psection("cpus");
		cpu_total++;
		count = sscanf(&line[3],	/* cpuNNNN USER */
			       "%d %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
			       &cpuno, &user, &nice, &sys, &idle,
			       &iowait, &hardirq, &softirq,
			       &steal, &guest, &guestnice);
		if (cpuno > max_cpuno)
		    max_cpuno = cpuno;
		if (cpuno >= MAX_LOGICAL_CPU)
		    continue;
		if (print) {
		    sprintf(label, "cpu%d", cpuno);
		    psub(label);
#define DELTA_LOGICAL(stat) ((double)((double)stat - (double)logical_cpu[cpuno].stat)/(double)elapsed)
		    pdouble("user", DELTA_LOGICAL(user));	/* counter */
		    pdouble("nice", DELTA_LOGICAL(nice));	/* counter */
		    pdouble("sys", DELTA_LOGICAL(sys));	/* counter */
		    pdouble("idle", DELTA_LOGICAL(idle));	/* counter */
/*			pdouble("DEBUG IDLE idle: %lld %lld %lld\n", logical_cpu[cpuno].idle, idle, idle-logical_cpu[cpuno].idle); */
		    pdouble("iowait", DELTA_LOGICAL(iowait));	/* counter */
		    pdouble("hardirq", DELTA_LOGICAL(hardirq));	/* counter */
		    pdouble("softirq", DELTA_LOGICAL(softirq));	/* counter */
		    pdouble("steal", DELTA_LOGICAL(steal));	/* counter */
		    pdouble("guest", DELTA_LOGICAL(guest));	/* counter */
		    pdouble("guestnice", DELTA_LOGICAL(guestnice));	/* counter */
		    psubend();
		}
		logical_cpu[cpuno].user = user;
		logical_cpu[cpuno].nice = nice;
		logical_cpu[cpuno].sys = sys;
		logical_cpu[cpuno].idle = idle;
		logical_cpu[cpuno].iowait = iowait;
		logical_cpu[cpuno].hardirq = hardirq;
		logical_cpu[cpuno].softirq = softirq;
		logical_cpu[cpuno].steal = steal;
		logical_cpu[cpuno].guest = guest;
		logical_cpu[cpuno].guestnice = guestnice;
		continue;
	    }
	}
	if (!strncmp(line, "ctxt", 4)) {
	    if (print)
		psectionend();	/* rather aassumes ctxt is the first non "cpu" line */
	    value = 0;
	    count = sscanf(&line[5], "%lld", &value);	/* counter */
	    if (count == 1) {
		if (print) {
		    psection("stat_counters");
		    pdouble("ctxt",
			    ((double) (value - old_ctxt) / elapsed));
		}
		old_ctxt = value;
	    }
	    continue;
	}
	if (!strncmp(line, "btime", 5)) {
	    value = 0;
	    count = sscanf(&line[6], "%lld", &value);	/* seconds since boot */
	    if (print)
		plong("btime", value);
	    continue;
	}
	if (!strncmp(line, "processes", 9)) {
	    value = 0;
	    count = sscanf(&line[10], "%lld", &value);	/* counter  actually forks */
	    if (print)
		pdouble("processes_forks", ((double) (value - old_processes) / elapsed));
	    old_processes = value;
	    continue;
	}
	if (!strncmp(line, "procs_running", 13)) {
	    value = 0;
	    count = sscanf(&line[14], "%lld", &value);
	    if (print)
		plong("procs_running", value);
	    continue;
	}
	if (!strncmp(line, "procs_blocked", 13)) {
	    value = 0;
	    count = sscanf(&line[14], "%lld", &value);
	    if (print) {
		plong("procs_blocked", value);
		psectionend();	/* rather assumes "blocked" is the last line */
	    }
	    continue;
	}
	/* any thing else will be ignored */
    }
}


struct diskinfo {
	long dk_count; /* the sscanf count of items on the line */
	long dk_major;
	long dk_minor;
	char dk_name[128];
	long long dk_reads;
	long long dk_rmerge;
	long long dk_rkb;
	long long dk_rmsec;
	long long dk_writes;
	long long dk_wmerge;
	long long dk_wkb;
	long long dk_wmsec;
	long long dk_inflight;
	long long dk_time;
	long long dk_backlog;
	long long dk_xfers;
	long long dk_bsize;
	/* extra stats for Kernel 4.8 */
	long long dk_discards; /* discard completed successfully */
	long long dk_discard_merges;  /* discard merge */
	long long dk_discard_sectors; /* sectors discarded */
	long long dk_discard_time;    /* time spent discarding - assumed miliseconds (assumed) */
	long long dk_flushes;   /* flush requests success - assumed miliseconds (assumed) */
	long long dk_flush_time;      /* time spent flushing - assumed miliseconds (assumed) */
    };

/* for the /proc/diskstats file content */
struct diskinfo *diskstat_current;
struct diskinfo *diskstat_previous;
long disks_all = 0;
FILE *diskstat_fp = 0;

/* actual disk drives and not all the other nosense in the diskstats file */
char **real_disks_list = 0;
long real_disks_count = 0;

int diskstats_resync = 0;

void diskstat_cleanup(struct diskinfo *disk) 
{
    FUNCTION_START;
	if (disk->dk_count == 7) {	/* shuffle the data around due to missing columns for partitions */
	    disk->dk_writes = disk->dk_rkb;
	    disk->dk_rkb = disk->dk_rmerge;
	    disk->dk_rmsec = 0;
	    disk->dk_rmerge = 0;

	} else if (disk->dk_count != 14)
	    DEBUG fprintf(stderr, "disk sscanf wanted 14 but returned=%ld\n", disk->dk_count);

	disk->dk_rkb /= 2;	/* sectors = 512 bytes */
	disk->dk_wkb /= 2;
	disk->dk_xfers = disk->dk_reads + disk->dk_writes;
	if (disk->dk_xfers == 0)
	    disk->dk_bsize = 0;
	else
	    disk->dk_bsize =
		((disk->dk_rkb +
		  disk->dk_wkb) / disk->dk_xfers) * 1024;
}

void add_real_disk(int num,char *name)
{
    FUNCTION_START;
    DEBUG fprintf(stderr, "add_real_disk(%d,%s)\n",num,name);
    if(num == 0)
            real_disks_list = (char **)malloc(sizeof(char *));
    else
            real_disks_list = (char **)realloc(real_disks_list, sizeof(char *) * (num+1));
    real_disks_list[num] = (char *)malloc(strlen(name+1));
    strcpy(&real_disks_list[num][0], name);
}

void proc_diskstats_init()
{
    char buf[1024];
    /* popen variables */
    FILE *pop;
    char tmpstr[1024 + 1];
    long i;
    long j;
    long len;
    long next_disk = 0;
    char *strret;

    struct diskinfo dstat;

    FUNCTION_START;
	pop = popen("lsblk --nodeps --output NAME,TYPE --raw 2>/dev/null", "r");
	if (pop != NULL) {
	    /* throw away the headerline */
	    strret = fgets(tmpstr, 70, pop);
	    for (real_disks_count = 0; ; real_disks_count++) {
		tmpstr[0] = 0;
		if (fgets(tmpstr, 70, pop) == NULL)
		    break;
		tmpstr[strlen(tmpstr)] = 0;	/* remove NL char */
		len = strlen(tmpstr);
		for (j = 0; j < len; j++)
		    if (tmpstr[j] == ' ')
			tmpstr[j] = 0;
		add_real_disk(real_disks_count, tmpstr);
	    }
	    pclose(pop);
	} else
	    real_disks_count = 0;

	if ((diskstat_fp = fopen("/proc/diskstats", "r")) == NULL) {
	    nwarning("failed to open - /proc/diskstats");
	    return;
	}

    disks_all = 0;
    while (fgets(buf, 1024, diskstat_fp) != NULL) {
	buf[strlen(buf) - 1] = 0;	/* remove newline */
	/*printf("DISKSTATS: \"%s\"", buf); */
	/* zero the data ready for reading */
	bzero(&dstat, sizeof(struct diskinfo));

	dstat.dk_count = sscanf(&buf[0],
		   "%ld %ld %s %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
		   &dstat.dk_major, &dstat.dk_minor,
		   &dstat.dk_name[0], &dstat.dk_reads,
		   &dstat.dk_rmerge, &dstat.dk_rkb,    &dstat.dk_rmsec,
		   &dstat.dk_writes, &dstat.dk_wmerge, &dstat.dk_wkb,
		   &dstat.dk_wmsec, &dstat.dk_inflight,
		   &dstat.dk_time, &dstat.dk_backlog,
		   /* new additions */
		   &dstat.dk_discards,
		   &dstat.dk_discard_merges,
		   &dstat.dk_discard_sectors,
		   &dstat.dk_discard_time,
		   &dstat.dk_flushes,
		   &dstat.dk_flush_time);

	diskstat_cleanup(&dstat);
	/* TEST TEST
	if(strcmp("sr0", dstat.dk_name) )
		continue;
	*/
	disks_all++;
	/* create the space to store the data */
	diskstat_previous = realloc(diskstat_previous,sizeof(struct diskinfo) * disks_all);
	diskstat_current  = realloc(diskstat_current ,sizeof(struct diskinfo) * disks_all);
	memcpy(&diskstat_current[next_disk++], &dstat, sizeof(struct diskinfo));
    }
    if(debug) {
	fprintf(stderr, "diskstats_init disks=%ld:", disks_all);
	for (i = 0; i < disks_all; i++) {
	     fprintf(stderr, " %s", diskstat_current[i].dk_name);
	}
	fprintf(stderr, "\n");
	fprintf(stderr, "diskstats_init real=%ld:", real_disks_count);
	for (i = 0; i < real_disks_count; i++) {
	     fprintf(stderr, " <%s>", real_disks_list[i]);
	}
	fprintf(stderr, "\n");
    }
}

int miss_count = 0;

void proc_diskstats_collect(double elapsed)
{
    char buf[1024];
    long i;
    long j;

    FUNCTION_START;
    if(diskstat_fp == NULL)
   	return;
   
    rewind(diskstat_fp);

    memcpy(&diskstat_previous[0], &diskstat_current[0], sizeof(struct diskinfo) * disks_all);

    j = 0;
    while (fgets(buf, 1024, diskstat_fp) != NULL) {
	buf[strlen(buf) - 1] = 0;	/* remove newline */
	/*printf("DISKSTATS: \"%s\"", buf); */
	/* zero the data ready for reading */
	bzero(&diskstat_current[j], sizeof(struct diskinfo));
	diskstat_current[j].dk_count =
	    sscanf(&buf[0],
		   "%ld %ld %s %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
		   &diskstat_current[j].dk_major, 
		   &diskstat_current[j].dk_minor,
		   &diskstat_current[j].dk_name[0], 
		   &diskstat_current[j].dk_reads,
		   &diskstat_current[j].dk_rmerge, 
		   &diskstat_current[j].dk_rkb, 
		   &diskstat_current[j].dk_rmsec,
		   &diskstat_current[j].dk_writes, 
		   &diskstat_current[j].dk_wmerge, 
		   &diskstat_current[j].dk_wkb,
		   &diskstat_current[j].dk_wmsec, 
		   &diskstat_current[j].dk_inflight,
		   &diskstat_current[j].dk_time, 
		   &diskstat_current[j].dk_backlog,
		   /* new additions */
		   &diskstat_current[j].dk_discards,
		   &diskstat_current[j].dk_discard_merges,
		   &diskstat_current[j].dk_discard_sectors,
		   &diskstat_current[j].dk_discard_time,
		   &diskstat_current[j].dk_flushes,
		   &diskstat_current[j].dk_flush_time);
/*
	if(strcmp("sr0", &diskstat_current[j].dk_name[0]) == 0 && miss_count++ < 3)
		continue;
*/

	diskstat_cleanup(&diskstat_current[j]);
	j++;
    }
    /* Check if the number of lines in /proc/diskstats is the same as when we started - ek! */
    diskstats_resync = disks_all - j;
    /* This is actioned in main() */

    if(debug) {
	fprintf(stderr, "diskstats_collect disks=%ld:", disks_all);
	for (i = 0; i < disks_all; i++) {
	    fprintf(stderr, " %s", diskstat_current[i].dk_name);
	}
	fprintf(stderr, "\n");
    }
}


void diskstats_print(struct diskinfo *current, struct diskinfo *previous, double elapsed)
{
    FUNCTION_START;
    psub(current->dk_name);
    /* device numbers are not interesting and never change
     * printf("major",      current->dk_major); 
     * printf("minor",      current->dk_minor); 
     */
    pdouble("reads",  (current->dk_reads  - previous->dk_reads) /   elapsed);
    pdouble("rmerge", (current->dk_rmerge - previous->dk_rmerge) /  elapsed);
    pdouble("rkb",    (current->dk_rkb    - previous->dk_rkb) /     elapsed);
    pdouble("rmsec",  (current->dk_rmsec  - previous->dk_rmsec) /   elapsed);

    pdouble("writes", (current->dk_writes  - previous->dk_writes) / elapsed);
    pdouble("wmerge", (current->dk_wmerge  - previous->dk_wmerge) / elapsed);
    pdouble("wkb",    (current->dk_wkb     - previous->dk_wkb) /    elapsed);
    pdouble("wmsec",  (current->dk_wmsec   - previous->dk_wmsec) /  elapsed);

    plong("inflight",  current->dk_inflight);	/* this is a count & not a incremented number */
    pdouble("busy",   (current->dk_time - previous->dk_time) /         elapsed /1000.0 * 100.0); /* convert to percentage */
    pdouble("backlog",(current->dk_backlog - previous->dk_backlog) /   elapsed);
    pdouble("xfers",  (current->dk_xfers   - previous->dk_xfers) /     elapsed);
    plong("bsize",     current->dk_bsize);	/* this is fixed number & not a incremented number */
    /* new statisitcs */
    if(current->dk_count >= 15 && current->dk_count <= 18) {
        pdouble("discards",       (current->dk_discards          - previous->dk_discards) /        elapsed);
        pdouble("discard_merge",  (current->dk_discard_merges     - previous->dk_discard_merges) /   elapsed);
        pdouble("discard_sectors",(current->dk_discard_sectors   - previous->dk_discard_sectors) / elapsed);
        pdouble("discard_busy",   (current->dk_discard_time - previous->dk_discard_time) / elapsed /1000.0 * 100.0); /* convert to percentage */
    }
    if(current->dk_count >= 19 && current->dk_count <= 20) {
    pdouble("flushes",        (current->dk_flushes      - previous->dk_flushes) /      elapsed);
    pdouble("flush_busy",     (current->dk_discard_time - previous->dk_discard_time) / elapsed /1000.0 * 100.0); /* convert to percentage */
    }
    psubend();
}

void proc_diskstats_real(double elapsed)
{
    long i;
    long j;
    long k;

    FUNCTION_START;
    if(real_disks_count <= 0)
	    return;
    psection("disks");
    for (i = 0; i < real_disks_count; i++) {
      for (j = 0; j < disks_all; j++) {
	if( (strcmp(&diskstat_current[j].dk_name[0], real_disks_list[i]) )) {
	    continue; /* Skip disks that we are not looking for */
	}

	/* look for the previous stats for this disk */
	for (k = 0; k < disks_all; k++) {
	  if( (strcmp(&diskstat_previous[k].dk_name[0], real_disks_list[i]) )) {
	    continue; /* Skip disks that we are not looking for */
	  }
	  diskstats_print(&diskstat_current[j], &diskstat_previous[k], elapsed);
	  break;
        }
      }
    }
    psectionend();
}


void proc_diskstats_all(double elapsed)
{
    long i;
    long j;

    FUNCTION_START;
    if(disks_all <= 0)
	    return;
    psection("diskstats");
    for (i = 0; i < disks_all; i++) {
	/* look for the previous stats for this disk */
	for (j = 0; j < disks_all; j++) {
	  if( (strcmp(&diskstat_previous[j].dk_name[0], &diskstat_current[i].dk_name[0]) )) {
	    continue; /* Skip disks that don't match names */
	  }
	  diskstats_print(&diskstat_current[i], &diskstat_previous[j], elapsed);
	  break;
      }
    }
    psectionend();
}


void add_btrfs(int num,char *name)
{
    FUNCTION_START;
    DEBUG fprintf(stderr, "add_btrfs(%d,%s)\n",num,name);
    if(num == 0)
            btrfs_disks_list = (char **)malloc(sizeof(char *));
    else
            btrfs_disks_list = (char **)realloc(btrfs_disks_list, sizeof(char *) * (num+1));
    btrfs_disks_list[num] = (char *)malloc(strlen(name+1));
    strcpy(btrfs_disks_list[num -1], &name[5]);
}

void proc_diskstats_btrfs_init()
{
    /* popen variables */
    FILE *pop;
    char tmpstr[1024 + 1];
    char t1[1024+1];
    char t2[1024+1];
    char t3[1024+1];
    long i;
    long j;
    char *strret;

    FUNCTION_START;
        /* start by just count the number of btrfs disks with a mountpoint */

        /* for testing pop = popen("cat fake_lsblk.out 2>/dev/null", "r"); */
        pop = popen("lsblk -lpo FSTYPE,KNAME,MOUNTPOINT 2>/dev/null", "r");
        if (pop != NULL) {
            DEBUG fprintf(stderr, "btrfs_init popen lsblk != NULL\n");
            /* throw away the headerline */
            tmpstr[0] = 0;
            strret = fgets(tmpstr, 127, pop);

            for (btrfs_disks_count = 0; ; ) {
                tmpstr[0] = 0;
                if (fgets(tmpstr, 127, pop) == NULL)
                    break; /*end of file */
                tmpstr[strlen(tmpstr) - 1] = 0; /* remove newline */

                if( (i = strncmp(tmpstr, "btrfs", 5 )) == 0) {
                    DEBUG fprintf(stderr,"btrfs_init found btrfs %s\n", tmpstr);
                    /* line starts with btrfs - next find out if there is a mountpoint */
                    if ((j = sscanf(tmpstr,"%s %s %s", t1, t2, t3)) == 3){
                        DEBUG fprintf(stderr,"btrfs_init lsblk match(%s, %s, %s)\n",t1,t2,t3);
                        btrfs_disks_count++;
                        add_btrfs(btrfs_disks_count,t2);
                    } /* three fields NOT found */
                } /* "btrfs" NOT found at start of line */
            }
            pclose(pop);
        } else {
            DEBUG fprintf(stderr,"btrfs_init popen lsblk == NULL\n");
            btrfs_disks_count = 0;
            return;
        }
}


void proc_diskstats_btrfs(double elapsed)
{
    long i;
    long j;
    long k;

    FUNCTION_START;
    if(btrfs_disks_count <= 0)
        return;
    psection("btrfs");
    for (i = 0; i < btrfs_disks_count; i++) {
      for (j = 0; j < disks_all; j++) {
	if( (strcmp(&diskstat_current[j].dk_name[0], btrfs_disks_list[i]) )) {
	    continue; /* Skip disks that we are not looking for */
	}

	/* look for the previous stats for this disk */
	for (k = 0; k < disks_all; k++) {
	  if( (strcmp(&diskstat_previous[k].dk_name[0], real_disks_list[i]) )) {
	    continue; /* Skip disks that we are not looking for */
	  }
	  diskstats_print(&diskstat_current[j], &diskstat_previous[k], elapsed);
        }
      }
    }
    psectionend();
}

void proc_diskstats_resync()
{
int i;
    FUNCTION_START;
    /* real = not partitions and all the other rubbish in /proc/disksstats */
	for(i=0;i<real_disks_count;i++) {
	    if(real_disks_list[i] != 0)
	        free(real_disks_list[i]);
	}
	real_disks_count = 0;
	if(real_disks_list != 0)
	    free(real_disks_list);
	real_disks_list = 0;

    /* current/previous all the stats from /proc/disksstats */
	if(diskstat_current != 0)
	    free(diskstat_current);

	if(diskstat_previous != 0)
	    free(diskstat_previous);

	disks_all = 0;

	if(diskstat_fp != 0)
	    fclose(diskstat_fp);

    /* btrfs only devices */
	for(i=0;i<btrfs_disks_count;i++) {
	    if(btrfs_disks_list[i] != 0)
	        free(btrfs_disks_list[i]);
	}
	btrfs_disks_count = 0;
	if(btrfs_disks_list != 0)
	    free(btrfs_disks_list);
	btrfs_disks_list = 0;

	diskstats_resync = 0;
}

/*
FILE * swaps_fp = NULL;

void proc_swaps_init()
{
    if ((swaps_fp = fopen("/proc/swaps", "r")) == NULL) {
            nwarning("failed to open - /proc/swaps");
            return;
        }
}
*/

void proc_swaps()
{
    char buf[1024];
    char filename[1024];
    char type[1024];
    long long size;
    long long used;
    long long priority;
    long count;
    FILE * swaps_fp = NULL;
    char *strret;

    FUNCTION_START;
    if ((swaps_fp = fopen("/proc/swaps", "r")) == NULL) {
            nwarning("failed to open - /proc/swaps");
            return;
        }
    /*
    if (swaps_fp == NULL)
            return;

    rewind(swaps_fp);
    */
    /* EXAMPLE
    Filename    Type            Size    Used    Priority
    /dev/dm-5   partition       4202432 3517760 -2
    */

    strret = fgets(buf, 1024, swaps_fp); /* throw away the header line */

    psection("swaps");
    while (fgets(buf, 1024, swaps_fp) != NULL) {
        count = sscanf(buf, "%s %s %lld %lld %lld\n",
                filename,
                type,
                &size,
                &used,
                &priority);

        if( count == 5) {
                DEBUG printf("SWAPS: in %sSWAPS:out%s %s %lld %lld %lld\n", buf, filename, type, size, used, priority);
                psub(filename);
                pstring("type", type);
                plong("size", size);
                plong("used", used);
                plong("priority", priority);
                psubend();
        }
    }
    psectionend();
    fclose(swaps_fp);
}


void strip_spaces(char *s)
{
    char *p;
    int spaced = 1;

    p = s;
    for (p = s; *p != 0; p++) {
	if (*p == ':')
	    *p = ' ';
	if (*p != ' ') {
	    *s = *p;
	    s++;
	    spaced = 0;
	} else if (spaced) {
	    /* do no thing as this is second space */
	} else {
	    *s = *p;
	    s++;
	    spaced = 1;
	}

    }
    *s = 0;
}

void proc_net_dev(double elapsed, int print)
{
    struct netinfo {
	char if_name[128];
	long long if_ibytes;
	long long if_ipackets;
	long long if_ierrs;
	long long if_idrop;
	long long if_ififo;
	long long if_iframe;
	long long if_obytes;
	long long if_opackets;
	long long if_oerrs;
	long long if_odrop;
	long long if_ofifo;
	long long if_ocolls;
	long long if_ocarrier;
    };
    static struct netinfo current;
    static struct netinfo *previous = NULL;
    long long junk;

    static FILE *fp = 0;
    char buf[1024];
    static long interfaces = 0;
    int ret;
    long i;

    FUNCTION_START;
    if (fp == (FILE *) 0) {
	if ((fp = fopen("/proc/net/dev", "r")) == NULL) {
	    nwarning("failed to open - /proc/net/dev");
	    return;
	}
    } else
	rewind(fp);

    if (fgets(buf, 1024, fp) == NULL)
	return;			/* throw away the header line */
    if (fgets(buf, 1024, fp) == NULL)
	return;			/* throw away the header line */

    if (print)
	psection("networks");
    while (fgets(buf, 1024, fp) != NULL) {
	strip_spaces(buf);
	bzero(&current, sizeof(struct netinfo));
	ret = sscanf(&buf[0],
		     "%s %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
		     (char *) current.if_name,
		     &current.if_ibytes,
		     &current.if_ipackets,
		     &current.if_ierrs,
		     &current.if_idrop,
		     &current.if_ififo,
		     &current.if_iframe,
		     &junk,
		     &junk,
		     &current.if_obytes,
		     &current.if_opackets,
		     &current.if_oerrs,
		     &current.if_odrop,
		     &current.if_ofifo,
		     &current.if_ocolls, &current.if_ocarrier);
	if (ret == 16) {
	    for (i = 0; i < interfaces; i++) {
		if (!strcmp(current.if_name, previous[i].if_name)) {
		    if (print) {
			psub(current.if_name);
			pdouble("ibytes",
				(current.if_ibytes -
				 previous[i].if_ibytes) / elapsed);
			pdouble("ipackets",
				(current.if_ipackets -
				 previous[i].if_ipackets) / elapsed);
			pdouble("ierrs",
				(current.if_ierrs -
				 previous[i].if_ierrs) / elapsed);
			pdouble("idrop",
				(current.if_idrop -
				 previous[i].if_idrop) / elapsed);
			pdouble("ififo",
				(current.if_ififo -
				 previous[i].if_ififo) / elapsed);
			pdouble("iframe",
				(current.if_iframe -
				 previous[i].if_iframe) / elapsed);

			pdouble("obytes",
				(current.if_obytes -
				 previous[i].if_obytes) / elapsed);
			pdouble("opackets",
				(current.if_opackets -
				 previous[i].if_opackets) / elapsed);
			pdouble("oerrs",
				(current.if_oerrs -
				 previous[i].if_oerrs) / elapsed);
			pdouble("odrop",
				(current.if_odrop -
				 previous[i].if_odrop) / elapsed);
			pdouble("ofifo",
				(current.if_ofifo -
				 previous[i].if_ofifo) / elapsed);

			pdouble("ocolls",
				(current.if_ocolls -
				 previous[i].if_ocolls) / elapsed);
			pdouble("ocarrier",
				(current.if_ocarrier -
				 previous[i].if_ocarrier) / elapsed);
			psubend();
		    }
		    memcpy(&previous[i], &current, sizeof(struct netinfo));
		    break;	/* once found stop searching */
		}
	    }
	    if (i == interfaces) {	/* no match means we have a new one (if it is active) */
		if (current.if_ibytes + current.if_obytes > 0) {
		    interfaces++;
		    previous =
			realloc(previous,
				sizeof(struct netinfo) * interfaces);
		    memcpy(&previous[interfaces - 1], &current,
			   sizeof(struct netinfo));
		}
	    }
	} else {
	    DEBUG fprintf(stderr, "net sscanf wanted 16 returned = %d line=%s\n", ret, (char *) buf);
	}
    }
    if (print)
	psectionend();
}

char *clean_string(char *s)
{
    char buffer[256];
    int i;

    while (s[0] == '"' || s[0] == ' ') {	/* remove starting double quotes or spaces */
	strcpy(buffer, s);
	strcpy(s, &buffer[1]);
    }
    for (i = 0; i < strlen(s); i++)	/* change double quotes to space */
	if (s[i] == '"')
	    s[i] = ' ';
    while (s[strlen(s) - 1] == ' ')	/* strip off trailing spaces */
	s[strlen(s) - 1] = 0;
    return s;
}

void etc_os_release()
{
    static FILE *fp = 0;
    static int firsttime = 1;
    static char os_name[256] = "unknown";
    static char os_version[1024] = "unknown";
    static char os_version_id[256] = "unknown";
    static char os_pretty[256] = "unknown";
    char buf[1024 + 1];
    char relname[256];
    int i;

    FUNCTION_START;
    if (firsttime) {
	if ((fp = fopen("/etc/os-release", "r")) != NULL) {
	    while (fgets(buf, 1024, fp) != NULL) {
		buf[strlen(buf) - 1] = 0;	/* remove end newline */
		for (i = 0; i < strlen(buf); i++)
		    if (buf[i] == '"' || buf[i] == '\'')
			buf[i] = ' ';	/* replace with spaces all double and single quotes */

		if (!strncmp(buf, "NAME=", strlen("NAME="))) {
		    strncpy(os_name, &buf[strlen("NAME=") + 1], 255);
		}
		if (!strncmp(buf, "VERSION=", strlen("VERSION="))) {
		    strncpy(os_version, &buf[strlen("VERSION=") + 1], 255);
		}
		if (!strncmp(buf, "PRETTY_NAME=", strlen("PRETTY_NAME="))) {
		    strncpy(os_pretty, &buf[strlen("PRETTY_NAME=") + 1], 255);
		}
		if (!strncmp(buf, "VERSION_ID=", strlen("VERSION_ID="))) {
		    strncpy(os_version_id, &buf[strlen("VERSION_ID=") + 1], 255);
		}
	    }
	    fclose(fp);
	} else {
	    if ((fp = fopen("/etc/redhat-release", "r")) != NULL) {
		/* Example of this file is
		 * Red Hat Enterprise Linux Server release 7.5 (Maipo)
		 */
		if (fgets(buf, 1024, fp) != NULL) {
		    buf[strlen(buf) - 1] = 0;	/* remove newline */
		    for (i = 0; i < strlen(buf); i++)
			if (buf[i] == '"' || buf[i] == '\'')
			    buf[i] = ' ';	/* replace with spaces all double and single quotes */

		    strncpy(os_pretty, buf, 255);
		    if (!strncmp(buf, "Fedora", strlen("Fedora"))) {	/* guessing the name */
			strcpy(os_name, "Fedora");
		    }
		    if (!strncmp(buf, "Centos", strlen("Centos"))) {	/* guessing the name */
			strcpy(os_name, "Centos");
		    }
		    if (!strncmp(buf, "Red Hat", strlen("Red Hat"))) {	/* guessing the name */
			strcpy(os_name, "Red Hat");
		    }
		    if (!strncmp
			(buf, "Red Hat Enterprise Linux Server",
			 strlen("Red Hat Enterprise Linux Server"))) {
			strcpy(os_name, "Red Hat Enterprise Linux");

			sscanf(buf,
			       "Red Hat Enterprise Linux Server release %s (%s)",
			       os_version_id, relname);
			sprintf(os_version, "%s (%s)", os_version_id, relname);
		    }
		}
		fclose(fp);
	    }
	}
	firsttime = 0;
    }

    psection("os_release");
    pstring("name", clean_string(os_name));
    pstring("version", clean_string(os_version));
    pstring("pretty_name", clean_string(os_pretty));
    pstring("version_id", clean_string(os_version_id));
    psectionend();
}

void proc_version()
{
    static FILE *fp = 0;
    char buf[1024 + 1];
    int i;

    FUNCTION_START;
    if (fp == 0) {
	if ((fp = fopen("/proc/version", "r")) == NULL) {
	    return;
	}
    } else
	rewind(fp);
    if (fgets(buf, 1024, fp) != NULL) {
	buf[strlen(buf) - 1] = 0;	/* remove newline */
	for (i = 0; i < strlen(buf); i++) {
	    if (buf[i] == '"')
		buf[i] = '|';
	}
	psection("proc_version");
	pstring("version", buf);
	psectionend();
    }
}

void lscpu()
{
    FILE *pop = 0;
    int data_col = 21;
    int len = 0;
    char buf[1024 + 1];

    FUNCTION_START;
    if ((pop = popen("/usr/bin/lscpu", "r")) == NULL)
	return;

    buf[0] = 0;
    psection("lscpu");
    while (fgets(buf, 1024, pop) != NULL) {
	buf[strlen(buf) - 1] = 0;	/* remove newline */
	if (!strncmp("Architecture:", buf, strlen("Architecture:"))) {
	    if (!strncmp("ppc64", &buf[data_col], strlen("ppc64")))
		ispower++;
	    len = strlen(buf);
	    for (data_col = 14; data_col < len; data_col++) {
		if (isalnum(buf[data_col]))
		    break;
	    }
	    pstring("architecture", &buf[data_col]);
	}
	if (!strncmp("Byte Order:", buf, strlen("Byte Order:"))) {
	    pstring("byte_order", &buf[data_col]);
	}
	if (!strncmp("CPU(s):", buf, strlen("CPU(s):"))) {
	    pstring("cpus", &buf[data_col]);
	}
	if (!strncmp
	    ("On-line CPU(s) list:", buf,
	     strlen("On-line CPU(s) list:"))) {
	    pstring("online_cpu_list", &buf[data_col]);
	}
	if (!strncmp
	    ("Off-line CPU(s) list:", buf,
	     strlen("Off-line CPU(s) list:"))) {
	    pstring("online_cpu_list", &buf[data_col]);
	}
	if (!strncmp("Model:", buf, strlen("Model:"))) {
	    pstring("model", &buf[data_col]);
	}
	if (!strncmp("Model name:", buf, strlen("Model name:"))) {
	    pstring("model_name", &buf[data_col]);
	}
	if (!strncmp
	    ("Thread(s) per core:", buf, strlen("Thread(s) per core:"))) {
	    pstring("threads_per_core", &buf[data_col]);
	}
	if (!strncmp
	    ("Core(s) per socket:", buf, strlen("Core(s) per socket:"))) {
	    pstring("cores_per_socket", &buf[data_col]);
	}
	if (!strncmp("Socket(s):", buf, strlen("Socket(s):"))) {
	    pstring("sockets", &buf[data_col]);
	}
	if (!strncmp("NUMA node(s):", buf, strlen("NUMA node(s):"))) {
	    pstring("numa_nodes", &buf[data_col]);
	}
	if (!strncmp("CPU MHz:", buf, strlen("CPU MHz:"))) {
	    pstring("cpu_mhz", &buf[data_col]);
	}
	if (!strncmp("CPU max MHz:", buf, strlen("CPU max MHz:"))) {
	    pstring("cpu_max_mhz", &buf[data_col]);
	}
	if (!strncmp("CPU min MHz:", buf, strlen("CPU min MHz:"))) {
	    pstring("cpu_min_mhz", &buf[data_col]);
	}
	/* Intel only */
	if (!strncmp("BogoMIPS:", buf, strlen("BogoMIPS:"))) {
	    pstring("bogomips", &buf[data_col]);
	}
	if (!strncmp("Vendor ID:", buf, strlen("Vendor ID:"))) {
	    pstring("vendor_id", &buf[data_col]);
	}
	if (!strncmp("CPU family:", buf, strlen("CPU family:"))) {
	    pstring("cpu_family", &buf[data_col]);
	}
	if (!strncmp("Stepping:", buf, strlen("Stepping:"))) {
	    pstring("stepping", &buf[data_col]);
	}
    }
    psectionend();
    pclose(pop);
}

void replaces(char *str, char *orig, char *rep)
{
  char *p;

  if(!(p = strstr(str, orig)))
    return;

  *p = 0;
  strcpy(p, rep);
  strcpy(p + strlen(rep), p + strlen(orig));
}

void uptime()
{
    FILE *pop;
    char string[256], *s;
    int i;;
    int days = 0;
    int hours = 0;
    int mins = 0;
    int users = 0;
    int good = 0;
    /* this acient command uses lots of "fun" output varitations
       $ uptime 
       06:56PM   up 49 mins,  1 user,  load average: 0.70, 0.75, 0.70
       08:37PM   up   2:30,  3 user,  load average: 1.23, 1.12, 1.01
       06:08PM   up 212 days,   48 min,  9 user,  load average: 3.28, 2.52, 2.29
       06:08PM   up 212 days,   48 mins,  9 user,  load average: 3.28, 2.52, 2.29
       06:08PM   up 212 days,   6:48,  9 user,  load average: 3.28, 2.52, 2.29
       08:07AM   up 14 hrs,  1 user,  load average: 4.17, 3.54, 2.58
     */
    FUNCTION_START;

    if ((pop = popen("/usr/bin/uptime 2>/dev/null", "r")) != NULL) {
	if (fgets(string, 256, pop) != NULL) {
	    for (i = 0; i < strlen(string); i++) {	/* remove commas & newline */
		if (string[i] == ',')
		    string[i] = ' ';
		if (string[i] == '\n')
		    string[i] = ' ';
	    }
	    s = &string[12];	/* remove the time, AM|PM and up */

            /* remove plurals */
            replaces(s, "days",  "day");
            replaces(s, "hours", "hour");
            replaces(s, "hrs",   "hr");
            replaces(s, "mins",  "min");

	    /* day H:M + user */
	    days = hours = mins = users = 0;
	    if (sscanf (s, "%d day %d:%d %d user", &days, &hours, &mins, &users) == 4) {
		good = 1;
	    } else {
		/* H:M + user */
		days = hours = mins = users = 0;
		if (sscanf(s, "%d:%d %d user", &hours, &mins, &users) == 3) {
		    good = 1;
		} else {
		    /* day + min + user */
		    days = hours = mins = users = 0;
		    if (sscanf (s, "%d day %d min %d user", &days, &mins, &users) == 3) {
			good = 1;
		    } else {
			/* day + hr + user */
			days = hours = mins = users = 0;
			if (sscanf (s, "%d day %d hr %d user", &days, &hours, &users) == 3) {
			    good = 1;
			} else {
			    /* min + user */
			    days = hours = mins = users = 0;
			    if (sscanf(s, "%d min %d user", &mins, &users) == 2) {
				good = 1;
			    } else {
				/* hr + user */
				days = hours = mins = users = 0;
				if (sscanf (s, "%d hrs %d user", &hours, &users) == 2) {
				    good = 1;
				} else {
				    /* day + user */
				    days = hours = mins = users = 0;
				    if (sscanf (s, "%d day %d user", &days, &users) == 2) { 
					good = 1;
				    }
				}
			    }
			}
		    }
		}
	    }
	    if (good) {
		psection("uptime");
		plong("days", days);
		plong("hours", hours);
		plong("minutes", mins);
		plong("users", users);
		psectionend();
	    } else {
		psection("uptime_output");
		pstring("output", string);
		psectionend();
	    }
	}
	pclose(pop);
    }
}

#define FS_MOUNTPOINT 1
#define FS_NAME	2

void filesystems(int mode)
{
    FILE *fp;
    struct mntent *fs;
    struct statfs vfs;
    char strtmp[1024];
    char *subvolptr;

    FUNCTION_START;
    if ((fp = setmntent("/etc/mtab", "r")) == NULL) { /* check read access */
	nwarning("setmntent(\"/etc/mtab\", \"r\") failed");
	return;
    }

    psection("filesystems");
    while ((fs = getmntent(fp)) != NULL) { /* get the next mount point entry */
	if (fs->mnt_fsname[0] == '/') {
	    if (strncmp(fs->mnt_type, "autofs", 6) == 0) {	/* skip autofs filesystems, they don't have I/O stats */
		sprintf(errorbuf, "%s: ignoring autofs mount\n",
			fs->mnt_dir);
		nwarning(errorbuf);	/* this returns */
		continue;
	    }

	    if (statfs(fs->mnt_dir, &vfs) != 0) { /* get the filesystem details */
		sprintf(errorbuf, "statfs() failed on: %s\n", fs->mnt_dir);
		nwarning(errorbuf);	/* this returns */
		continue;
	    }

	    /*printf("%s, mounted on %s:\n", fs->mnt_dir, fs->mnt_fsname); */
	    /* TEST TEST TEST 
	    if(strcmp(fs->mnt_type, "btrfs") || !strcmp(fs->mnt_fsname, "sr0")) {
	    */
	    if(strcmp(fs->mnt_type, "btrfs") ) {
		/* not btrfs */
		if(mode == FS_MOUNTPOINT)
			psub(fs->mnt_dir); /* mount point / /boot /home  */
		else
			psub(fs->mnt_fsname); /* fs name /dev/mappr/rhel-root etc */
	    } else {
		/* btrfs as multiple file systems mountins in a single mapper device */
		subvolptr = strstr(fs->mnt_opts,"subvol=");
		if(subvolptr == NULL)
			sprintf(strtmp, "%s",fs->mnt_fsname);
		else
			sprintf(strtmp, "%s[%s]",fs->mnt_fsname,&subvolptr[strlen("subvol=")]);
		psub(strtmp);
	    }
	    pstring("fs_dir",  fs->mnt_dir);
	    pstring("fs_type", fs->mnt_type);
	    pstring("fs_opts", fs->mnt_opts);

	    plong("fs_freqs",  fs->mnt_freq);
	    plong("fs_passno", fs->mnt_passno);
	    plong("fs_bsize",  vfs.f_bsize);
	    plong("fs_blocks", vfs.f_blocks);
	    plong("fs_bfree",  vfs.f_bfree);
	    plong("fs_bavail", vfs.f_bavail);
	    plong("fs_size_mb",
		  (vfs.f_blocks * vfs.f_bsize) / 1024 / 1024);
	    plong("fs_free_mb", (vfs.f_bfree * vfs.f_bsize) / 1024 / 1024);
	    plong("fs_used_mb",
		  (vfs.f_blocks * vfs.f_bsize) / 1024 / 1024 -
		  (vfs.f_bfree * vfs.f_bsize) / 1024 / 1024);
	    if (vfs.f_blocks > 0)
		pdouble("fs_full_percent",
			((double) vfs.f_blocks -
			 (double) vfs.f_bfree) / (double) vfs.f_blocks *
			(double) 100.0);
	    plong("fs_avail",     (vfs.f_bavail * vfs.f_bsize) / 1024 / 1024);
	    plong("fs_files",      vfs.f_files);
	    plong("fs_files_free", vfs.f_ffree);
	    plong("fs_namelength", vfs.f_namelen);
	    psubend();
	}
    }
    psectionend();
    endmntent(fp);
}

long power_timebase = 0;
long power_nominal_mhz = 0;

void proc_cpuinfo(int reduced_stats)
{
    static FILE *fp = 0;
    char buf[4 * 4024 + 1];
    char string[4 * 1024 + 1];
    double value;
    int int_val;
    int processor = 0;

    FUNCTION_START;

#ifdef MAINFRAME
#define MCOL 18
    if (fp == 0) {
	if ((fp = fopen("/proc/cpuinfo", "r")) == NULL) {
	    return;
	}
    } else
	rewind(fp);
    psection("cpuinfo");
    while (fgets(buf, 1024, fp) != NULL) {
	buf[strlen(buf) - 1] = 0;	/* remove newline */
	if (!strncmp("vendor_id", buf, strlen("vendor_id"))) {
	    pstring("vendor_id", &buf[MCOL]);
	}
	if (!strncmp("# processors", buf, strlen("# processors"))) {
	    processor = atol(&buf[MCOL]);
	    plong("processors", processor);
	}
	if (!strncmp("bogomips per cpu", buf, strlen("bogomips per cpu"))) {
	    bogo = atol(&buf[MCOL]);
	    plong("bogomips_per_cpu", bogo);
	}
        if (!strncmp("processor ", buf, strlen("processor "))) {
            ret = sscanf(buf, "%s %s %s %s %s %s %s %s", label, label, label, label, label, label, label, serialno);
	    if(ret == 8 && serialno_done == 0) {
      	        for(i=0;i<strlen(serialno);i++)
	    	    if(serialno[i] == ',') 
		        serialno[i] = 0;
      		pstring("serialno", serialno);
		serialno_done++;
		tag_set(tag_sn,serialno);
	    }
        }
    }
    psectionend();
#else /* NON-MAINFRAME */
    if (fp == 0) {
	if ((fp = fopen("/proc/cpuinfo", "r")) == NULL) {
	    return;
	}
    } else
	rewind(fp);

    psection("cpuinfo");
    while (fgets(buf, 1024, fp) != NULL) {
	buf[strlen(buf) - 1] = 0;	/* remove newline */
	/* moronically cpuinfo file format has Tab characters ! */
      if(!reduced_stats) {
	if (!strncmp("processor", buf, strlen("processor"))) {
	    if (processor != 0)
		psubend();
	    sscanf(&buf[12], "%d", &int_val);
	    sprintf(string, "proc%d", processor);
	    psub(string);
	    processor++;
	}
	if (!strncmp("clock", buf, strlen("clock"))) {	/* POWER ONLY */
	    sscanf(&buf[9], "%lf", &value);
	    pdouble("mhz_clock", value);
	    power_nominal_mhz = value;	/* save for sys_device_system_cpu() */
	}
	if (!strncmp("vendor_id", buf, strlen("vendor_id"))) {
	    pstring("vendor_id", &buf[12]);
	}
	if (!strncmp("cpu MHz", buf, strlen("cpu MHz"))) {
	    sscanf(&buf[11], "%lf", &value);
	    pdouble("cpu_mhz", value);
	}
	if (!strncmp("cache size", buf, strlen("cache size"))) {
	    sscanf(&buf[13], "%lf", &value);
	    pdouble("cache_size", value);
	}
	if (!strncmp("physical id", buf, strlen("physical id"))) {
	    sscanf(&buf[14], "%d", &int_val);
	    plong("physical_id", int_val);
	}
	if (!strncmp("siblings", buf, strlen("siblings"))) {
	    sscanf(&buf[11], "%d", &int_val);
	    plong("siblings", int_val);
	}
	if (!strncmp("core id", buf, strlen("core id"))) {
	    sscanf(&buf[10], "%d", &int_val);
	    plong("core_id", int_val);
	}
	if (!strncmp("cpu cores", buf, strlen("cpu cores"))) {
	    sscanf(&buf[12], "%d", &int_val);
	    plong("cpu_cores", int_val);
	}
	if (!strncmp("model name", buf, strlen("model name"))) {
	    pstring("model_name", &buf[13]);
	}
	if (!strncmp("timebase", buf, strlen("timebase"))) {	/* POWER only */
	    break;
	}
      }
    }
    if (processor != 0)
	psubend();
    psectionend(); /* section cpuinfo */

    if (ispower) {
	psection("cpuinfo_power");
	if (!strncmp("timebase", buf, strlen("timebase"))) {	/* POWER only */
	    pstring("timebase", &buf[11]);
	    power_timebase = atol(&buf[11]);
	    plong("power_timebase", power_timebase);
	}
	while (fgets(buf, 1024, fp) != NULL) {
	    buf[strlen(buf) - 1] = 0;	/* remove newline */
	    if (!strncmp("platform", buf, strlen("platform"))) {	/* POWER only */
		pstring("platform", &buf[11]);
	    }
	    if (!strncmp("model", buf, strlen("model"))) {
		pstring("model", &buf[9]);
	    }
	    if (!strncmp("machine", buf, strlen("machine"))) {
		pstring("machine", &buf[11]);
	    }
	    if (!strncmp("firmware", buf, strlen("firmware"))) {
		pstring("firmware", &buf[11]);
	    }
	}
	psectionend();
    }
#endif /* NON-MAINFRAME */
}


void proc_loadavg()
{
    char buf[1024];
    double min1 = 111.0;
    double min5 = 222.0;
    double min15 = 333.0;
    long long runable = 444;
    long long schedulable = 555;
    long long last_started_pid = 666;
    long count;
    FILE * loadavg_fp = NULL;

    FUNCTION_START;
    /* 0.47 0.29 0.25 1/1344 2084349 */

    if ((loadavg_fp = fopen("/proc/loadavg", "r")) == NULL) {
	    nwarning("failed to open - /proc/loadavg");
	    return;
	}

    if (fgets(buf, 1024, loadavg_fp) != NULL) {
	count = sscanf(buf, "%lf %lf %lf %lld/%lld %lld",
		&min1,
		&min5,
		&min15,
		&runable,
		&schedulable,
		&last_started_pid);

	if( count == 6) {
		DEBUG printf("LOADAVG: in %sLOADAVG: out%lf %lf %lf %lld %lld %lld\n",
		buf, min1, min5, min15, runable, schedulable, last_started_pid);

		psection("loadavg");
		pdouble("loadavg_min1", min1);
		pdouble("loadavg_min5", min5);
		pdouble("loadavg_min15", min15);
		plong("runable", runable);
		plong("schedulable", schedulable);
		plong("last_started_pid", last_started_pid);
		psectionend();
	}
    }
    fclose(loadavg_fp);
}


/* Call this function AFTER proc_cpuinfo as it needs numbers from it */
void sys_device_system_cpu(double elapsed, int print)
{
    FILE *fp = 0;
    char filename[1024];
    char line[1024];
    int i;
    int finished = 0;

    double sdelta;
    double pdelta;
    double overclock;
    long long spurr_total;
    long long purr_total;

    static int switch_off = 0;
    static long long purr_saved = 0;
    static long long spurr_saved = 0;

    FUNCTION_START;
    if (switch_off) {
	DEBUG fprintf(stderr, "DEBUG: sys_device_system_cpu switched_off\n");
	return;
    }

    if (lparcfg_found == 0) {
	DEBUG fprintf(stderr, "DEBUG: lparcfg_found == 0\n");
	return;
    }

    spurr_total = 0;
    purr_total = 0;

    for (i = 0, finished = 0; finished == 0; i++) {
	    /* 1024 just a sanity check number */
	sprintf(filename, "/sys/devices/system/cpu/cpu%d/spurr", i);
	if ((fp = fopen(filename, "r")) == NULL) {
	    if (i == 0) {	/* failed on the 1st attempt then no spurr file = never try again */
		DEBUG fprintf(stderr, "DEBUG: sys_device_system_cpu switched_off = not present or not root user 33\n");
		switch_off = 1;
	    }
	    finished = 1;
	    break;
	}
	DEBUG fprintf(stderr, "spurr file \"%s\" found\n", filename);
	if (fgets(line, 1000, fp) != NULL) {
	    line[strlen(line) - 1] = 0;
	    DEBUG fprintf(stderr, "spurr read \"%s\"\n", line);
	    spurr_total += strtoll(line, NULL, 16);
	} else {
	    DEBUG fprintf(stderr, "spurr read failed\n");
	    finished = 1;
	}
	fclose(fp);

	sprintf(filename, "/sys/devices/system/cpu/cpu%d/purr", i);
	if ((fp = fopen(filename, "r")) == NULL) {
	    DEBUG fprintf(stderr, "purr opened failed\n");
	    if (i == 0) {	/* failed on the 1st attempt then no purr file = never try again */
		DEBUG fprintf(stderr, "DEBUG: sys_device_system_cpu switched_off 44\n");
		switch_off = 1;
	    }
	    finished = 1;
	    break;
	}
	if (fgets(line, 1000, fp) != NULL) {
	    line[strlen(line) - 1] = 0;
	    DEBUG fprintf(stderr, "purr read \"%s\"\n", line);
	    purr_total += strtoll(line, NULL, 16);
	} else {
	    DEBUG fprintf(stderr, "purr read failed\n");
	    finished = 1;
	}
	fclose(fp);
    }

    if (print == PRINT_FALSE) {
	DEBUG fprintf(stderr, "DEBUG: PRINT_FALSE\n");
	purr_saved = purr_total;
	spurr_saved = spurr_total;
	return;
    }

    if (purr_total == 0 || spurr_total == 0 || power_timebase == 0) {
	DEBUG fprintf(stderr, "DEBUG: purr, spur or timebase is zero\n");
	switch_off = 1;
	return;
    } else {
	psection("sys_dev_sys_cpu");

	pdelta = (double) (purr_total -
		      purr_saved) / (double) power_timebase / elapsed;
	purr_saved = purr_total;
	pdouble("purr", pdelta);

	sdelta = (double) (spurr_total -
		      spurr_saved) / (double) power_timebase / elapsed;
	spurr_saved = spurr_total;
	pdouble("spurr", sdelta);

	overclock = (double) sdelta / (double) pdelta;
	pdouble("nsp", overclock * 100.0);
	pdouble("nominal_mhz", (double) power_nominal_mhz);
	pdouble("current_mhz", (double) power_nominal_mhz * overclock);
	psectionend();
    }
}

void file_read_one_stat(char *file, char *name)
{
    FILE *fp;
    char buf[1024 + 1];

    if ((fp = fopen(file, "r")) != NULL) {
	if (fgets(buf, 1024, fp) != NULL) {
	    if (buf[strlen(buf) - 1] == '\n')	/* remove last char = newline */
		buf[strlen(buf) - 1] = 0;
	    pstring(name, buf);
	}
	fclose(fp);
    }
}

void identity(char *command, char *version)
{
    int i;

    /* hostname */
    static struct addrinfo hints;
    struct addrinfo *info;
    struct addrinfo *p;

    /* user name and id */
    struct passwd *pw;

    /* network IP addresses */
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *ifaddrs_ptr = NULL;
    char address_buf[INET6_ADDRSTRLEN];
    char *str;
    char name_save[128];
    char extra_letter = 'b';
    char label[1024];

    FUNCTION_START;
    psection("identity");

    /* check if the user forced a "special" hostname */
    if(alias_hostname[0] != 0 ) {
        pstring("hostname", alias_hostname);
        pstring("fullhostname", alias_hostname);
    } else {
        if(fullhostname_tag) 
            pstring("hostname", fullhostname);
        else 
            pstring("hostname", hostname);
        pstring("fullhostname", fullhostname);
    }

    pstring("ipaddress", source_ip);
    pstring("njmon_command", command);
    switch(mode) {
        case NJMON: str = "njmon-JSON"; break;
        case NIMON: str = "nimon-InfluxDB"; break;
        case NSMON: str = "nsmon-Splunk"; break;
        default: str = "none";
    }
    pstring("njmon_mode", str);

    pstring("njmon_version", version);
    if ((pw = getpwuid(uid)) != 0) {
	pstring("username", pw->pw_name);
	plong("userid", uid);
    } else {
	pstring("username", "unknown");
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;	/*either IPV4 or IPV6 */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_CANONNAME;

    if (getaddrinfo(fullhostname, "http", &hints, &info) == 0) {
	for (p = info, i = 1; p != NULL; p = p->ai_next, i++) {
	    sprintf(label, "fullhostname%d", i);
	    if (p->ai_canonname != 0)
		pstring(label, p->ai_canonname);
	}
    }
    freeaddrinfo(info);

    strcpy(name_save, "not-set");
    if (getifaddrs(&interfaces) == 0) {	/* retrieve the current interfaces */
	for (ifaddrs_ptr = interfaces; ifaddrs_ptr != NULL;
	     ifaddrs_ptr = ifaddrs_ptr->ifa_next) {

	    if (ifaddrs_ptr->ifa_addr) {
		switch (ifaddrs_ptr->ifa_addr->sa_family) {
		case AF_INET:
		    if ((str =
			 (char *) inet_ntop(ifaddrs_ptr->ifa_addr->
					    sa_family,
					    &((struct sockaddr_in *)
					      ifaddrs_ptr->ifa_addr)->
					    sin_addr, address_buf,
					    sizeof(address_buf))) !=
			NULL) {
			if (!strcmp(ifaddrs_ptr->ifa_name, name_save))
			    sprintf(label, "%s_IP4_%c",
				    ifaddrs_ptr->ifa_name, extra_letter++);
			else
			    sprintf(label, "%s_IP4",
				    ifaddrs_ptr->ifa_name);
			pstring(label, str);
			strcpy(name_save, ifaddrs_ptr->ifa_name);
		    }
		    break;
		case AF_INET6:
		    if ((str =
			 (char *) inet_ntop(ifaddrs_ptr->ifa_addr->
					    sa_family,
					    &((struct sockaddr_in6 *)
					      ifaddrs_ptr->ifa_addr)->
					    sin6_addr, address_buf,
					    sizeof(address_buf))) !=
			NULL) {
			if (!strcmp(ifaddrs_ptr->ifa_name, name_save))
			    sprintf(label, "%s_IP6_%c",
				    ifaddrs_ptr->ifa_name, extra_letter++);
			else
			    sprintf(label, "%s_IP6",
				    ifaddrs_ptr->ifa_name);
			pstring(label, str);
			strcpy(name_save, ifaddrs_ptr->ifa_name);
		    }
		    break;
		default:
		    DEBUG sprintf(label, "%s_Not_Supported_%d", ifaddrs_ptr->ifa_name, ifaddrs_ptr->ifa_addr->sa_family);
		    break;
		}
	    } else {
		DEBUG sprintf(label, "%s_network_ignored", ifaddrs_ptr->ifa_name);
		DEBUG pstring(label, "null_address");
	    }
	}
	freeifaddrs(interfaces);	/* free the dynamic memory */
    }

    /* POWER and AMD and may be others */
    if (access("/proc/device-tree", R_OK) == 0) {
	file_read_one_stat("/proc/device-tree/compatible", "compatible");
	file_read_one_stat("/proc/device-tree/model", "model");
	file_read_one_stat("/proc/device-tree/part-number", "part-number");
	file_read_one_stat("/proc/device-tree/serial-number",
			   "serial-number");
	file_read_one_stat("/proc/device-tree/system-id", "system-id");
	file_read_one_stat("/proc/device-tree/vendor", "vendor");
    } else {
	/*x86_64 and AMD64 - dmi files requires root user */
	if (access("/sys/devices/virtual/dmi/id/", R_OK) == 0) {
	    file_read_one_stat
		("/sys/devices/virtual/dmi/id/product_serial",
		 "serial-number");
	    file_read_one_stat("/sys/devices/virtual/dmi/id/product_name",
			       "model");
	    file_read_one_stat("/sys/devices/virtual/dmi/id/sys_vendor",
			       "vendor");
	}
    }

    psectionend();
}

/* check_pid_file() and make_pid_file()
 *    If you start njmon and it finds there is a copy running already then it will quitely stop.
 *       You can hourly start njmon via crontab and not end up with dozens of copies runnings.
 *	  It also means if the server reboots then njmon start in the next hour.
 *	      Side-effect: it creates a file called /tmp/njmon.pid
 *	      */
int kill_on = 0;
char * pid_filename = NULL;

void make_pid_file()
{
    int fd;
    int ret;
    char buffer[1024];

    FUNCTION_START;

    if ((fd = creat(pid_filename,
	       O_CREAT | O_WRONLY | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
	       S_IROTH | S_IWOTH)) < 0) {
	sprintf(errorbuf, "make_pid_file() creat() failed filename=%s", pid_filename);
	error(errorbuf);
    }
    DEBUG fprintf(stderr, "make_pid_file write file descriptor=%d\n", fd);
    sprintf(buffer, "%d \n", getpid());
    if ((ret = write(fd, buffer, strlen(buffer))) <= 0) {
	sprintf(errorbuf, "make_pid_file() write to \"%s\" content=\"%s\" ret=%d", pid_filename, buffer, ret);
	error(errorbuf);
    }
    close(fd);
}

void check_pid_file()
{
    char *strptr;
    char *ep;
    char buffer[32];
    int fd;
    pid_t pid;
    int ret;

    FUNCTION_START;
    if(pid_filename == NULL) {
        if(mode == NJMON) 
		strptr = "NJMON_PID_FILE";
	else
		strptr = "NIMON_PID_FILE";
        ep = getenv(strptr);
        if (ep == NULL) {		/* not in the environment, so use default */
	    if(mode == NJMON)
		    pid_filename = "/tmp/njmon.pid";
	    else
		    pid_filename = "/tmp/nimon.pid";
        } else {			/* in the environment but getenv data not safe */
            pid_filename = malloc(strlen(ep) + 1);
            strcpy(pid_filename,ep);
        }
    }
    DEBUG fprintf(stderr, "pid_file=%s\n", pid_filename);

    if( access( pid_filename, F_OK ) != -1 ) {
        printf("file exists\n");
        if ((fd = open(pid_filename, O_RDONLY)) < 0) {
            printf("njmon Warning %s files exists but no access:\n\tWrong owner or no permission.\n", pid_filename);
	    perror("njmon stopping");
            exit(42); 
        }

	if (read(fd, buffer, 31) > 0) {	/* has some data */
	DEBUG fprintf(stderr, "file has some content\n");
	buffer[31] = 0;
	  if (sscanf(buffer, "%d", &pid) == 1) {
	    DEBUG fprintf(stderr, "read a pid from the file OK = %d\n", pid);
	    ret = kill(pid, 0);		/* kiil with signal=0 does not kill the process ever */
	    DEBUG fprintf(stderr, "kill %d, 0) = returned =%d\n", pid, ret);
	    if (ret == 0) {
		DEBUG fprintf(stderr, "We have nimon or njmon running - exit quietly\n");
		exit(99);
	    }
	  }
	}
        /* if we got here there is a file but the content is duff or the process is not running */
        close(fd);
        remove(pid_filename);
    }
}

void    remove_pid_file()
{
    FUNCTION_START;
    if(pid_filename != 0)
        remove(pid_filename);
}

/* --- Top Processes Start --- */
double ignore_threshold = 0.01;	/* percent of a CPU */

/* Lookup the right process state string */
char *get_state(char n)
{
    static char duff[64];
    switch (n) {
    case 'R':
	return "Running";
    case 'S':
	return "Sleeping-interruptible";
    case 'D':
	return "Waiting-uninterruptible";
    case 'Z':
	return "Zombie";
    case 'T':
	return "Stopped";
    case 't':
	return "Tracing";
    case 'W':
	return "Paging-or-Waking";
    case 'X':
	return "Dead";
    case 'x':
	return "dead";
    case 'K':
	return "Wakekill";
    case 'P':
	return "Parked";
    default:
	snprintf(duff, 64, "State=%d(%c)", n, n);
	return duff;
    }
}

struct procsinfo {
    /* Process owner */
    uid_t uid;
    char username[64];
    /* Process details */
    int pi_pid;
    char pi_comm[64];
    char pi_state;
    int pi_ppid;
    int pi_pgrp;
    int pi_session;
    int pi_tty_nr;
    int pi_tty_pgrp;
    unsigned long pi_flags;
    unsigned long pi_minflt;
    unsigned long pi_child_min_flt;
    unsigned long pi_majflt;
    unsigned long pi_child_maj_flt;
    unsigned long pi_utime;
    unsigned long pi_stime;
    long pi_child_utime;
    long pi_child_stime;
    long pi_priority;
    long pi_nice;
    long pi_num_threads;
    long pi_it_real_value;
    unsigned long pi_start_time;
    unsigned long pi_vsize;
    long pi_rss;
    unsigned long pi_rsslimit;
    unsigned long pi_start_code;
    unsigned long pi_end_code;
    unsigned long pi_start_stack;
    unsigned long pi_esp;
    unsigned long pi_eip;
    /* The signal information here is obsolete. See "man proc" */
    unsigned long pi_signal_pending;
    unsigned long pi_signal_blocked;
    unsigned long pi_signal_ignore;
    unsigned long pi_signal_catch;
    unsigned long pi_wchan;
    unsigned long pi_swap_pages;
    unsigned long pi_child_swap_pages;
    int pi_signal_exit;
    int pi_last_cpu;
    unsigned long pi_realtime_priority;
    unsigned long pi_sched_policy;
    unsigned long long pi_delayacct_blkio_ticks;
    /* Process stats for memory */
    unsigned long statm_size;	/* total program size */
    unsigned long statm_resident;	/* resident set size */
    unsigned long statm_share;	/* shared pages */
    unsigned long statm_trs;	/* text (code) */
    unsigned long statm_drs;	/* data/stack */
    unsigned long statm_lrs;	/* library */
    unsigned long statm_dt;	/* dirty pages */

    /* Process stats for disks */
    unsigned long long read_io;	/* storage read bytes */
    unsigned long long write_io;/* storage write bytes */
};

struct data {
    struct procsinfo *procs;
    long proc_records;
    long processes;
} database[2], *p = &database[0], *q = &database[1], *r;


/* We order this array rather than the actual process tables
 * the index is the position in the process table and
 * the time is the CPU used in the last period in seconds
 */
struct topper {
    int pindex;
    int qindex;
    long time;
} *topper = NULL;
int topper_size = 0;

/* Routine used by qsort to order the processes by CPU usage */
int cpu_compare(const void *a, const void *b)
{
    return (int) (((struct topper *) b)->time -
		  ((struct topper *) a)->time);
}

int proc_procsinfo(int pid, int index)
{
    FILE *fp;
    int fd;
    char filename[64];
    char buf[1024 * 4];
    int size = 0;
    int ret = 0;
    int count = 0;
    int i;
    struct stat statbuf;
    uid_t userid;
    static struct passwd pw;
    static struct passwd *pwpointer;
    static char pwbuf[1024 * 4];

    FUNCTION_START;
    /* the statistic file for the process */
    snprintf(filename, 64, "/proc/%d/stat", pid);

    if ((fd = open(filename, O_RDONLY)) == -1) {
	DEBUG fprintf(stderr, "ERROR: failed to open file %s", filename);
	return 0;
    }
    size = read(fd, buf, 1024);

    if (fstat(fd, &statbuf) != 0) {
	DEBUG fprintf(stderr, "ERROR: failed to stat file %s", filename);
	return 0;
    }

    /* get the user id and user name  using the open file descriptor */
    userid = statbuf.st_uid;
    p->procs[index].uid = userid;
    if (userid == (uid_t) 0) {
	strcpy(p->procs[index].username, "root");
    } else {
	pwpointer = &pw;
	ret = getpwuid_r(userid, &pw, pwbuf, sizeof(pwbuf), &pwpointer);
	if (ret == 0 && pwpointer != 0) {
	    strncpy(p->procs[index].username, pw.pw_name, 63);
	    p->procs[index].username[63] = 0;
	}
    }
    close(fd);

    if (size == -1) {
	DEBUG fprintf(stderr, "ERROR: procsinfo read returned = %d assuming process stopped pid=%d errno=%d\n", ret, pid, errno);
	return 0;
    }
    ret =
	sscanf(buf, "%d (%s)", &p->procs[index].pi_pid,
	       &p->procs[index].pi_comm[0]);
    if (ret != 2) {
	DEBUG fprintf(stderr, "procsinfo sscanf returned = %d line=%s\n", ret, buf);
	return 0;
    }
    p->procs[index].pi_comm[strlen(p->procs[index].pi_comm) - 1] = 0;

    /* now look for ") " as dumb Infiniband driver includes "()" */
    for (count = 0; count < size; count++) {
	if (buf[count] == ')' && buf[count + 1] == ' ')
	    break;
    }
    if (count == size) {
	DEBUG fprintf(stderr, "procsinfo failed to find end of command buf=%s\n", buf);
	return 0;
    }
    count++;
    count++;

    ret = sscanf(&buf[count],
/*		 1  2  3  4  5  6  7   8   9   10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32  33  34  35 36 37  38  39  40 */
		 "%c %d %d %d %d %d %lu %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %lu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %d %d %lu %lu %llu",
		 /* column 1 and 2 handled above */
		 &p->procs[index].pi_state,	/*3 numbers taken from "man proc" */
		 &p->procs[index].pi_ppid,	/*4 */
		 &p->procs[index].pi_pgrp,	/*5 */
		 &p->procs[index].pi_session,	/*6 */
		 &p->procs[index].pi_tty_nr,	/*7 */
		 &p->procs[index].pi_tty_pgrp,	/*8 */
		 &p->procs[index].pi_flags,	/*9 */
		 &p->procs[index].pi_minflt,	/*10 */
		 &p->procs[index].pi_child_min_flt,	/*11 */
		 &p->procs[index].pi_majflt,	/*12 */
		 &p->procs[index].pi_child_maj_flt,	/*13 */
		 &p->procs[index].pi_utime,	/*14 */
		 &p->procs[index].pi_stime,	/*15 */
		 &p->procs[index].pi_child_utime,	/*16 */
		 &p->procs[index].pi_child_stime,	/*18 */
		 &p->procs[index].pi_priority,	/*19 */
		 &p->procs[index].pi_nice,	/*20 */
		 &p->procs[index].pi_num_threads,	/*21 */
		 &p->procs[index].pi_it_real_value,	/*22 */
		 &p->procs[index].pi_start_time,	/*23 */
		 &p->procs[index].pi_vsize,	/*24 */
		 &p->procs[index].pi_rss,	/*25 */
		 &p->procs[index].pi_rsslimit,	/*26 */
		 &p->procs[index].pi_start_code,	/*27 */
		 &p->procs[index].pi_end_code,	/*28 */
		 &p->procs[index].pi_start_stack,	/*29 */
		 &p->procs[index].pi_esp,	/*29 */
		 &p->procs[index].pi_eip,	/*30 */
		 &p->procs[index].pi_signal_pending,	/*31 */
		 &p->procs[index].pi_signal_blocked,	/*32 */
		 &p->procs[index].pi_signal_ignore,	/*33 */
		 &p->procs[index].pi_signal_catch,	/*34 */
		 &p->procs[index].pi_wchan,	/*35 */
		 &p->procs[index].pi_swap_pages,	/*36 */
		 &p->procs[index].pi_child_swap_pages,	/*37 */
		 &p->procs[index].pi_signal_exit,	/*38 */
		 &p->procs[index].pi_last_cpu	/*39 */
		 , &p->procs[index].pi_realtime_priority,	/*40 */
		 &p->procs[index].pi_sched_policy,	/*41 */
		 &p->procs[index].pi_delayacct_blkio_ticks	/*42 */
	);
    if (ret != 40) {
	DEBUG fprintf(stderr, "procsinfo2 sscanf wanted 40 returned = %d pid=%d line=%s\n", ret, pid, buf);
	return 0;
    }
    snprintf(filename, 64, "/proc/%d/statm", pid);
    if ((fd = open(filename, O_RDONLY)) == -1) {
	DEBUG fprintf(stderr, "failed to open file %s", filename);
	return 0;
    }
    size = read(fd, buf, 1024 * 4);
    close(fd);			/* close it even if the read failed, the file could have been removed 
				   between open & read i.e. the device driver does not behave like a file */
    if (size == -1) {
	DEBUG fprintf(stderr, "failed to read file %s", filename);
	return 0;
    }

    ret = sscanf(&buf[0], "%lu %lu %lu %lu %lu %lu %lu",
		 &p->procs[index].statm_size,
		 &p->procs[index].statm_resident,
		 &p->procs[index].statm_share,
		 &p->procs[index].statm_trs,
		 &p->procs[index].statm_lrs,
		 &p->procs[index].statm_drs, &p->procs[index].statm_dt);
    if (ret != 7) {
	DEBUG fprintf(stderr, "sscanf wanted 7 returned = %d line=%s\n", ret,
		    buf);
	return 0;
    }
    if (uid == (uid_t) 0) {
	p->procs[index].read_io = 0;
	p->procs[index].write_io = 0;
	sprintf(filename, "/proc/%d/io", pid);
	if ((fp = fopen(filename, "r")) != NULL) {
	    for (i = 0; i < 6; i++) {
		if (fgets(buf, 1024, fp) == NULL) {
		    break;
		}
		if (strncmp("read_bytes:", buf, 11) == 0)
		    sscanf(&buf[12], "%lld", &p->procs[index].read_io);
		if (strncmp("write_bytes:", buf, 12) == 0)
		    sscanf(&buf[13], "%lld", &p->procs[index].write_io);
	    }
	}

	if (fp != NULL)
	    fclose(fp);
    }
    return 1;
}

int isnumbers(char *s)
{
    while (*s != 0) {
	if (*s < '0' || *s > '9')
	    return 0;
	s++;
    }
    return 1;
}

#define JUST_RETURN_THE_COUNT 0

/* Read directory /proc/<PID> for process information
 *
 * If just counting them the number of directory = processes then return the count.
 *
 * Otherwise for each process call proc_procsinfo() to get the process details
 *  up to the max_records.
 */
int getprocs(int max_records)
{
    struct dirent *dent;
    DIR *procdir;
    int count = 0;

    FUNCTION_START;
    if ((char *) (procdir = opendir("/proc")) == NULL) {
	printf("opendir(/proc) failed");
	return 0;
    }
    while ((char *) (dent = readdir(procdir)) != NULL) {
	if (dent->d_type == 4) {	/* is this a directlory */
	    /* mainframes report 0 = unknown every time !  */
	    if (isnumbers(dent->d_name)) {
		if (max_records != JUST_RETURN_THE_COUNT) {	/* getting the details mode */
		    count += proc_procsinfo(atoi(dent->d_name), count);
		    if (count == max_records) {
			break;
		    }
		} else {	/* just counting the processes mode */
		    count++;
		}
	    }
	}
    }
    closedir(procdir);
    DEBUG printf("GETPROCS returning %d\n", count);
    return count;
}

/* Workout how many prcesses we have and make sure we have enough structures */
void processes_space_manage()
{
    int count;
    int rounded_up;

    FUNCTION_START;
    /* how many processes ? */
    count = getprocs(JUST_RETURN_THE_COUNT);

    DEBUG fprintf(stderr, "++++ processes_space_manage() BEFORE proc_records p=%ld q=%ld processes p=%ld q=%ld count=%d\n",
	     p->proc_records, q->proc_records, p->processes, q->processes, count);
    /* allocate enough data structures plus a few in case  processes start */
    rounded_up = count + 64;
    if (count >= p->proc_records) {
	p->procs =
	    realloc(p->procs, sizeof(struct procsinfo) * rounded_up);
	p->proc_records = rounded_up;
    }
    if (count >= q->proc_records) {
	q->procs =
	    realloc(q->procs, sizeof(struct procsinfo) * rounded_up);
	q->proc_records = rounded_up;
    }
    if (count >= topper_size) {
	topper = realloc(topper, sizeof(struct topper) * rounded_up);
	topper_size = rounded_up;
    }
    DEBUG fprintf(stderr, "++++ processes_space_manage() AFTER proc_records p=%ld q=%ld processes p=%ld q=%ld round_up=%d\n",
	     p->proc_records, q->proc_records, p->processes, q->processes,
	     rounded_up);
}

/* initialise processor data structures */
void processes_init()
{
    FUNCTION_START;
    DEBUG fprintf(stderr, "processes_init\n");

    /* allocate space for process records */
    processes_space_manage();

    /* fill the first set */
    p->processes = getprocs(p->proc_records);
}

#define CURRENT(member) (p->procs[pindex].member)
#define PREVIOUS(member) (q->procs[qindex].member)
#define TIMEDELTA(member) (CURRENT(member) - PREVIOUS(member))
#define COUNTDELTA(member) ( (PREVIOUS(member) > CURRENT(member)) ? 0 : (CURRENT(member) - PREVIOUS(member)) )

/* processes() does the bulk work
 * 1 get the latest process stats
 * 2 build the topper structures of matching previous & current processes matched by pid
 * 3 save data for processes using over the threshold CPU percentage
 */
void processes(double elapsed,int proc_pid)
{
    int pindex = 0;
    int qindex = 0;
    int entry = 0;
    int max_sorted = 0;
    char str[256];
    long cputime;
#define pagesize  (1024 * 4)

    FUNCTION_START;
    /* swap databases note: q is previous and p is current */
    r = q;
    q = p;
    p = r;

    /* recaculate the number of processes */
    processes_space_manage();

    /* get fresh top processes data */
    p->processes = getprocs(p->proc_records);

    /* Sort the processes by CPU utilisation */
    /* 1st find matching pids in both lists */
    for (pindex = 0, max_sorted = 0; pindex < p->processes; pindex++) {
	/* move forward in the previous array to find a match */
	for (qindex = 0; qindex < q->processes; qindex++) {
	    if (p->procs[pindex].pi_pid == q->procs[qindex].pi_pid) {	/* found a match */
		cputime = TIMEDELTA(pi_utime) + TIMEDELTA(pi_stime);
		if ((cputime / elapsed) > ignore_threshold) {
		    /* save only interesting processes (i.e. not near zero cputime) */
		    topper[max_sorted].pindex = pindex;
		    topper[max_sorted].qindex = qindex;
		    topper[max_sorted].time = cputime;
		    max_sorted++;
		}
		break;
	    }
	}
    }
    if (max_sorted <= 0) /* empty list */
	return;

    qsort((void *) &topper[0], max_sorted, sizeof(struct topper),
	      &cpu_compare);

    /* Even if we have no processes create a processors section + end
     * No proceses over the threadold is still valid JSON  - I hope.
     * */
    psection("processes");
    for (entry = 0; entry < max_sorted; entry++) {
	pindex = topper[entry].pindex;
	qindex = topper[entry].qindex;

	if(proc_pid)
	    sprintf(str, "%s_%ld", CURRENT(pi_comm), (long) CURRENT(pi_pid));
	else
	    sprintf(str, "%s", CURRENT(pi_comm));
	psub(str);

	plong("pid", CURRENT(pi_pid));
	/* Full command line can be found /proc/PID/cmdline with zeros in it! */
	pstring("cmd", CURRENT(pi_comm));
	plong("ppid", CURRENT(pi_ppid));
	plong("pgrp", CURRENT(pi_pgrp));
	plong("priority", CURRENT(pi_priority));
	plong("nice", CURRENT(pi_nice));
	plong("session", CURRENT(pi_session));
	plong("tty_nr", CURRENT(pi_tty_nr));
	phex("flags", CURRENT(pi_flags));
	pstring("state", get_state(CURRENT(pi_state)));
	plong("threads", CURRENT(pi_num_threads));
	pdouble("cpu_percent", topper[entry].time / elapsed);
	pdouble("cpu_usr", TIMEDELTA(pi_utime) / elapsed);
	pdouble("cpu_sys", TIMEDELTA(pi_stime) / elapsed);
	pdouble("cpu_usr_total_secs",
		CURRENT(pi_utime) / (double) sysconf(_SC_CLK_TCK));
	pdouble("cpu_sys_total_secs",
		CURRENT(pi_stime) / (double) sysconf(_SC_CLK_TCK));
	plong("statm_size_kb", CURRENT(statm_size) * pagesize / 1024);
	plong("statm_resident_kb",
	      CURRENT(statm_resident) * pagesize / 1024);
	plong("statm_restext_kb", CURRENT(statm_trs) * pagesize / 1024);
	plong("statm_resdata_kb", CURRENT(statm_drs) * pagesize / 1024);
	plong("statm_share_kb", CURRENT(statm_share) * pagesize / 1024);
	pdouble("minorfault", COUNTDELTA(pi_minflt) / elapsed);
	pdouble("majorfault", COUNTDELTA(pi_majflt) / elapsed);

	plong("it_real_value", CURRENT(pi_it_real_value));
	pdouble("starttime_secs",
		(double) (CURRENT(pi_start_time)) /
		(double) sysconf(_SC_CLK_TCK));
	plong("virtual_size_kb", (long long) (CURRENT(pi_vsize) / 1024));
	plong("rss_pages", CURRENT(pi_rss));
	plong("rss_limit", CURRENT(pi_rsslimit));
#ifdef PROCESS_DEBUGING_ADDRESSES_SIGNALS
	/* NOT INCLUDED AS THEY ARE FOR DEBUGGING AND NOT PERFORMANCE TUNING */
	phex("start_code", CURRENT(pi_start_code));
	phex("end_code", CURRENT(pi_end_code));
	phex("start_stack", CURRENT(pi_start_stack));
	phex("esp_stack_pointer", CURRENT(pi_esp));
	phex("eip_instruction_pointer", CURRENT(pi_eip));
	phex("signal_pending", CURRENT(pi_signal_pending));
	phex("signal_blocked", CURRENT(pi_signal_blocked));
	phex("signal_ignore", CURRENT(pi_signal_ignore));
	phex("signal_catch", CURRENT(pi_signal_catch));
	phex("signal_exit", CURRENT(pi_signal_exit));
	phex("wchan", CURRENT(pi_wchan));
	/* NOT INCLUDED AS THEY ARE FOR DEBUGGING AND NOT PERFORMANCE TUNING */
#endif

	plong("swap_pages", CURRENT(pi_swap_pages));
	plong("child_swap_pages", CURRENT(pi_child_swap_pages));
	plong("last_cpu", CURRENT(pi_last_cpu));
	plong("realtime_priority", CURRENT(pi_realtime_priority));
	plong("sched_policy", CURRENT(pi_sched_policy));
	pdouble("delayacct_blkio_secs",
		(double) CURRENT(pi_delayacct_blkio_ticks) /
		(double) sysconf(_SC_CLK_TCK));
	plong("uid", CURRENT(uid));
	if (strlen(CURRENT(username)) > 0)
	    pstring("username", CURRENT(username));
	psubend();
    }
    psectionend();

}
void tokenise(char *s)
{
int i;
int len;
char key[256]   = { 0 };
char value[256] = { 0 };
int keymode = 1;
char tmpstr[256];

    for(i=1;; i++) {  /* 1 = to skip the first ,  save with the tags */
        if(s[i] == '=') {
            keymode = 0;
            continue;
        }
        if(s[i] == ',' || s[i] == 0) {
            sprintf(tmpstr,"%s_tag",key);
            pstring(tmpstr,value);
            key[0] = 0;
            value[0] = 0;
            keymode = 1;
        }
        if(s[i] == 0) {
            break;
        }
        if(s[i] == ',') {
            continue;
        }
        if(keymode == 1) {
            len=strlen(key);
            key[len] = s[i];
            key[len+1] = 0;
        } else {
            len=strlen(value);
            value[len] = s[i];
            value[len+1] = 0;
        }
    }
}

void tags()
{
    FUNCTION_START;
    psection("tags");
    pstring("host_tag",         tag_hostname);
    pstring("os_tag",           tag_os);
    pstring("architecture_tag", tag_arch);
    pstring("serial_no_tag",    tag_sn);
    pstring("mtm_tag",          tag_mtm);

    if(strlen(additional_tags))
        tokenise(additional_tags);
    psectionend();
}


void hint(char *program, char *version)
{
    FUNCTION_START;
    printf("%s: help information. Version:%s\n\n", program, version);
    printf("- Performance stats collector outputting JSON or Influx Line Protocol format.\n");
    printf("- If the commend starts with njmon it runs in NJMON mode\n");
    printf("- If the commend starts with nimon it runs in NIJMON mode\n");
    printf("- -J forces NJMON mode regardless of the command name\n");
    printf("- -I forces NIMON mode regardless of the command name\n");
    printf("- Note:njmon & nimondata is exactly the same, so use one database.\n");
    printf("- Default is stdout\n");
    printf("- Core syntax  :     %s -s seconds -c count\n", program);
    printf("- File output  :     -m directory -f\n");
    printf("- Check & restart: -k and -K filename\n");
    printf("- Data options:    -A alias -H -P -t %% -b -d -D -F -n -r -R -W\n");
    printf("- Argument file:   -a file (do not include the command name)\n");
    printf("- NJMON Network connection: -i njmond_host -p port\n");
    printf("- NIMON Network connection: -i InfluxDB_host -p port\n");
    printf("  NIMON InfluxDB Details  : -x database [-y username [-x password]]\n");
    printf("  NIMON InfluxDB2         : -x bucket -O Org -T <token>\n");
    printf("  Prometheus mode   : -w\n");

    printf("\t-!           : Version check and immediate exit\n");
    printf("\t-?           : This help informtion\n");
    printf("\t-s seconds   : seconds between snapshots of data (default 60 seconds)\n");
    printf("\t-c count     : number of snapshots (default forever)\n\n");
    printf("\t-D           : Add diskstats measurement includes all devices in /proc/diskstats\n");
    printf("\t-m directory : Program will cd to the directory before output\n");
    printf("\t-M           : Filesystems listed by mount point (like AIX njmon) and not filesystem name\n");
    printf("\t-f           : Output to file (not stdout) to two files below\n");
    printf("\t             : NIMON mode - Data:  hostname_<year><month><day>_<hour><minutes>.influxlp\n");
    printf("\t             : NJMON mode - Data:  hostname_<year><month><day>_<hour><minutes>.json\n");
    printf("\t             : Error: hostname_<year><month><day>_<hour><minutes>.err\n");
    printf("\t-ff          : NIMON mode - Note: a second -f adds a timestamp so this data can be added to InfluxDB later\n");
    printf("\t-A alias     : User defined alias hostname to work around crazy duplicate hostnames\n");
    printf("\t-a file      : Command line arguments are in the file (hides passwords)\n");
    printf("\t             : Use the same arguments, all on the first line, space separated\n");
    printf("\t             : Only have the -a option on the actual command line\n");
    printf("\t-H           : NIMON mode - Force host tag to be a Fully Qualified Domain Name\n");
    printf("\t-k           : NIMON mode - Read /tmp/nimon.pid for a running nimon PID & if found\n");
    printf("\t-k           : NJMON mode - Read /tmp/njmon.pid for a running njmon PID & if found\n");
    printf("\t               still running then this process exits quietly\n");
    printf("\t-K filename  : User defined pid filename (implies -k). Allows not using /tmp or\n");
    printf("\t               multiple concurrent data captures\n");
    printf("\t-I           : Force to NIMON mode. Saving InfluxDB Line Protocol data or njmond.py or other Timeseries database\n");
    printf("\t-J           : Force to NJMON mode. Set njmon mode for JSON format for njmond.py or other Timeseries database\n");
    printf("\t-P           : Add process stats (take CPU cycles and large stats volume)\n");
    printf("\t-t percent   : Set ignore process CPU use percent threshold (default 0.01%%)\n");
    printf("\t-b           : Switch of adding pid to the process names: \"ksh_76927\" -> \"ksh\"\n");
    printf("\t-? or -h     : This output and stop\n");
    printf("\t-d           : Switch on debug tracing (output no longer JSON/line protocol format)\n");
    printf("\t-W           : Switch off warning messages in error output stream\n");
    printf("\t-r           : Random start pause. Stops cron making every program send data in sync\n");
    printf("\t-n           : No PID printed out at start up.\n");
    printf("\t-R           : Reduced stats - skip logical CPU stats for SMT threads.\n");
    printf("\t-F           : Switch off filesystem stats (autofs and tmpfs can cause issues)\n");

    printf("--- NIMON mode options ---\n");
    printf("- Sent data to InfluxDB (all of these are inportant for InfluxDB):\n");
    printf("\t-i hostname  : Hostmane or IP address of the influxDB server\n");
    printf("\t-p port      : InfluxDB port (default is 8086)\n");
    printf("\t-x njmon     : InfluxDB database name created with influx > create database njmon. (default is njmon)\n");
    printf("\t-y user      : Mandatory InfluxDB username - if you have set a username for the API (otherwise ignored)\n");
    printf("\t-z pass      : Mandatory InfluxDB password - if you have set a password for the API (otherwise ignored)\n");
    printf("\t-w           : For output to Telegraf. Dont send POST, message type or packet size\n");
    printf("\t-v           : Show data + response to InfluxDB on stderr\n");
    printf("\tCurrently, InfluxDB security certificates are not implemented\n");

    printf("--- NIMON with InfluxDB 2.0 options ---\n");
    printf("\t-O org       : InfluxDB 2.0 uses Organisations for data, users, dashboards etc. Default is \"default\".\n");
    printf("\t-T token     : Security Token from InfluxDB 2.0 GUI & switch to 2.0 mode\n");
    printf("\t-q additional_tags : additional tags for nimon \"-q dept=foobar,area=51\"\n");

    printf("\n");
    printf("NIMON Examples:\n");
    printf("    1 Take a look at the Line Protocol data\n");
    printf("\t/usr/local/bin/nimon -s 2 -c 3 -f    then edit <hostname>_<date>_<time>.influxlp\n");
    printf("    2 Every 1 minute all day using the default port of 8086\n");
    printf("\t/usr/local/bin/nimon -s 30 -c 2880 -i influxbox -x njmon -y Nigel -z passwd\n");
    printf("    3 Add process stats & use IP address\n");
    printf("\t/usr/local/bin/nimon -s 60 -c 1440 -P -i 9.137.62.12 -p 8086 -x njmon\n");
    printf("    4 Crontab run 1 day - start 1 minute after midnight, save local data every 30 seconds\n");
    printf("\t1 0 * * * /usr/local/bin/nimon -s 30 -c 2880 -i influxbox -p 8086 -x njmon -y Nigel -z passwd\n");
    printf("    5 Crontab - hourly check/restart nimon (if nimon is not running)\n");
    printf("\t0 * * * * /usr/lbin/nimon -k -s 60 -i influxbox -p 8086 -x njmon\n");
    printf("    6 Crontab - as 5 but output for telegraf\n");
    printf("\t0 * * * * /usr/lbin/nimon -w -k -s 60 -i influxbox -p 8086 -x njmon\n");
    printf("    7 InfluxDB 2+ using default port 8086 & default bucket njmon\n");
    printf("\t/usr/lbin/nimon -k -s 60 -i influx2box -O IBM -T Ptf$g4...==\n");

    printf("\n--- NJMON mode options or other Time-Series database ---\n");
    printf("Push data to central njmond daemon:\n");
    printf("\t-i ip/host   : IP address or Hostname of the njmond central daemon\n");
    printf("\t-p port      : Port number on njmond.py host\n");
    printf("\n");

    printf("NJMON Examples:\n");
    printf("    1 Every 5 mins all day\n");
    printf("\t/usr/local/bin/njmon -s 300 -c 288 -f -m /home/perf\n");
    printf("    2 Piping to data handler all day at 30 second intervals\n");
    printf("\t/usr/local/bin/njmon -s 30 -c 1440 | myprog\n");
    printf("    3 Use the defaults (-s 60 forever) and save to a file \n");
    printf("\t./njmon > my_server_today.json\n");
    printf("    4 Send data to the central daemon with no password\n");
    printf("\t./njmon -s 30 -c 2880 -i admin.acme.com -p 8181\n");
    printf("    5 Send data to the central daemon with process stats and with password\n");
    printf("\t./njmon -s 60 -c 1440 -P -i admin.acme.com -p 8181 -X password\n");
    printf("Crontab options:\n");
    printf("    6 Crontab only save to a file (2 minutes past midnight)\n");
    printf("\t2 0 * * * /usr/local/bin/njmon -s 60 -c 1440 -f -m /home/perf\n");
    printf("    7 Crontab daily at midnight start sending data to central server\n");
    printf("\t0 0 * * * /usr/local/bin/njmon -s 30 -c 2880 -i admin.acme.com -p 8181\n");
    printf("    8 Crontab send data to central server check hourly (if njmon is not running)\n");
    printf("\t0 * * * * /usr/local/bin/njmon -k -s 30 -i admin.acme.com -p 8181\n");
    printf("    9 Crontab use ssh on Influx server to start njmon, send data back via socket to a local InfluxDB \"injector\" program\n");
    printf("\t0 0 * * * ssh root@endpoint /usr/local/bin/njmon -s 30 -c 2880 -i admin.acme.com -p 8181 \n");
    printf("\n");
}

void rmspace(char *s)
{
int i;
	for(i=0;i<=strlen(s);i++) {
	    s[i] = s[i+1];
	}
}

/* MAIN */

int main(int argc, char **argv)
{
    long maxloops = -1;
    long seconds = 60;
    int target_mode = 0;
    int no_pid = 0;
    int ch;
    double elapsed = 0.1;
    double previous_time;
    double current_time;
    double sleep_start;
    double sleep_end;
    double sleep_time;
    double execute_start = 0.0;
    double execute_end;
    double execute_time = 0.0;
    double sleep_overrun = 0.0;
    double sleep_target;
    long sleep_secs;
    long sleep_usecs;
    struct timeval tv;
    int commlen;
    int i;
    int j;
    int directory_set = 0;
    char directory[4096 + 1];
    char filename[4096];
    char *s;
    FILE *fp;
    pid_t child_pid;
    int proc_mode = 0;
    int pause = 0;
    int proc_pid = 1;
    int reduced_stats = 0;
    int filesystems_on = 1;

    /* command line processing */
    int  option_a = 0;;
    int  argumentc;
    char **argumentv;
    FILE *cmd_fp;
    char *cmd_parts[64];
    char cmd_buffer[1024+1];
    char *cmd;
    char commandline[2048+1];
    char * cli_njmon;
    char * cli_nimon;
    int storage = 0;
    int btrfs = 0;
    int mountpoint = FS_NAME;
    char * ptr;

    argumentc = argc;
    argumentv = argv;

    FUNCTION_START;
    s = getenv("NJMON_DEBUG");
    if (s != 0)
	debug = atoi(s);
    s = getenv("NJMON_STATS");
    if (s != 0)
	njmon_internal_stats = atoi(s);

    /* if no slash that use the whole command name */
    cmd = argv[0];
    /* find command name without the directory part */
    for(i=strlen(argv[0]); i > 0; i--){
                if(argv[0][i] == '/') {
                        cmd = &argv[0][i+1];
                        break;
                }
    }
    /* check command name starts nimon or njmon and set mode appropriately */
    if(!strncmp(cmd, "njmon",5)) {
        mode = NJMON;
    }
    else if (!strncmp(cmd, "nimon",5)) {
        mode = NIMON;
    }
    else {
        printf("Invalid command name: must start with njmon or nimon and not \"%s\"\n", cmd);
        exit(76);
    }

    signal(SIGUSR1, interrupt);
    signal(SIGUSR2, interrupt);

    uid = getuid();

    argumentc = argc;
    argumentv = argv;

    commandline[0] = 0;
    for (i = 0; i < argc; i++) {
	sprintf(&commandline[strlen(commandline)], "%s ", argv[i]);
    }
    if(mode == NJMON)
	cli_njmon = "a:A:bBc:dDefFh?i:IJkK:m:MnO:p:PrRs:t:T:WX:!";
    else
	cli_nimon = "a:A:bBc:dDfFhH?i:IJkK:m:MnO:p:Pq:rRs:t:T:vwW!x:y:z:"; /* less X and extra vwxyz */

    while (-1 != (ch = getopt(argumentc, argumentv, mode==NJMON?cli_njmon:cli_nimon))) 
	{
	    switch (ch) {
	    case '!':
		printf("version %s\n", VERSION);
		exit(0);
	    case 'A': 
		DEBUG fprintf(stderr, "option -A: alias hostname %s\n", optarg);
		strncpy(alias_hostname, optarg, 1023);
		break;
	    case 'a': /* comandline options in a file */
		DEBUG fprintf(stderr, "option -a: filename=\"%s\"\n",optarg);
		if(option_a++) break; /* only allow -a option once */
		cmd_fp = fopen(optarg,"r");
		if(cmd_fp == NULL) {
			printf("%s stopping: -a cmdfile - opening %s failed errno=%d\n",argv[0],optarg,errno);
			exit(2345);
		}
		cmd_parts[0] = argv[0];
		if(fgets(cmd_buffer, 1024, cmd_fp) == NULL) {
			printf("%s stopping: -a cmdfile - read %s failed errno=%d\n",argv[0],optarg,errno);
			exit(7568);
		}
		cmd_buffer[1024] = 0; /* force NULL */
		if (cmd_buffer[strlen(cmd_buffer)-1] == '\n')
			cmd_buffer[strlen(cmd_buffer)-1] = 0;   /* remove trailing newlin e */
		/* remove doube spaces */
		for(i=0;i<strlen(cmd_buffer);i++) {
		    if( (cmd_buffer[i] == ' ') && (cmd_buffer[i+1] == ' ') ) {
			rmspace(&cmd_buffer[i]);
			i--;
		    }
		}
		DEBUG fprintf(stderr, "undouble spaces=\"%s\"\n",cmd_buffer);
		/* remove start space */
		if(cmd_buffer[0] == ' ')
			rmspace(cmd_buffer);
		/* remove end space */
		if(cmd_buffer[strlen(cmd_buffer) -1] == ' ')
			cmd_buffer[strlen(cmd_buffer) -1] = 0;
		DEBUG fprintf(stderr, "un start end spaces=\"%s\"\n",cmd_buffer);
		sprintf(&commandline[strlen(commandline)], "%s", cmd_buffer);
		DEBUG fprintf(stderr, "commandline-a=\"%s\"\n",commandline);
		j=1; /* skip 0th entry = command name */
		cmd_parts[0] = argv[0];;
		cmd_parts[1] = cmd_buffer;
		for(i=0;i<1024;i++) {
		    if(cmd_buffer[i] == ' ' || cmd_buffer[i] == 0) {
			cmd_buffer[i] = 0;
			j++;
			cmd_parts[j] = &cmd_buffer[i+1];
		    }
		    if(j >= 64 ) break; /* hacking attempt */
		}
		optind = 0;     /* rewind to the start of new parameters */
		argumentc = j;
		argumentv = cmd_parts;
		/*
		 * printf("reprogrammed %d arguments\n",i);
		 *  for(i=0;i<j;i++)
		 *   printf("argumentv[%d]=%s\n",i,argumentv[i]);
		 */
		break;
	    case 'b': /* process names to with no PID */
		DEBUG fprintf(stderr, "option -b: no processes PID\n");
		proc_pid = 0;
		break;
	    case 'B': /* List btrfs disks in /proc/diskstats */
		btrfs = 1;
		DEBUG fprintf(stderr, "option -B: btrfs in /proc/diskstat\n");
		break;
	    case 'c': /* count = the number of snapshots */
		DEBUG fprintf(stderr, "option -c: count=\"%s\"\n",optarg);
		if (isdigit(optarg[0])) {
		    maxloops = atoi(optarg);
		    if (maxloops < 1)
			maxloops = 1;
		} else {
		    printf("njmon: -c option required a number\n");
		    exit(101);
		}
		break;
	    case 'd': /* debug */
		debug++;
		DEBUG fprintf(stderr, "option -d: debug\n");
		break;
	    case 'D': /* List ALL "things" in /proc/diskstats */
		storage = 1;
		DEBUG fprintf(stderr, "option -D: all devices in /proc/diskstat\n");
		break;
	    case 'e': /* switch on elastic search option for sub sections to be arrays */
                if(mode == NJMON) {
                    DEBUG printf("option -e: elastic JSON style\n");
                    elastic = 1;
                } else {
                    printf("Invalid nimon mode option -e: elastic JSON style\n");
                    exit(65);
                }
		break;
	    case 'f': /* save the data to a file */
		DEBUG fprintf(stderr, "option -f: to file\n");
		file_output++;
		break;
	    case 'F': /* switch off file system if they are a problem, typically autofs or tmpfs */
		DEBUG fprintf(stderr, "option -F: no filesystems\n");
		filesystems_on = 0;
		break;
	    case '?': /* help info  */
	    case 'h':
		DEBUG fprintf(stderr, "option -h or ?: help\n");
		hint(argv[0], VERSION);
		exit(0);
	    case 'H': /* NIMON only */
		DEBUG fprintf(stderr, "option -H fullhost tag\n");
		fullhostname_tag = 1;
		break;
	    case 'i': /* IP address of target_hostname */
		DEBUG fprintf(stderr, "option -i: IP/target_hostname=\"%s\"\n",optarg);
		strncpy(target_host, optarg, 1024);
		target_host[1024] = 0;
		target_mode = 1;
		break;
	    case 'K': /* user specified pid file */
		DEBUG fprintf(stderr, "option -K: PIDfile\n");
		pid_filename = (char *)malloc(strlen(optarg) +1);
		strcpy(pid_filename, optarg);
		kill_on = 1;
		break;
	    case 'k': /* bail out if njmon/nimon already running */
		DEBUG fprintf(stderr, "option -k: kill\n");
		kill_on = 1;
		break;
            case 'I':
                DEBUG printf("option -I: InfluxDB nimon mode\n");
                mode = NIMON;
                break;
            case 'J':
                DEBUG printf("option -J: JSON njmon stats\n");
                mode = NJMON;
                break;
	    case 'n': /* Don't print the PID at start up */
		DEBUG fprintf(stderr, "option -n: no PID\n");
		no_pid = 1;
		break;
	    case 'O':
		DEBUG fprintf(stderr, "option -O organisation=%s\n",optarg);
		strncpy(influx_org, optarg, 64);
		break;
	    case 'm': /* change to this directory */
		DEBUG fprintf(stderr, "option -m: directory=\"%s\"\n",optarg);
		directory_set = 1;
		strncpy(directory, optarg, 4096);
		directory[4096] = 0;
		break;
	    case 'M': 
		DEBUG fprintf(stderr, "option -M: use filesystem mountpoints and not device names\n");
		mountpoint = FS_MOUNTPOINT;
		break;
	    case 'p': /* socket IP target_port */
		DEBUG fprintf(stderr, "option -i: target_port=\"%s\"\n",optarg);
		if (isdigit(optarg[0])) {
		    target_port = atoi(optarg);
		} else {
		    printf("njmon: -p option required a number\n");
		    exit(101);
		}
		break;
	    case 'P': /* save process stats */
		DEBUG fprintf(stderr, "option -P: processes\n");
		proc_mode = 1;
		break;
            case 'q':
                DEBUG fprintf(stderr, "option -q: additional tag string\"%s\"\n", optarg);
                for(i=0; i<strlen(optarg); i++) {
                    if(optarg[i] == ' ') {
                        printf("Invalid njmon mode option -q $s invalid space\n",optarg);
                        exit(78);
                    }
                }
                additional_tags[0] = ',';
                strncpy(&additional_tags[1], optarg, 254);
                break;

	    case 'r': /* add random sleep after start up to stop synchronised data push */
		DEBUG fprintf(stderr, "option -r: random sleep\n");
		pause = 1;
		break;
	    case 'R': /* skip details CPU stats to reduce the data size */
		DEBUG fprintf(stderr, "option -R: reduced stats\n");
		reduced_stats = 1;
		break;
	    case 's': /* seconds between snapshots */
		DEBUG fprintf(stderr, "option -s: snapshots=\"%s\"\n",optarg);
		if (isdigit(optarg[0])) {
		    seconds = atoi(optarg);
		    if (seconds < 1)
			seconds = 1;
		} else {
		    printf("njmon: -s option required a number\n");
		    exit(100);
		}
		break;
	    case 't': /* threshhold below which we dont report processes = removes processes with near zzero run time */
		DEBUG fprintf(stderr, "option -t: threshold=\"%s\"\n",optarg);
		ignore_threshold = atof(optarg);
		break;
	    case 'T': /* implies InfluxDB 2.0+ */
		DEBUG fprintf(stderr, "option -T token=%s\n",optarg);
		strncpy(influx_token, optarg, 256);
		influx_version = 2;
		break;
	    case 'W': /* skip warning if you can live with them */
		DEBUG fprintf(stderr, "option -W: warnings\n");
		warnings_on = 0;
		break;
	    case 'v': /* add more debug info */
		DEBUG fprintf(stderr, "option -v: verbose debug\n");
		warnings_on = 0;
		verbose++;
		break;
	    case 'w': /* output to telegraf so no POST, type of measure size */
		DEBUG fprintf(stderr, "option -w: telegraf output\n");
		telegraf_mode = 1;
		break;
	    case 'x': /* name the InfluxDB database */
		DEBUG fprintf(stderr, "option -x: InfluxDB name=\"%s\"\n",optarg);
		strncpy(influx_database, optarg, 63);
		break;
	    case 'y': /* user name on InfluxDB */
		DEBUG fprintf(stderr, "option -y: user=\"%s\"\n",optarg);
		strncpy(influx_username, optarg, 63);
		break;
	    case 'z': /* user password on InfluxDB */
		DEBUG fprintf(stderr, "option -z: passwd=\"%s\"\n",optarg);
		strncpy(influx_password, optarg, 63);
		break;
	    }
	}
    if (alias_hostname[0] == 0) {
	    ptr = getenv("NJMON_HOSTNAME");
	    if(ptr != 0)
	        strncpy(alias_hostname, ptr, 1023);
    }

    if(kill_on)  {
	check_pid_file();
    }

    get_hostname();
    if (mode == NIMON && target_mode == 1 && target_port <= 0) {
	DEBUG fprintf(stderr, "target_port not set defaulting to 8086\n");
	target_port = 8086;
    }
    if (mode == NIMON && strlen(influx_database) == 0) {
	DEBUG fprintf(stderr, "influx_database not set defaulting to \"njmon\"\n");
	strcpy(influx_database, "njmon");
    }
    if (target_mode == 0 && target_port != 0) {
	printf("%s -p %ld but not the -i ip-address option\n", argv[0], target_port);
	exit(53);
    }
    if (target_mode == 1 && target_port != 0) {	/* We are attempting sending the data remotely */
	if (isalpha(target_host[0])) {
	    struct hostent *he;

	    he = gethostbyname(target_host);
	    if (he == NULL) {
		sprintf(errorbuf,
			"influx host=%s to IP address convertion failed gethostbyname(), bailing out\n",
			target_host);
		error(errorbuf);
	    }
	    /* TEST
	       printf("name=%s\n",he->h_name);
	       printf("type=%d = ",he->h_addrtype);
	       switch(he->h_addrtype) {
	       case AF_INET: printf("IPv4\n"); break;
	       case AF_INET6: printf("(IPv6\n"); break;
	       default: printf("unknown\n");
	       }
	       printf("length=%d\n",he->h_length);
	     */

	    /* this could return multiple IP addresses but we assume its the first one */
	    if (he->h_addr_list[0] != NULL) {
		strcpy(target_ip, inet_ntoa(*(struct in_addr *) (he->h_addr_list[0])));
	    } else {
		sprintf(errorbuf,
			"target_host=%s to IP address convertion failed9 (empty list), bailing out\n",
			target_host);
		error(errorbuf);
	    }
        } else {
            strcpy( target_ip, target_host);
        }
    }
    if(mode == NIMON) {
	    if (target_mode == 1 && strlen(influx_database) <= 0) {
		printf("%s -i %s set but not the -x database option\n", argv[0], target_host);
		exit(55);
	    }
    }
    if (directory_set) {
	if (chdir(directory) == -1) {
	    directory[512]=0; 
	    sprintf(errorbuf, "Change Directory to %s failed", directory);
	    error(errorbuf);
	}
    }

    /* disconnect from terminal */
    DEBUG fprintf(stderr, "forking for daemon making if debug=%d === 0\n", debug);
    if (!debug && (child_pid = fork()) != 0) {
	if(!no_pid)
	    printf("%d\n", child_pid);
	exit(0);		/* parent returns OK */
    }
    DEBUG fprintf(stderr, "child running\n");
    if(kill_on)  {
        make_pid_file();
    }
    if (!debug) {
	/*      close(0);
	   close(1);
	   close(2);
	 */
	setpgrp();		/* become process group leader */
	signal(SIGHUP, SIG_IGN);	/* ignore hangups */
    }
    if (file_output) {
	get_time();
	get_localtime();
        fflush(NULL);
	if(mode == NJMON) {
	    if(file_output >= 2) {
		sprintf(filename_ff2, "%s_%02d%02d%02d_%02d%02d", 
			hostname, tim->tm_year, tim->tm_mon, tim->tm_mday, tim->tm_hour, tim->tm_min);
	    } else {
		sprintf(filename, "%s_%02d%02d%02d_%02d%02d.json", 
			hostname, tim->tm_year, tim->tm_mon, tim->tm_mday, tim->tm_hour, tim->tm_min);
		if ((fp = freopen(filename, "w", stdout)) == 0 ) {
                    nwarning2("opening file for stdout filename=%s\n", filename);
                    exit(12);
                }
	    }
	} else {
		sprintf(filename, "%s_%02d%02d%02d_%02d%02d.influxlp", 
			hostname, tim->tm_year, tim->tm_mon, tim->tm_mday, tim->tm_hour, tim->tm_min);
		if ((fp = freopen(filename, "w", stdout)) == 0 ) {
                    nwarning2("opening file for stdout filename=%s\n", filename);
                    exit(13);
                }
	}

	sprintf(filename, "%s_%02d%02d%02d_%02d%02d.err",
		hostname,
		tim->tm_year,
		tim->tm_mon, tim->tm_mday, tim->tm_hour, tim->tm_min);
	if ((fp = freopen(filename, "w", stderr)) == 0) {
	    sprintf(errorbuf, "Can't open output file=%s\n", filename);
	    error(errorbuf);
	}
    }
    fflush(NULL);

#define RANGE(min,max) ((long)random() % ((max) - (min) +1)) + (min)
    if (pause) {
	long pause_seconds;
	pause = seconds;
	/* sanity checks */
	if (pause > 300)
	    pause = 300;	/* don't delay the first snapshot too much */
	if (pause < 2)
	    pause = 5;
	/* seed the random generator with a different value on every end point */
	srandom(getpid());
	pause_seconds = RANGE(0, pause);
	DEBUG fprintf(stderr, "pause=%ld\n", pause_seconds);
	sleep(pause_seconds);
    }
    output_size = INITIAL_BUFFER_SIZE;
    output = malloc(output_size);	/* buffer space for the stats before the push to standard output */
    commlen = 1;		/* for the terminating zero */
    for (i = 0; i < argc; i++) {
	commlen = commlen + strlen(argv[i]) + 1;	/* +1 for spaces */
    }
    njmon_command = malloc(commlen);
    njmon_command[0] = 0;
    for (i = 0; i < argc; i++) {
	strcat(njmon_command, argv[i]);
	if (i != (argc - 1))
	    strcat(njmon_command, " ");
    }

    save_tags();
    /* seed incrementing counters */
    proc_stat(elapsed, PRINT_FALSE, reduced_stats);

    proc_diskstats_init(elapsed);
    if(btrfs)
        proc_diskstats_btrfs_init(elapsed);
    /*
    proc_swaps_init();
    */

    proc_net_dev(elapsed, PRINT_FALSE);
    nfs_init();
    init_lparcfg();
    sys_device_system_cpu(1.0, PRINT_FALSE);
#ifndef NOGPFS
    gpfs_init();
#endif				/* NOGPFS */
#ifdef NVIDIA_GPU
    gpu_init();
#endif				/* NVIDIA_GPU */

    if (proc_mode)
	processes_init();

#ifdef EXTRA
    extra_init();
#endif /* EXTRA */

    gettimeofday(&tv, 0);
    previous_time = (double) tv.tv_sec + (double) tv.tv_usec * 1.0e-6;

    if (seconds <= 60) 
	sleep(seconds);
    else 
	sleep(60);	/* if a long time between snapshot do a quick one 
				   now so we have some stats in the output file */
    
    gettimeofday(&tv, 0);
    current_time = (double) tv.tv_sec + (double) tv.tv_usec * 1.0e-6;
    elapsed = previous_time - current_time;

    /* have to initialise just this one */
    execute_start = (double) tv.tv_sec + ((double) tv.tv_usec * 1.0e-6);
    for (loop = 0; maxloops == -1 || loop < maxloops; loop++) {
        /* sanity check */
        if(execute_time < 0.0)
            execute_time = 0.0;
        if(sleep_overrun < 0.0) /* seen this at a 1/1000th of a second scale due to sleep() inaccurate on some HW */
            sleep_overrun = 0.0;

        sleep_target = (double)seconds - sleep_overrun - execute_time;
        /* sanity check */
        if(sleep_target > 0.0 && sleep_target <= (double)seconds) {
            sleep_secs = (long)sleep_target;            /* whole seconds */
            sleep_usecs= (sleep_target - (double)sleep_secs) * 1000000; /* final fraction of a second in microseconds */
        } else {
            /* execute or sleep time negative or very large (can't get enough CPU time) than the maths does not work */
            sleep_secs = seconds;
            sleep_usecs= 0;
        }
       if (loop != 0) {  /* don't sleep on the first loop */
            DEBUG printf("calling usleep(%6.4f) . . .\n", sleep_target);
/* testing 
 *             if(debug) {
 *                 psection("sleeptime");
 *                 plong("seconds", seconds);
 *                 pdouble("sleep_target", sleep_target);
 *                 plong("sleep_secs", sleep_secs);
 *                 plong("sleep_usecs", sleep_usecs);
 *                 pdouble("execute_time", execute_time);
 *                 pdouble("sleep_overrun", sleep_overrun);
 *                 psectionend();
 */

            gettimeofday(&tv, 0);
            sleep_start = (double)tv.tv_sec + ((double)tv.tv_usec * 1.0e-6);

            if(sleep_secs > 0 && sleep_secs < (seconds + 1) )
                sleep (sleep_secs);  /* WHOLE SECOND SLEEP */
            if(sleep_usecs > 0.0 && sleep_usecs < 1000001 )
                usleep(sleep_usecs); /* MICRO SECOND SLEEP */

            gettimeofday(&tv, 0);
            sleep_end = (double)tv.tv_sec + ((double)tv.tv_usec * 1.0e-6);

            sleep_time = sleep_end - sleep_start;
            sleep_overrun = sleep_time - sleep_target;
        }
        gettimeofday(&tv, 0);
        execute_start = (double)tv.tv_sec + ((double)tv.tv_usec * 1.0e-6);

	psample();
#ifdef TIMERS
	/* for testing
	   if(loop == 10) accumalated_delay += 1.5;
	   if(loop == 20) accumalated_delay += 2.5;
	   if(loop == 30) accumalated_delay += 3.5;
	 */
        if(!(loop%10))
	    save_tags();

	psection("njmontime");
	plong("njmon_seconds", seconds);
	pdouble("njmon_sleep_time", sleep_time);
	pdouble("njmon_execute_time", execute_time);
	pdouble("njmon_accumalated", accumalated_delay);
	plong("njmon_sleep_seconds", sleep_seconds);
	psectionend();
#endif				/* timers */

	/* calculate elapsed time to include sleep and data collection time */
	if (loop != 0)
	    previous_time = current_time;
	gettimeofday(&tv, 0);
	current_time = (double) tv.tv_sec + ((double) tv.tv_usec * 1.0e-6);
	elapsed = current_time - previous_time;

	date_time(seconds, loop, maxloops, sleep_target, sleep_overrun, execute_time, elapsed);
	identity(commandline, VERSION);
	tags();
	etc_os_release();
	proc_version();
	lscpu();
	proc_stat(elapsed, PRINT_TRUE,reduced_stats);
	proc_cpuinfo(reduced_stats);
        proc_loadavg();
	read_data_number("meminfo", elapsed);
	read_data_number("vmstat",  elapsed);
	proc_diskstats_collect(elapsed);
	proc_diskstats_real(elapsed);
	if(storage)
	    proc_diskstats_all(elapsed);
	if(btrfs)
	    proc_diskstats_btrfs(elapsed);
	
        if(diskstats_resync) {
	    proc_diskstats_resync();
    	    proc_diskstats_init(elapsed);
    	    if(btrfs)
        	    proc_diskstats_btrfs_init(elapsed);
	}
	proc_swaps();
	proc_net_dev(elapsed, PRINT_TRUE);
	uptime();
	if(filesystems_on)
	    filesystems(mountpoint);
	nfs(elapsed);
	read_lparcfg(elapsed);
	sys_device_system_cpu(elapsed, PRINT_TRUE);
#ifndef NOGPFS
	gpfs_data(elapsed);
#endif				/* NOGPFS */
#ifdef NVIDIA_GPU
	gpu_stats();
#endif				/* NVIDIA_GPU */
	if (proc_mode)
	    processes(elapsed,proc_pid);

#ifdef EXTRA
	extra_data(elapsed);
#endif /* EXTRA */

	psampleend();
	push();
	/* debbuging - uncomment to crash here!
	  {
          int *crashptr = NULL;
	  crashptr = 42;
	  crashptr = 42;
	  }
	 */
        gettimeofday(&tv, 0);
        execute_end = (double)tv.tv_sec + ((double)tv.tv_usec * 1.0e-6);
        execute_time = execute_end - execute_start;
    }
    /* finish-of */
    remove_ending_comma_if_any();
    if (njmon_internal_stats)
	pstats();
    push();
    close(sockfd);		/* if a socket, let it close cleanly */
    remove_pid_file();
    sleep(1);
    return 0;
}

/* - - - the end - - - */
