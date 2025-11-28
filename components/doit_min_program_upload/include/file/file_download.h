#ifndef __FILE_DOWNLOAD_H__
#define __FILE_DOWNLOAD_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "file_common.h"

    doit_file_result_t doit_file_download(const char *url, const char *dir_name);

#ifdef __cplusplus
}
#endif

#endif // __FILE_DOWNLOAD__