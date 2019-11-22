
#include "SfdConfiguration.h"

#include <linux/sched.h>
#include <linux/namei.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/binfmts.h>
#include <linux/statfs.h>

#if defined(CONFIG_SECURITY_SFD_PLATFORM_TIZEN)

/**
****************************************************************************************************
* @brief Configuration of the directories for TIZEN platform
* @note Directories accepted
****************************************************************************************************
*/
static const char* s_moduleConfiguration[] =
{
	"/opt",
	"/var",
	"/tmp",
	"/media"
};

#endif

#if defined(CONFIG_SECURITY_SFD_PLATFORM_ORSAY)

/**
****************************************************************************************************
* @brief Configuration of the directories for ORSAY platform
****************************************************************************************************
*/
static const char* s_moduleConfiguration[] =
{
	"/mtd_uniro",
	"/mtd_exe",
	"/mtd_rwarea",
	"/mtd_drmregion_a",
	"/mtd_drmregion_b",
	"/sys/kernel/debug",
	"/mtd_rocommon",
	"/mtd_unirw",
	"/tmp",
	"/dtv"
};

#endif

/**
****************************************************************************************************
* @brief Number of files in the configuration directory
****************************************************************************************************
*/
static const size_t c_directoryListNumber = sizeof(s_moduleConfiguration) / sizeof(char*);

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SfCheckModuleResponsibility(struct file* const pFile)
{
    int ret;
    struct kstatfs stat;

    if( NULL == pFile )
    {
        return SF_STATUS_BAD_ARG;
    }

    // Check real mount point.
    // These routine should be included in release image or for performance team
    ret = vfs_statfs(&pFile->f_path, &stat);

    if (ret != 0)
    {
        SF_LOG_E( "%s Failed to vfs_statfs: ret:%d", __FUNCTION__, ret);
        return ret;
    }   

    return stat.f_flags & ST_RDONLY ? SF_STATUS_FAIL: SF_STATUS_OK;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
Char* SFAPI SfConstructAbsoluteFileNameByPath(struct path* const pPath, Char** const ppName)
{
    char* pBuffer = NULL;

    do
    {
        if (NULL == pPath || NULL == ppName)
        {
            SF_LOG_E( "%s takes invalid argument (pFile = %p, ppName = %p)",
                __FUNCTION__, pPath, ppName );
            break;
        }

        // Allocate buffer to be returned
        pBuffer = sf_malloc(PATH_MAX);

        if (NULL == pBuffer)
        {
            // Comment out considering that there is no free memory
            // SF_LOG_E("%s can not allocate memory with PATH_MAX = %d", __FUNCTION__, PATH_MAX);
            break;
        }

        path_get(pPath);

        /**
        * @brief Construct absolute file name *ppName is a pointer in the pBuffer array in case
        *   of successfull operation.
        */
        *ppName = d_path(pPath, pBuffer, PATH_MAX);

        if (IS_ERR(*ppName))
        {
            path_put(pPath);
            sf_free(pBuffer);
            pBuffer = NULL;
            break;
        }

        path_put(pPath);

    } while(FALSE);

    return pBuffer;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
Char* SfConstructAbsoluteFileNameByFile(struct file* const pFile, Char** const ppName)
{
    Char* pBuffer = NULL;

    do
    {
        if ( NULL == pFile || NULL == ppName )
        {
            SF_LOG_E( "%s takes invalid argument (pFile = %p, ppName = %p)",
                __FUNCTION__, pFile, ppName );
            break;
        }

        pBuffer = SfConstructAbsoluteFileNameByPath( &pFile->f_path, ppName);

    } while( FALSE );

    return pBuffer;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
Char* SFAPI SfConstructAbsoluteFileNameByTask(const struct task_struct* const pProcessContext,
	Char** const ppName)
{
    Char* pBuffer = NULL;
    struct mm_struct* pProcessMemoryMap;

    do
    {
        if ( NULL == pProcessContext || NULL == ppName )
        {
            SF_LOG_E("%s takes invalid argument (pProcessContext = %p, ppName = %p)",
                __FUNCTION__, pProcessContext, ppName);
            break;
        }

        pProcessMemoryMap = pProcessContext->mm;

        if ( NULL == pProcessMemoryMap )
        {
            SF_LOG_E( "%s can not get process memory map", __FUNCTION__ );
            break;
        }

        down_read( &pProcessMemoryMap->mmap_sem );
        if (NULL != pProcessMemoryMap->exe_file)
        {
            /**
            * @ brief Each process created from and executable file (process image) exe_file used
            *   to find absolute path to process image.
            */
            pBuffer = SfConstructAbsoluteFileNameByPath( &pProcessMemoryMap->exe_file->f_path,
                ppName );
        }
        else
        {
            SF_LOG_E( "%s can not get process image (exe_file)", __FUNCTION__ );
        }
        up_read( &pProcessMemoryMap->mmap_sem );
    } while( FALSE );

    return pBuffer;
}
