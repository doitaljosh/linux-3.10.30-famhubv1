/**
****************************************************************************************************
* @file SfdUep.c
* @brief Security framework [SF] filter driver [D] Unauthorized Execution Prevention (UEP) module
* @author Maksym Koshel (m.koshel@samsung.com)
* @author Dmitriy Dorogovtsev (d.dorogovtse@samsung.com)
* @author Yurii Kryvokhata (y.kryvokhata@samsung.com)
* @date Created Apr 1, 2014 12:47
* @see VD Coding standard guideline [VDSW-IMC-GDL02] 5.4 release 2013-08-12 
* @par In Samsung Ukraine R&D Center (SURC) under a contract between
* @par LLC "Samsung Electronics Ukraine Company" (Kiev, Ukraine)
* @par and "Samsung Electronics Co", Ltd (Seoul, Republic of Korea)
* @par Copyright: (c) Samsung Electronics Co, Ltd 2014. All rights reserved.
****************************************************************************************************
*/

#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/device.h>
#include <uapi/linux/stat.h>
#include <uapi/linux/sf/core/SfDebug.h>

#include "SfdUep.h"
#include "SfdUepHookHandlers.h"
#define PASS_HASH_ALGO_NAME  "sha256"
#define PASS_HASH_ALGO_SIZE  32
// testpass
#define PASS_HASH_CORRECT    "34d3f35460166cc84cc1b51a61abe9c5a9994558b777125969aed8d9d0c29588" 

extern Int s_uepStatus;
static DEFINE_MUTEX(uep_mutex);

static ssize_t SfdUepGetStatus(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", s_uepStatus);
}

static ssize_t SfdUepSetStatus(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    mutex_lock(&uep_mutex);
    sscanf(buf, "%d", &s_uepStatus);
    mutex_unlock(&uep_mutex);

    return count;
}

static ssize_t SfdUepGetStatusWithPass(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "v.1.0.1\n");
}

static ssize_t SfdUepSetStatusWithPass(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
    char pass[PASS_HASH_ALGO_SIZE];
    char hash_str[PASS_HASH_ALGO_SIZE*2 + 1];
    struct scatterlist sg;
    struct hash_desc desc;
    unsigned int pass_len, i;
    uint8_t pass_hash[PASS_HASH_ALGO_SIZE];

    mutex_lock(&uep_mutex);
    snprintf(pass, 32, "%s", buf);
    pass_len = count;
    desc.flags = 0;
    desc.tfm = crypto_alloc_hash(PASS_HASH_ALGO_NAME, 0 , CRYPTO_ALG_ASYNC);
    if (IS_ERR(desc.tfm)) {
        SF_LOG_E("%s(): failed to allocate %s hashing algorithm", __FUNCTION__, PASS_HASH_ALGO_NAME);
    }
    else {
        sg_init_one (&sg, pass, pass_len);
        crypto_hash_digest(&desc, &sg, pass_len, pass_hash);
        crypto_free_hash(desc.tfm);
        for (i=0;i<PASS_HASH_ALGO_SIZE;i++) sprintf(hash_str + 2*i, "%02x",pass_hash[i]);
        //SF_LOG_I("Password hash %s", hash_str);
        if (memcmp(PASS_HASH_CORRECT, hash_str, PASS_HASH_ALGO_SIZE*2) == 0) {
            s_uepStatus = 0;
            SF_LOG_I("UEP is disabled with password");
        }
    }
    mutex_unlock(&uep_mutex);

    return count;
}

static struct class_attribute uep_class_attrs[] = {
    __ATTR(status, S_IRUSR | S_IWUSR, SfdUepGetStatus, NULL),
    __ATTR(version, S_IRUSR | S_IWUSR , SfdUepGetStatusWithPass, SfdUepSetStatusWithPass),
    __ATTR_NULL
};

static struct class uep_class =
{
    .name = "uep",
    .owner = THIS_MODULE,
    .class_attrs = uep_class_attrs
};

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdOpenUepContext(SfdUepContext* const pUep)
{
	SF_STATUS result = SF_STATUS_FAIL;

	/**
	* If you want to use this macros in your code, please, check it's implementation before.
	* Implementation located in libcore/SfValidator.h
	*/
	SF_CONTEXT_SAFE_INITIALIZATION(pUep, SfdUepContext, SF_CORE_VERSION, SfdCloseUepContext);

	pUep->module.moduleType = SFD_MODULE_TYPE_UEP;

	pUep->module.PacketHandler[SFD_PACKET_HANDLER_TYPE_PREVENTIVE] = SfdUepPacketHandler;
	pUep->module.PacketHandler[SFD_PACKET_HANDLER_TYPE_NOTIFICATION] = NULL;

	pUep->header.state = SF_CONTEXT_STATE_INITIALIZED;
	result = SfdRegisterModule(&pUep->module);

    class_register(&uep_class);

	SF_LOG_I("%s was done with result: %d", __FUNCTION__, result);
	return result;
}

/*
****************************************************************************************************
*
****************************************************************************************************
*/
SF_STATUS SFAPI SfdCloseUepContext(SfdUepContext* const pUep)
{
	SF_STATUS result = SF_STATUS_NOT_IMPLEMENTED;

	if (!SfIsContextValid(&pUep->header, sizeof(SfdUepContext)))
	{
		SF_LOG_E("%s takes (pUep = %p) argument", __FUNCTION__, pUep);
		return SF_STATUS_BAD_ARG;
	}

	pUep->header.state = SF_CONTEXT_STATE_UNINITIALIZED;

    class_unregister(&uep_class);

	SF_LOG_I("%s was done with result: %d", __FUNCTION__, result);
	return result;
}
