/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/

/* User command to change their password. */
#include "rods.h"
#include "rodsClient.h"

void usage (char *prog);

int
main(int argc, char **argv)
{
    int i, ix, status;
    int echoFlag=0;
    char *password;
    rodsEnv myEnv;
    rcComm_t *Conn;
    rErrMsg_t errMsg;
    rodsArguments_t myRodsArgs;

    struct stat statbuf;
    int doStty=0;
    char newPw[MAX_PASSWORD_LEN+10];
    int len, lcopy;

    char buf0[MAX_PASSWORD_LEN+10];
    char buf1[MAX_PASSWORD_LEN+10];
    char buf2[MAX_PASSWORD_LEN+10];

    userAdminInp_t userAdminInp;

    /* this is a random string used to pad, arbitrary, but must match
       the server side: */
    char rand[]="1gCBizHWbwIYyWLoysGzTe6SyzqFKMniZX05faZHWAwQKXf6Fs"; 

    status = parseCmdLineOpt(argc, argv, "ehvVl", 0, &myRodsArgs);
    if (status != 0) {
       printf("Use -h for help.\n");
       exit(1);
    }

    if (myRodsArgs.echo==True) {
       echoFlag=1;
    }
    if (myRodsArgs.help==True) {
       usage(argv[0]);
       exit(0);
    }

    if (myRodsArgs.longOption==True) {
	rodsLogLevel(LOG_NOTICE);
    }
 
    ix = myRodsArgs.optind;

    password="";
    if (ix < argc) {
       password = argv[ix];
    }

    status = getRodsEnv (&myEnv);  /* Need to get irodsAuthFileName (if set) */
    if (status < 0) {
       rodsLog (LOG_ERROR, "main: getRodsEnv error. status = %d",
		status);
       exit (1);
    }

    if (myRodsArgs.verbose==True) {
       i = obfSavePw(echoFlag, 1, 1, password);
    }
    else {
       i = obfSavePw(echoFlag, 0, 0, password);
    }

    if (i != 0) {
       rodsLogError(LOG_ERROR, i, "Save Password failure");
       exit(1);
    }

    /* Connect... */ 
    Conn = rcConnect (myEnv.rodsHost, myEnv.rodsPort, myEnv.rodsUserName,
      myEnv.rodsZone, 0, &errMsg);
    if (Conn == NULL) {
       rodsLog(LOG_ERROR, 
		    "Saved password, but failed to connect to server %s",
	       myEnv.rodsHost);
       exit(2);
    }

    /* and check that the user/password is OK */
    status = clientLogin(Conn);
    if (status != 0) {
       rcDisconnect(Conn);
       exit (7);
    }

    /* get the new password */
#ifdef windows_platform
	 iRODSNtGetUserPasswdInputInConsole(newPw, "Enter your new iRODS password:", echoFlag);
#else
    if (stat ("/bin/stty", &statbuf) == 0) {
	 system("/bin/stty -echo");
	 doStty=1;
    }
    len = 0;
    for (;len < 4;) {
       printf("Enter your new iRODS password:");
       fgets(newPw, MAX_PASSWORD_LEN, stdin);
       len = strlen(newPw);
       if (len < 4) {
	  printf("\nYour password must be at least 3 characters long.\n");
       }
    }
    if (doStty) {
       system("/bin/stty echo");
       printf("\n");
    }
    newPw[len-1]='\0'; /* remove trailing \n */
#endif

    strncpy(buf0, newPw, MAX_PASSWORD_LEN);
    len = strlen(newPw);
    lcopy = MAX_PASSWORD_LEN-10-len;
    if (lcopy > 15) {  /* server will look for 15 characters
			  of random string */
       strncat(buf0, rand, lcopy);
    }
    i = obfGetPw(buf1);
    if (i !=0) {
       printf("Error getting current password\n");
       exit(1);
    }
    obfEncodeByKey(buf0, buf1, buf2);

    userAdminInp.arg0 = "userpw";
    userAdminInp.arg1 = myEnv.rodsUserName;
    userAdminInp.arg2 = "password";
    userAdminInp.arg3 = buf2;
	userAdminInp.arg4 = "";
	userAdminInp.arg5 = "";
	userAdminInp.arg6 = "";
	userAdminInp.arg7 = "";
	userAdminInp.arg8 = "";
	userAdminInp.arg9 = "";

    status = rcUserAdmin(Conn, &userAdminInp);
    if (status != 0) {
       rodsLog (LOG_ERROR, "rcUserAdmin error. status = %d",
		status);

    }
    else {
       /* initialize with the new password */
       i = obfSavePw(0, 0, 0, newPw);
    }

    rcDisconnect(Conn);

    exit (0);
}


void usage (char *prog)
{
   fprintf(stderr, "Changes your irods password and, like iinit, stores your new iRODS\n");
   fprintf(stderr, "password in a scrambled form to be used automatically by the icommands.\n");
   fprintf(stderr, "Prompts for your old and new passwords.\n");
   fprintf(stderr, "Usage: %s [-hvVl]\n", prog);
   fprintf(stderr, " -v  verbose\n");
   fprintf(stderr, " -V  Very verbose\n");
   fprintf(stderr, " -h  this help\n");
}
