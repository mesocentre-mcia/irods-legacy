/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/

/* See fileChksum.h for a description of this API call.*/

#include "fileChksum.h"
#include "miscServerFunct.h"

#ifdef SHA256_FILE_HASH
#include "sha.h"
#endif

#define SVR_MD5_BUF_SZ (1024*1024)

#ifdef SHA256_FILE_HASH
void sha256_hash_string (unsigned char hash[SHA256_DIGEST_LENGTH],
			 char outputBuffer[65]) {
    int i = 0;

    for(i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    }

    outputBuffer[i*2] = 0;
}
#endif


int
rsFileChksum (rsComm_t *rsComm, fileChksumInp_t *fileChksumInp, 
char **chksumStr)
{
    rodsServerHost_t *rodsServerHost;
    int remoteFlag;
    int status;

    remoteFlag = resolveHost (&fileChksumInp->addr, &rodsServerHost);
    if (remoteFlag == LOCAL_HOST) {
        status = _rsFileChksum (rsComm, fileChksumInp, chksumStr);
    } else if (remoteFlag == REMOTE_HOST) {
        status = remoteFileChksum (rsComm, fileChksumInp, chksumStr,
	  rodsServerHost);
    } else {
        if (remoteFlag < 0) {
            return (remoteFlag);
        } else {
            rodsLog (LOG_NOTICE,
              "rsFileChksum: resolveHost returned unrecognized value %d",
               remoteFlag);
            return (SYS_UNRECOGNIZED_REMOTE_FLAG);
        }
    }

    return (status);
}

int
remoteFileChksum (rsComm_t *rsComm, fileChksumInp_t *fileChksumInp,
char **chksumStr, rodsServerHost_t *rodsServerHost)
{    
    int status;

    if (rodsServerHost == NULL) {
        rodsLog (LOG_NOTICE,
          "remoteFileChksum: Invalid rodsServerHost");
        return SYS_INVALID_SERVER_HOST;
    }

    if ((status = svrToSvrConnect (rsComm, rodsServerHost)) < 0) {
        return status;
    }


    status = rcFileChksum (rodsServerHost->conn, fileChksumInp, chksumStr);

    if (status < 0) { 
        rodsLog (LOG_NOTICE,
         "remoteFileChksum: rcFileChksum failed for %s",
          fileChksumInp->fileName);
    }

    return status;
}

int
_rsFileChksum (rsComm_t *rsComm, fileChksumInp_t *fileChksumInp,
char **chksumStr)
{
    int status;

    *chksumStr = (char*)malloc (CHKSUM_LEN);

    status = fileChksum (fileChksumInp->fileType, rsComm, 
      fileChksumInp->fileName, *chksumStr);

    if (status < 0) {
        rodsLog (LOG_NOTICE, 
          "_rsFileChksum: fileChksum for %s, status = %d",
          fileChksumInp->fileName, status);
	free (*chksumStr);
	*chksumStr = NULL;
        return (status);
    }

    return (status);
} 

int
fileChksum (int fileType, rsComm_t *rsComm, char *fileName, char *chksumStr)
{
    int fd;
    MD5_CTX context;
    int len;
    unsigned char buffer[SVR_MD5_BUF_SZ];
    unsigned char digest[16]; /* for MD5 */
#ifdef SHA256_FILE_HASH
    int use_sha256=1;
    unsigned char sha256_hash[SHA256_DIGEST_LENGTH+10];
    SHA256_CTX sha256;
#endif
    int status;
#ifdef MD5_DEBUG
    rodsLong_t bytesRead = 0;	/* XXXX debug */
#endif

    if ((fd = fileOpen ((fileDriverType_t)fileType, rsComm, fileName, O_RDONLY, 0, NULL)) < 0) {
        status = UNIX_FILE_OPEN_ERR - errno;
        rodsLog (LOG_NOTICE,
        "fileChksum; fileOpen failed for %s. status = %d", fileName, status);
        return (status);
    }
#ifdef SHA256_FILE_HASH
    if (use_sha256==1) {
       SHA256_Init(&sha256); 
    }
    else {
       MD5Init (&context);
    }
#else
    MD5Init (&context);
#endif

    while ((len = fileRead ((fileDriverType_t)fileType, rsComm, fd, buffer, SVR_MD5_BUF_SZ)) > 0) {
#ifdef MD5_DEBUG
	bytesRead += len;	/* XXXX debug */
#endif
#ifdef SHA256_FILE_HASH
	if (use_sha256) {
	   SHA256_Update(&sha256, buffer, len);
	}
	else {
	   MD5Update (&context, buffer, len);
	}
#else
        MD5Update (&context, buffer, len);
#endif
    }
#ifdef SHA256_FILE_HASH
    if (use_sha256) {
       SHA256_Final(sha256_hash, &sha256);
    }
    else {
       MD5Final (digest, &context);
    }
#else
    MD5Final (digest, &context);
#endif
    fileClose ((fileDriverType_t)fileType, rsComm, fd);

#ifdef SHA256_FILE_HASH 
    if (use_sha256) {
       sha256_hash_string(sha256_hash, chksumStr);
    }
    else {
       md5ToStr (digest, chksumStr);
    }
#else
    md5ToStr (digest, chksumStr);
#endif

#ifdef MD5_DEBUG
    rodsLog (LOG_NOTICE,	/* XXXX debug */
    "fileChksum: chksum = %s, bytesRead = %lld", chksumStr, bytesRead);
#endif

    return (0);

}

