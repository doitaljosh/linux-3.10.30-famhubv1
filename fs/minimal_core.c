/*
 * linux/fs/minimal_core.c
 *
 * These are the functions used to create corefile with minimal info
 *       --> thread of crashing process
 *       --> note sections (NT_PRPSINFO, NT_SIGINFO, NT_AUXV, NT_FILE)
 *       --> crashing thread stack
 *       --> pages pointed by current thread context in its registers
 *           (r0 to r12)
 * The corefile created is compressed & encrypted,
 * It needs a modified GDB which is shared with HQ
 *
 * Copyright 2014: Samsung Electronics Co, Pvt Ltd,
 * Author : Manoharan Vijaya Raghavan (r.manoharan@samsung.com).
 */

#include <linux/mincore.h>
#include "coredump.h"
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/binfmts.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/personality.h>
#include <linux/elfcore.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/compiler.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <linux/random.h>
#include <linux/elf.h>
#include <linux/coredump.h>
#include <linux/uaccess.h>
#include <asm/param.h>
#include <asm/page.h>
#include <linux/namei.h>
#include <linux/list_sort.h>
#include <linux/zlib.h>
#include <linux/zutil.h>
#include <linux/vmalloc.h>
#include <linux/crc32.h>
#include <linux/crypto.h>
#include "../init/secureboot/include/SBKN_rsa.h"
#include "../init/secureboot/include/SBKN_bignum.h"
#include <linux/sort.h>
#include <linux/mutex.h>
#ifdef CONFIG_FUTEX_DLA
#include <linux/dla.h>
#endif
#ifdef CONFIG_SECURITY_SMACK
#include <../security/smack/smack.h>
#endif

#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
static struct memelfnote file_rlimit;
static int fill_open_files_info_note(void);
static unsigned long extend_file_rlimit(struct mm_struct *mm,
				struct list_head *psvma_list);
DEFINE_MUTEX(file_rlimit_mutex);
#endif

#define ERROR_CONDITION ((unsigned long)-1)
static DEFINE_MUTEX(mincore_file_mutex);

static cc_u8 gn_minimal_core[128] = {
	0x98, 0xdb, 0x1c, 0x51, 0xee, 0xc6, 0xde, 0x43,
	0x82, 0x6c, 0x7e, 0x8a, 0x21, 0xa5, 0xb2, 0xce,
	0x9d, 0x02, 0x49, 0x3d, 0x30, 0x3f, 0xb5, 0x83,
	0x31, 0xcc, 0xd0, 0xe8, 0xe4, 0x71, 0xdd, 0x30,
	0x12, 0x6e, 0x4b, 0x8b, 0xfa, 0x18, 0x1c, 0x39,
	0x89, 0xde, 0x24, 0x42, 0x65, 0xf2, 0x67, 0x52,
	0x37, 0x51, 0xa6, 0xbf, 0x8d, 0x0b, 0xec, 0x9f,
	0xac, 0x2c, 0x5a, 0x41, 0x85, 0xd2, 0x55, 0xc6,
	0x05, 0x54, 0xab, 0x2b, 0xe1, 0xf1, 0x2f, 0xee,
	0x65, 0xd2, 0x43, 0xe7, 0x40, 0xf5, 0xd4, 0x71,
	0xc3, 0x5c, 0xc7, 0x15, 0xe5, 0x33, 0x20, 0xd4,
	0x1f, 0x52, 0x1b, 0x71, 0xb0, 0xf0, 0x12, 0x1e,
	0x80, 0x5a, 0x66, 0xaa, 0xfd, 0x0e, 0x03, 0xa7,
	0xfd, 0xf8, 0x46, 0x1c, 0xdb, 0x42, 0x53, 0xe4,
	0xfd, 0x97, 0x5d, 0x24, 0xf7, 0x90, 0x4d, 0xaf,
	0x47, 0x8e, 0xcb, 0x92, 0x1d, 0x07, 0x1f, 0x6f
};

static cc_u8 ge_minimal_core[3] = {0x01, 0x00, 0x01};

/* Enable for debugging */
/* #define MINIMAL_CORE_DEBUG 1 */
#ifdef MINIMAL_CORE_DEBUG
char svma_name_buf[4096];
#endif

/* This code has been added so that encoded file decrypted properly
 * by rsa_hq decrypt tool.
 */
static void encodeblock(unsigned char in[3], unsigned char out[4], int len)
{
	static const char cb64[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz0123456789+/";

	out[0] = cb64[in[0] >> 2];
	out[1] = cb64[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];
	out[2] = (unsigned char) (len > 1 ? cb64[((in[1] & 0x0f) << 2)
				| ((in[2] & 0xc0) >> 6)] : '=');
	out[3] = (unsigned char) (len > 2 ? cb64[in[2] & 0x3f] : '=');
}

static void b64_encode(cc_u8 *input, cc_u8 *output, int insize)
{
	unsigned char in[3], out[4];
	int i, len, pos1 = 0, pos2 = 0;

	while (pos1 <= insize) {
		len = 0;
		for (i = 0; i < 3; i++) {
			in[i] = (unsigned char) input[pos1++];
			if (pos1 <= insize)
				len++;
			else
				in[i] = 0;
		}
		if (len) {
			encodeblock(in, out, len);
			for (i = 0; i < 4; i++)
				output[pos2++] = out[i];
		}
	}
}

static int coredump_alloc_encrypt_workspaces(struct minimal_coredump_params*
		cprm)
{
	cc_u32 RSA_KeyByteLen = RSA_KEYBYTE_SIZE;
	int	ret = -ENOMEM;

#ifdef MINIMAL_CORE_DEBUG
	pr_alert("coredump allocating encryption workspaces\n");
#endif
	cprm->pbbuf_mod = kmalloc((SDRM_RSA_ALLOC_SIZE), GFP_KERNEL);
	if (NULL == cprm->pbbuf_mod)
		goto out;

	cprm->pbbuf_exp = kmalloc((SDRM_RSA_ALLOC_SIZE), GFP_KERNEL);
	if (NULL == cprm->pbbuf_exp)
		goto exp_fail;

	cprm->pbbuf_base = kmalloc((SDRM_RSA_ALLOC_SIZE), GFP_KERNEL);
	if (NULL == cprm->pbbuf_base)
		goto base_fail;

	cprm->pbbuf_out = kmalloc((SDRM_RSA_ALLOC_SIZE), GFP_KERNEL);
	if (NULL == cprm->pbbuf_out)
		goto out_fail;

	cprm->mod = SDRM_BN_Alloc((cc_u8 *)cprm->pbbuf_mod,
			SDRM_RSA_BN_BUFSIZE);
	cprm->exp = SDRM_BN_Alloc((cc_u8 *)cprm->pbbuf_exp,
			SDRM_RSA_BN_BUFSIZE);
	cprm->base = SDRM_BN_Alloc((cc_u8 *)cprm->pbbuf_base,
			SDRM_RSA_BN_BUFSIZE);
	cprm->out = SDRM_BN_Alloc((cc_u8 *)cprm->pbbuf_out,
			SDRM_RSA_BN_BUFSIZE);
	cprm->bufs_index = 0;
	ret = SDRM_OS2BN(gn_minimal_core, RSA_KEYBYTE_SIZE, cprm->mod);
	if (ret != CRYPTO_SUCCESS)
		goto enc_fail;
	SDRM_BN_OPTIMIZE_LENGTH(cprm->mod);
	ret = SDRM_OS2BN(ge_minimal_core, sizeof(ge_minimal_core), cprm->exp);
	if (ret != CRYPTO_SUCCESS)
		goto enc_fail;
	SDRM_BN_OPTIMIZE_LENGTH(cprm->exp);

	cprm->aes_enc_buf = kmalloc(AES_ENC_BUF_SIZE, GFP_KERNEL);
	if (NULL == cprm->aes_enc_buf) {
		ret = -ENOMEM;
		goto enc_fail;
	}

	cprm->cip  = crypto_alloc_cipher("aes", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(cprm->cip)) {
		ret = -ENOMEM;
		goto cipher_fail;
	}

#ifdef MINIMAL_CORE_DEBUG
	pr_alert("coredump allocated encryption workspaces\n");
#endif
	ret = 0;
	goto out;

cipher_fail:
	kfree(cprm->aes_enc_buf);
enc_fail:
	kfree(cprm->pbbuf_out);
out_fail:
	kfree(cprm->pbbuf_base);
base_fail:
	kfree(cprm->pbbuf_exp);
exp_fail:
	kfree(cprm->pbbuf_mod);

out:
	return ret;
}

static void coredump_free_encrypt_workspaces(struct minimal_coredump_params*
		cprm)
{
	SDRM_BN_FREE(cprm->pbbuf_mod);
	SDRM_BN_FREE(cprm->pbbuf_exp);
	SDRM_BN_FREE(cprm->pbbuf_base);
	SDRM_BN_FREE(cprm->pbbuf_out);
	crypto_free_cipher(cprm->cip);
	kfree(cprm->aes_enc_buf);

	return;
}

static int encrypt_write(const char *tmp, int len_available, int last_write,
			struct minimal_coredump_params *cprm)
{
	int i = 0;
	int bytes_remain = 0;
	int bytes_to_enc = 0;
	char *p_aes_buf;
	int ret = 0;

	p_aes_buf = cprm->aes_enc_buf;

	if (cprm->rsa_enc_aes_key) {
#ifdef MINIMAL_CORE_DEBUG
		pr_alert("Generating AES Key\n");
#endif
		cprm->rsa_enc_aes_key = 0;
		get_random_bytes(cprm->aes_key, AES_KEY_LEN);
		crypto_cipher_setkey(cprm->cip, cprm->aes_key, AES_KEY_LEN);
		memcpy(cprm->buf_encode_in, cprm->aes_key,
			sizeof(cprm->aes_key));
		b64_encode(cprm->buf_encode_in,
				cprm->buf_encode_out, RSA_ENC_AES_KEY_SIZE);
		ret = SDRM_OS2BN(cprm->buf_encode_out, ENCODE_OUT_SIZE,
				cprm->base);
		if (ret != CRYPTO_SUCCESS) {
			pr_alert(" Cannot encrypt while dumpoing\n");
			goto out;
		}
		ret = SDRM_BN_ModExp(cprm->out, cprm->base, cprm->exp,
				cprm->mod);
		if (ret != CRYPTO_SUCCESS) {
			pr_alert(" Cannot encrypt while dumpoing\n");
			goto out;
		}

#ifdef MINIMAL_CORE_DEBUG
		pr_alert("AES Key encrypted with RSA public key\n");
#endif
		for (i = ENCODE_OUT_SIZE - 1; i >= 0; i--) {
			*p_aes_buf++ = SDRM_CheckByteUINT32(cprm->out->pData,
					i);
		}

		p_aes_buf = cprm->aes_enc_buf;

		if (!dump_write(cprm->file, p_aes_buf, RSA_ENC_AES_KEY_SIZE)) {
			pr_alert("ERROR: mincore write\n");
			ret = -EIO;
			goto out;
		}

#ifdef MINIMAL_CORE_DEBUG
		pr_alert("AES Key encrypted written to corefile\n");
#endif
	}

	while (len_available) {
		if (len_available < (AES_ENC_BUF_SIZE - cprm->bufs_index)) {
				memcpy(p_aes_buf + cprm->bufs_index, tmp,
					(size_t)len_available);
				cprm->bufs_index += len_available;
				len_available = 0;
		} else {
			memcpy(p_aes_buf + cprm->bufs_index, tmp,
				(size_t)(AES_ENC_BUF_SIZE - cprm->bufs_index));

			len_available -= (AES_ENC_BUF_SIZE - cprm->bufs_index);
			tmp += (AES_ENC_BUF_SIZE - cprm->bufs_index);
			cprm->bufs_index = 0;

			/* encrypt 512 byte block here  */

			for (i = 0; i < AES_ENC_BUF_SIZE; i += AES_BLK_SIZE) {
				crypto_cipher_encrypt_one(cprm->cip,
					&p_aes_buf[i], &p_aes_buf[i]);
			}

			if (!dump_write(cprm->file, p_aes_buf,
					AES_ENC_BUF_SIZE)) {
				pr_alert("ERROR: mincore write\n");
				ret = -EIO;
				goto out;
			}
		}
	}

	if (last_write) {
		if (cprm->bufs_index) {
			bytes_remain = cprm->bufs_index % AES_BLK_SIZE;
			bytes_to_enc = cprm->bufs_index - bytes_remain;

			if (bytes_to_enc) {
				for (i = 0; i < bytes_to_enc;
					i += AES_BLK_SIZE) {
					crypto_cipher_encrypt_one(cprm->cip,
						&p_aes_buf[i], &p_aes_buf[i]);
				}

				if (!dump_write(cprm->file, p_aes_buf,
					bytes_to_enc)) {
					pr_alert(
					"ERROR: mincore write\n");
					ret = -EIO;
					goto out;
				}
			}

			/* dump the remaing bytes  */
			if (bytes_remain) {
				if (!dump_write(cprm->file,
					p_aes_buf + bytes_to_enc,
					bytes_remain)) {
					pr_alert(
					"ERROR: mincore write\n");
					ret = -EIO;
					goto out;
				}
			}

		}
		cprm->rsa_enc_aes_key = 1;
	}

out:
	return ret;
}

static int set_gzip_header(struct minimal_coredump_params *cprm)
{
	struct gzip_header gzip_hdr;
	struct timeval ktv;
	const char *filename = "0.gz";
	int ret = 0;

	/* gzip ID */
	gzip_hdr.id[0] = 0x1f;
	gzip_hdr.id[1] = 0x8b;

	/* CM - Compressed Method */
	gzip_hdr.cm = 8;

	/* FLG - flag=8 write original file name */
	gzip_hdr.flag = 8;

	/* MTime - Modification Time */
	do_gettimeofday(&ktv);
	memcpy(gzip_hdr.mtime, &ktv.tv_sec, sizeof(time_t));

	/* XFL - eXtra Flags */
	gzip_hdr.xfl = 2;

	/* OS - OS Filesystem */
	gzip_hdr.os = 3;

	ret = encrypt_write((char *)&gzip_hdr, sizeof(struct gzip_header),
				!LAST_WRITE, cprm);
	if (ret) {
		pr_alert("Can't encrypt_write minimal corefile\n");
		goto out;
	}

	ret = encrypt_write(filename, (int)strlen(filename)+1,
				!LAST_WRITE, cprm);
	if (ret) {
		pr_alert("Can't encrypt_write minimal corefile\n");
		/* goto out; */
		/* FALLTHROUGH to out */
	}

out:
	return ret;
}

static int coredump_alloc_workspaces(struct minimal_coredump_params *cprm)
{
	cprm->def_strm.workspace = vmalloc((long unsigned int)
					zlib_deflate_workspacesize(MAX_WBITS,
						 MAX_MEM_LEVEL));
	if (!cprm->def_strm.workspace) {
		pr_alert("zlib workspace allocation failure\n");
		return -ENOMEM;
	}

	cprm->comp_buf = kmalloc(MINIMAL_CORE_COMP_BUFSIZE, GFP_KERNEL);
	if (NULL == cprm->comp_buf) {
		pr_alert(" Cannot allocate compression buffer\n");
		vfree(cprm->def_strm.workspace);
		return -ENOMEM;
	}
#ifdef MINIMAL_CORE_DEBUG
	pr_alert("allocated workspace for compression\n");
#endif
	return 0;
}

static void coredump_free_workspaces(struct minimal_coredump_params *cprm)
{
#ifdef MINIMAL_CORE_DEBUG
	pr_alert("freeing workspace for compression\n");
#endif

	if (cprm->def_strm.workspace)
		vfree(cprm->def_strm.workspace);
	kfree(cprm->comp_buf);
	cprm->def_strm.workspace = NULL;
	cprm->comp_buf = NULL;
#ifdef MINIMAL_CORE_DEBUG
	pr_alert("Freed workspace for compression\n");
#endif
}

static int minimal_dump_write(struct minimal_coredump_params *cprm,
					const void *addr, int nr)
{
	int ret = 0;

	if (!nr)
		goto out;

	/* If this is the first write, then we need to write
	   * gzip header first, allocate zlib workspace,
	   * calculate crc value
	   */
	if (MINIMAL_CORE_INIT_STATE == cprm->minimal_core_state) {
#ifdef MINIMAL_CORE_DEBUG
		pr_alert("Initializing compression\n");
#endif
		cprm->def_strm.workspace = NULL;
		cprm->comp_buf = NULL;

		cprm->rsa_enc_aes_key = 1;
		ret = coredump_alloc_encrypt_workspaces(cprm);
		if (ret) {
			pr_alert("Mincore: workspce alloc failure\n");
			goto out;
		}
		ret = coredump_alloc_workspaces(cprm);
		if (ret) {
			pr_alert("Mincore: workspce alloc failure\n");
			goto free_encrypt;
		}

		ret = set_gzip_header(cprm);
		if (ret) {
			pr_alert("Mincore: can't set GZIP header\n");
			goto free_workspace;
		}

		if (Z_OK != zlib_deflateInit2(&(cprm->def_strm), 8,
				Z_DEFLATED, -MAX_WBITS,
				DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY)) {
			pr_alert("zlib init failure\n");
			ret = -EIO;
			goto free_workspace;
		}
		cprm->total_size = 0;
		/* Fixme : - we need a mutex for this */
		updcrc(&cprm->seed_crc, NULL, 0);
#ifdef MINIMAL_CORE_DEBUG
		pr_alert("Initialised compression\n");
#endif
		cprm->minimal_core_state = MINIMAL_CORE_COMP_STATE;
	}

	if (!access_ok(VERIFY_READ, addr, nr)) {
		pr_alert("Can't access address %p\n", addr);
		ret = -EIO;
		goto free_workspace;
	}

	cprm->def_strm.next_in = addr;
	cprm->def_strm.total_in = 0;

	cprm->def_strm.next_out = cprm->comp_buf;
	cprm->def_strm.total_out = 0;

	cprm->def_strm.avail_out = MINIMAL_CORE_COMP_BUFSIZE;
	cprm->def_strm.avail_in = (unsigned int)nr;
	cprm->total_size += (unsigned int)nr;

	/* crc32 check, used in GZIP tailer */
	cprm->crc = updcrc(&cprm->seed_crc, cprm->def_strm.next_in,
				cprm->def_strm.avail_in);

	if (MINIMAL_CORE_COMP_STATE == cprm->minimal_core_state) {
		do {
			ret = zlib_deflate(&(cprm->def_strm), Z_PARTIAL_FLUSH);

#ifdef MINIMAL_CORE_DEBUG_DEFLATEBLOCK
		pr_alert("Deflated... %p %d bytes\n", addr, nr);
#endif
			if (ret != Z_OK) {
				pr_alert("zlib deflate failure\n");
				zlib_deflateEnd(&(cprm->def_strm));
				ret = -EIO;
				goto free_workspace;
			}

			ret = encrypt_write(cprm->comp_buf,
				(int)cprm->def_strm.total_out, !LAST_WRITE,
					cprm);
			if (ret) {
				pr_alert("mincore : write fail\n");
				goto free_workspace;
			}
			cprm->def_strm.total_out = 0;
			cprm->def_strm.next_out = cprm->comp_buf;
			cprm->def_strm.avail_out = MINIMAL_CORE_COMP_BUFSIZE;
		} while (cprm->def_strm.avail_in);

		goto out;
	} else if (MINIMAL_CORE_FINI_STATE == cprm->minimal_core_state) {
		do {
			ret = zlib_deflate(&(cprm->def_strm), Z_FINISH);
			if ((ret != Z_OK) && (ret != Z_STREAM_END)) {
				pr_alert("Zlib deflate failure (last page)\n");
				zlib_deflateEnd(&(cprm->def_strm));
				ret = -EIO;
				goto free_workspace;
			}

			if (encrypt_write(cprm->comp_buf,
				(int)cprm->def_strm.total_out, !LAST_WRITE,
				cprm)) {
				pr_alert("mincore: encrypt_write fail\n");
				ret = -EIO;
				goto free_workspace;
			}
			cprm->def_strm.total_out = 0;
			cprm->def_strm.next_out = cprm->comp_buf;
			cprm->def_strm.avail_out = MINIMAL_CORE_COMP_BUFSIZE;
		} while (ret == Z_OK);

		/* Now write the gzip tailer as explained
		*in gzip-1.6/algorithm.doc */
		cprm->crc = le32_to_cpu((cprm->crc));
		cprm->total_size = le32_to_cpu((cprm->total_size));
#ifdef MINIMAL_CORE_DEBUG
		pr_alert("crc = 0x%08lx total_size = %d\n",
				cprm->crc, cprm->total_size);
#endif
		ret = encrypt_write((char *)&cprm->crc,
			sizeof(cprm->crc), !LAST_WRITE, cprm);
		if (ret) {
			pr_alert("mincore: encrypt write fail\n");
			goto free_workspace;
		}
		ret = encrypt_write((char *)&cprm->total_size,
			sizeof(cprm->total_size), LAST_WRITE, cprm);
		if (ret) {
			pr_alert("mincore: write fail\n");
			goto free_workspace;
		}
	}

free_workspace:
	coredump_free_workspaces(cprm);
free_encrypt:
	coredump_free_encrypt_workspaces(cprm);

out:
	return ret;
}

static int minimal_dump_seek(struct minimal_coredump_params *cprm, loff_t off)
{
	int ret = 0;
	/* tmp_var variable used to avoid warning "cast from function call"
	by using extra KBIULD_CFLAGS -Wbad-function-cast */
	unsigned long tmp_var;
	char *buf;
	tmp_var = get_zeroed_page(GFP_KERNEL);
	buf = (char *)tmp_var;

#ifdef MINIMAL_CORE_DEBUG_DETAIL
		pr_alert("lseek... Total %lu bytes\n",
			(unsigned long)off);
#endif

	if (!buf)
		return 0;
	while (off > 0) {
		long long n = off;

#ifdef MINIMAL_CORE_DEBUG_DETAIL
		pr_alert("lseek... %lu bytes\n", (unsigned long)off);
#endif
		if (n > PAGE_SIZE)
			n = PAGE_SIZE;
		ret = minimal_dump_write(cprm, buf, (int)n);
		if (ret)
			break;
		off -= n;
	}
	free_page((unsigned long)buf);

	return ret;
}

#define MINIMAL_CORE_WRITE(addr, nr, foffset)  \
	do { \
		if (minimal_dump_write(cprm, (addr), (nr)))\
			return -EIO;\
		*foffset += (nr);\
	} while (0)

static int write_mini_note(struct memelfnote *men,
		struct minimal_coredump_params *cprm, loff_t *foffset)
{
	struct elf_note en;
	const char buf[4] = { 0, };

	en.n_namesz = strlen(men->name) + 1;
	en.n_descsz = men->datasz;
	en.n_type = (Elf32_Word)men->type;

#ifdef MINIMAL_CORE_DEBUG_DETAIL
	pr_alert("Writing notes %s size=%d type=%d\n", men->name,
				en.n_descsz, en.n_type);
#endif
	MINIMAL_CORE_WRITE(&en, sizeof(en), foffset);
	MINIMAL_CORE_WRITE(men->name, (int)en.n_namesz, foffset);
	/* align to 4 byte */
	MINIMAL_CORE_WRITE(buf, (int)(roundup(*foffset, 4) - *foffset),
			foffset);
	MINIMAL_CORE_WRITE(men->data, (int)men->datasz, foffset);
	/* align to 4 byte */
	MINIMAL_CORE_WRITE(buf, (int)(roundup(*foffset, 4) - *foffset),
			foffset);

	return 0;
}

#undef MINIMAL_CORE_WRITE

static int notesize(struct memelfnote *en)
{
	int sz;

	sz = sizeof(struct elf_note);
	sz += (int)roundup((int)strlen(en->name) + 1, 4);
	sz += (int)roundup((int)en->datasz, 4);

	return sz;
}

#ifdef CORE_DUMP_USE_REGSET
/*
   * Write all the notes for each thread.  When writing the first thread, the
   * process-wide notes are interleaved after the first thread-specific note.
   */
static int write_note_info(struct elf_note_info *info,
		struct minimal_coredump_params *cprm, loff_t *foffset)
{
	struct elf_thread_core_info *t = info->thread;
	int i;
#ifdef CONFIG_FUTEX_DLA
	struct dla_node *dla_node;
#endif
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	struct open_file_info *file_node;
	int dump_files = 0;
#endif

#ifdef MINIMAL_CORE_DEBUG
	pr_alert("Writing notes content\n");
#endif

	if (write_mini_note(&t->notes[0], cprm, foffset))
		return -1;

	if (write_mini_note(&info->psinfo, cprm, foffset))
		return -1;

	if (write_mini_note(&info->signote, cprm, foffset))
		return -1;
	if (write_mini_note(&info->auxv, cprm, foffset))
		return -1;
	if (write_mini_note(&info->files, cprm, foffset))
		return -1;

	for (i = 1; i < info->thread_notes; ++i)
		if (t->notes[i].data &&
				write_mini_note(&t->notes[i], cprm,
					foffset))
			return -1;

#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	list_for_each_entry(file_node, &file_rlimit_list, node) {
		if (file_node->overflow_tsk->tgid == current->tgid) {
			dump_files = 1;
			break;
		}
	}

	if (!dump_files)
		goto process_next; /* to check dla if available */

	/* to skip already added note sectio of first thread    */
	t = t->next;
	while (t) {

		list_for_each_entry(file_node, &file_rlimit_list, node) {
			if ((file_node->tsk->tgid == current->tgid) &&
					(file_node->tsk == t->task)) {
				if (write_mini_note(&t->notes[0],
							cprm, foffset))
					return -1;
				for (i = 1; i < info->thread_notes; ++i)
					if (t->notes[i].data &&
						write_mini_note(&t->notes[i],
							cprm, foffset))
						return -1;
				break;
			}
		}
		t = t->next;
	}

	if (write_mini_note(&file_rlimit, cprm, foffset))
		return -1;
	goto out;
process_next:
#endif

#ifdef CONFIG_FUTEX_DLA
	t = t->next;
	/* Note : There should be No duplicate entries in futex_dla_list */
	while (t) {
		list_for_each_entry(dla_node, &futex_dla_list, node) {
			if ((dla_node->task->tgid == current->tgid) &&
					(dla_node->task == t->task)) {
				if (write_mini_note(&t->notes[0],
							cprm, foffset))
					return -1;
				for (i = 1; i < info->thread_notes; ++i)
					if (t->notes[i].data &&
							write_mini_note(
								&t->notes[i],
								cprm, foffset))
						return -1;
				break;
			}
		}
		t = t->next;
	}
#endif
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
out:
#endif
	return 0;
}

static size_t find_note_size(struct elf_note_info *info)
{
	struct elf_thread_core_info *t = info->thread;
	int i;
	size_t sz;
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	int dump_files = 0;
	struct open_file_info *file_node;
#endif
#ifdef CONFIG_FUTEX_DLA
	struct dla_node *dla_node;
#endif

	sz = (size_t)notesize(&t->notes[0]);
	sz += (size_t)notesize(&info->psinfo);
	sz += (size_t)notesize(&info->signote);
	sz += (size_t)notesize(&info->auxv);
	sz += (size_t)notesize(&info->files);

	for (i = 1; i < info->thread_notes; ++i)
		if (t->notes[i].data)
			sz += (size_t)notesize(&t->notes[i]);

#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	list_for_each_entry(file_node, &file_rlimit_list, node) {
		if (file_node->overflow_tsk->tgid == current->tgid) {
			dump_files = 1;
			break;
		}
	}

	if (!dump_files)
		goto process_next; /* to check dla if available */

	/* to skip already added note section of first thread	*/
	t = t->next;
	while (t) {

		list_for_each_entry(file_node, &file_rlimit_list, node) {
			if ((file_node->tsk->tgid == current->tgid) &&
					(file_node->tsk == t->task)) {
				sz += (size_t)notesize(&t->notes[0]);
				for (i = 1; i < info->thread_notes; ++i)
					if (t->notes[i].data)
						sz +=
						(size_t)notesize(&t->notes[i]);
				break;
			}
		}
		t = t->next;
	}
	sz += (size_t)notesize(&file_rlimit);
	goto out;
process_next:
#endif

#ifdef CONFIG_FUTEX_DLA
	t = t->next;
	/* Note : There should be No duplicate entries in futex_dla_list */
	while (t) {
		list_for_each_entry(dla_node, &futex_dla_list, node) {
			if ((dla_node->task->tgid == current->tgid) &&
					(dla_node->task == t->task)) {
				sz += (size_t)notesize(&t->notes[0]);
				for (i = 1; i < info->thread_notes; ++i)
					if (t->notes[i].data)
						sz += (size_t)notesize
								(&t->notes[i]);
				break;
			}
		}
		t = t->next;
	}
#endif
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
out:
#endif
	return sz;
}
#else

static int write_note_info(struct elf_note_info *info,
		struct minimal_coredump_params *cprm, loff_t *foffset)
{
	int i;
	struct elf_thread_status *tmp;
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	int dump_files = 0;
	struct list_head *t_files;
	struct open_file_info *file_node;
	bool first_file = 1;
#endif
#ifdef CONFIG_FUTEX_DLA
	struct list_head *t;
	struct dla_node *dla_node;
	bool first = 1;
#endif

	for (i = 0; i < info->numnote; i++)
		if (write_mini_note(info->notes + i, cprm, foffset))
			return -1;

	/* write out the thread status notes section */
	tmp = list_first_entry(&info->thread_list,
			struct elf_thread_status, list);

	for (i = 0; i < tmp->num_notes; i++)
		if (write_mini_note(&tmp->notes[i], cprm, foffset))
			return -1;

#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	list_for_each_entry(file_node, &file_rlimit_list, node) {
		if (file_node->overflow_tsk->tgid == current->tgid) {
			dump_files = 1;
			break;
		}
	}

	if (!dump_files)
		goto process_next; /* to check dla if available */

	list_for_each(t_files, &info->thread_list) {
		struct elf_thread_status *tmp =
			list_entry(t_files, struct elf_thread_status, list);

		if (first_file) {
			first_file = 0;
			continue;
		}

		list_for_each_entry(file_node, &file_rlilmit_list, node) {
			if ((file_node->tsk-tgid == current->tgid) &&
					(file_node->tsk == tmp->thread)) {
				for (i = 0; i < tmp->num_notes; i++)
					if (write_mini_note(&tmp->notes[i],
								cprm, foffset))
						return -1;
				break;
			}
		}
	}

	write_mini_note(&file_rlimit, cprm, foffset);
	goto out;
process_next:
#endif
#ifdef CONFIG_FUTEX_DLA
	list_for_each(t, &info->thread_list) {
		struct elf_thread_status *tmp =
			list_entry(t, struct elf_thread_status, list);
		if (first) {
			first = 0;
			continue;
		}
		/* Note:There should No duplicate entries in futex_dla_list */
		list_for_each_entry(dla_node, &futex_dla_list, node) {
			if ((dla_node->task-tgid == current->tgid) &&
					(dla_node->task == tmp->thread)) {
				for (i = 0; i < tmp->num_notes; i++)
					if (write_mini_note(&tmp->notes[i],
								cprm, foffset))
						return -1;
				break;
			}
		}
	}
#endif
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
out:
#endif
	return 0;
}

static size_t find_note_size(struct elf_note_info *info)
{
	int i;
	size_t sz;
	struct elf_thread_status *tmp;
#ifdef CONFIG_FUTEX_DLA
	struct list_head *t;
	struct dla_node *dla_node;
	bool first = 1;
#endif
#ifdef CONFIG_MINCORE_ROLIMIT_NOFILE
	int dump_files = 0;
	bool first_file = 1;
	struct list_head *t_files;
	struct open_file_info *file_node;
#endif

	sz = 0;
	tmp = list_first_entry(&info->thread_list,
			struct elf_thread_status, list);

	for (i = 0; i < tmp->num_notes; i++)
		sz += notesize(&tmp->notes[i]);

#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	list_for_each_entry(file_node, &file_rlimit_list, node) {
		if (file_node->overflow_tsk->tgid == current->tgid) {
			dump_files = 1;
			break;
		}
	}

	if (!dump_files)
		goto process_next; /* to check dla if available */

	list_for_each(t_files, &info->thread_list) {
		struct elf_thread_status *tmp =
			list_entry(t_files, struct elf_thread_status, list);

		if (first_file) {
			first_file = 0;
			continue;
		}

		list_for_each_entry(file_node, &file_rlilmit_list, node) {
			if ((file_node->tsk-tgid == current->tgid) &&
					(file_node->tsk == tmp->thread)) {
				for (i = 0; i < tmp->num_notes; i++)
					sz += notesize(&tmp->notes[i]);
				break;
			}
		}
	}

	sz += notesize(&file_rlimit);
	goto out;
process_next:
#endif
#ifdef CONFIG_FUTEX_DLA
	list_for_each(t, &info->thread_list) {
		struct elf_thread_status *tmp =
			list_entry(t, struct elf_thread_status, list);
		if (first) {
			first = 0;
			continue;
		}

		/* Note:There should No duplicate entries in futex_dla_list */
		list_for_each_entry(dla_node, &futex_dla_list, node) {
			if ((dla_node->task-tgid == current->tgid) &&
					(dla_node->task == tmp->thread)) {
				for (i = 0; i < tmp->num_notes; i++)
					sz += notesize(&tmp->notes[i]);
				break;
			}
		}
	}
#endif
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
out:
#endif
	return sz;
}
#endif

static int add_to_svma(unsigned long vm_start, unsigned long vm_end,
		unsigned long vm_flags, struct file *vm_file,
		unsigned long dumped, struct list_head *psvma_list)
{
	struct svma_struct *svma;

	svma = kzalloc(sizeof(struct svma_struct), GFP_KERNEL);
	if (NULL == svma)
		return -1;

	svma->vm_start = vm_start;
	svma->vm_end = vm_end;
	svma->vm_flags = vm_flags;
	svma->vm_file = vm_file;
	svma->dumped = dumped;
#ifdef MINIMAL_CORE_DEBUG
	pr_alert("0x%08lx-0x%08lx file=%p %s flags = 0x%08lx\n",
			vm_start, vm_end, vm_file,
			dumped ? "Dumped" : "NOT dumped",
			vm_flags);
#endif
	list_add_tail(&svma->node, psvma_list);

	return 0;
}

static int cmp_score(const void *a, const void *b)
{
	if ((*(const unsigned long *)a) < (*(const unsigned long *)b))
		return -1;
	return 0;
}

static int shortlist_dumpable(unsigned long *score_regs, unsigned long nregs,
			struct addrvalue *addrvalue,
			struct list_head *psvma_list)
{
	int i = 0;
	int found = 0;
	int naddrvalue = 0;
	struct svma_struct *svma;

	/* 2. Now lets sort */
	sort(score_regs, nregs, sizeof(unsigned long), cmp_score, NULL);

	for (i = 0; i < (int)nregs; i++) {
		found = 0;
#ifdef MINIMAL_CORE_DEBUG
		pr_alert("regs %d = 0x%08lx\n", i, score_regs[i]);
#endif
		list_for_each_entry(svma, psvma_list, node)	{
			/* Do not include NULL and already dumped */
			if ((!score_regs[i]) ||
				((score_regs[i] >= svma->vm_start) &&
				(score_regs[i] < svma->vm_end))) {
				/* If the address falls in already listed area
				* then do not consider */
				found = 1;
				break;
			}
		}

#ifdef MINIMAL_CORE_DEBUG
		if (found) {
			pr_alert("0x%08lx falls in 0x%08lx-0x%08lx\n",
					score_regs[i],
					score_regs[i] ? svma->vm_start : 0,
					score_regs[i] ? svma->vm_end : 0);
		}
#endif
		if (!found) {
			addrvalue[naddrvalue].start = rounddown(score_regs[i],
					PAGE_SIZE) - PAGE_SIZE;
			addrvalue[naddrvalue].end = roundup((score_regs[i] + 1),
					PAGE_SIZE) + PAGE_SIZE;
			addrvalue[naddrvalue].dumped = 1;
			pr_alert(
				"including addr range 0x%08lx to 0x%08lx\n",
				addrvalue[naddrvalue].start,
				addrvalue[naddrvalue].end);
			naddrvalue++;
		}
	}

	return naddrvalue;
}

static void trim_overlapping(int naddrvalue, struct addrvalue *addrvalue,
				struct list_head *psvma_list)
{
	struct svma_struct *svma;
	int i = 0;

	for (i = 0; i < naddrvalue; i++) {
		list_for_each_entry(svma, psvma_list, node)	{
			if ((addrvalue[i].start >= svma->vm_start) &&
				(addrvalue[i].start < svma->vm_end)) {
				pr_alert(
				"Rm overlap pge 0x%08lx 0x%08lx-0x%08lx\n",
				addrvalue[i].start, svma->vm_start,
				svma->vm_end);
				addrvalue[i].start += PAGE_SIZE;
			}
			if ((addrvalue[i].end <= svma->vm_end) &&
				(addrvalue[i].end > svma->vm_start)) {
				pr_alert(
				"Rm overlap pg 0x%08lx in 0x%08lx-0x%08lx\n",
				addrvalue[i].end, svma->vm_start,
				svma->vm_end);
				addrvalue[i].end -= PAGE_SIZE;
			}
		}
	}
}

static void filter_nondumpable(int naddrvalue, struct addrvalue *addrvalue,
				struct mm_struct *mm)
{
	int j = 0;
	struct vm_area_struct *vma;
	struct vm_area_struct *prev_vma = NULL;
	unsigned long addr;

	for (j = 0; j < naddrvalue; j++) {
		/* ASSUMPTION, vmas are sorted by address
		*  vm_next in struct vm_area_struct says
		*  "linked list of VM areas per task, sorted by address"
		*/
		prev_vma = NULL;
		for (vma = mm->mmap; vma != NULL; vma = (vma->vm_next)) {

			/* If this is a read only area or a shared
			 * mem area, or non dumpable area, skip
			 */
			if ((vma->vm_flags & VM_DONTDUMP)
				|| (!(vma->vm_flags & VM_WRITE))
				|| (vma->vm_flags & VM_MAYSHARE))
				continue;

			/* Address is not in any VMA
			*	 a. |  addr hole  | <-- addr
			*       |  addr hole  |
			*       |  first vma  | <-- vm_start
			*
			*    b. |    prev_vma |
			*       |             |<-- vm_end
			*       l   addr hole l
			*       l   addr hole l <-- addr
			*       l   addr hole l
			*       |    vma	   | <-- vm_start
			*       |             |
			*
			*    c. |  last vma   |
			*       |             |
			*       |   addr hole | <-- addr
			*/
			for (addr = addrvalue[j].start;
					addr < addrvalue[j].end;
					addr += PAGE_SIZE) {
				/* Removing unmapped areas */
				if (((NULL == prev_vma) &&
				(addr < vma->vm_start)) ||
				((prev_vma && (addr >= prev_vma->vm_end)) &&
				(addr < vma->vm_start)) ||
				 ((NULL == vma->vm_next) &&
				(addr >= vma->vm_end))) {
					pr_alert(
						"Rm unmapped page 0x%08lx\n",
						addr);
					if (addr == addrvalue[j].start) {
						addrvalue[j].start +=
								PAGE_SIZE;
					} else {
						addrvalue[j].end = addr;
					}
				}
			}
			/* Now remove all the mappings for which start >= end
			 * This could happen as a result of adjusting addresses
			 * which point to holes
			 */
			if (addrvalue[j].start >= addrvalue[j].end)
				addrvalue[j].dumped = 0;
			prev_vma = vma;
		}
	}
}
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
static void free_file_list(void)
{
	struct open_file_info *file_node;
	struct open_file_info *file_node_other;

	list_for_each_entry_safe(file_node, file_node_other,
			&file_rlimit_list, node) {
		if (file_node->overflow_tsk->tgid == current->tgid) {
			/* init task is never going to exit so get_task_struct
			 * was NOT done for init_task
			 */
			if (file_node->tsk != &init_task)
				put_task_struct(file_node->tsk);
			list_del(&file_node->node);
			kfree(file_node->name);
			kfree(file_node->owner_name);
			kfree(file_node);
		}
	}

}

#endif

static void merge_adjacent_areas(int naddrvalue, struct addrvalue *addrvalue)
{
	int i = 0;
	int j = 0;

	for (i = 0; i < (naddrvalue-1); i++) {
		/* if addr range is adjacent,
			merge and start from same again  */
		/* 3 cases
			a. Same //comparing start/end is enough in this case
			b. overlapping if i.end >= i+1.start &&
					i.end <= i+1.end
			c. Contingous
		*/
		if (!addrvalue[i].dumped)
			continue;

		/* find next dumped addrvalue range */
		for (j = i+1; j < naddrvalue; j++) {
			if (!(addrvalue[j].dumped))
				continue;
			if (addrvalue[i].end < addrvalue[j].start) {
				/* not an adjacent area,
				 * so no need to process anymore */
				break;
			}
			if ((addrvalue[i].start == addrvalue[j].start) ||
				((addrvalue[i].end >= addrvalue[j].start) &&
				(addrvalue[i].end <= addrvalue[j].end))) {
					addrvalue[i].end = addrvalue[j].end;
					addrvalue[j].dumped = 0;
					pr_alert(
				"Merging 0x%08lx-0x%08lx 0x%08lx-0x%08lx\n",
					addrvalue[i].start, addrvalue[i].end,
					addrvalue[j].start, addrvalue[j].end);
			}
		}
	}
}
/* r0-r12, orig_r0 */
static unsigned long extend_svma_list(struct pt_regs *regs,
			struct mm_struct *mm, struct list_head *psvma_list)
{
	int i = 0;
	unsigned long score_regs[ARM_NREGS];
	int naddrvalue = 0;
	unsigned long nvmas = 0;
	struct addrvalue addrvalue[ARM_NREGS];

	/*****************************************************************
	* According to Req we need to include pages                      *
	*                     ________________                           *
	*                   |                  |                         *
	*                   |                  |                         *
	*                   |                  |                         *
	*                   |                  |                         *
	*                   |------------------|                         *
	*                   |                  |                         *
	*                   |                  |                         *
	* ARM gp reg  -->   |                  |                         *
	*                   |                  |                         *
	*                   |------------------|                         *
	*                   |                  |                         *
	*                   |                  |                         *
	*                   |                  |                         *
	*                   |                  |                         *
	*                   |------------------|                         *
	*                                                                *
	* Registers considered                                           *
	*     If any of the registers from r0 - r12                      *
	*                                 r13 -> sp noneed               *
	*                                 r14 -> lr noneed               *
	*                                 r15 -> pc noneed               *
	*                                                                *
	* Alogrithm, sort all registers in ascending                     *
	* order based on value                                           *
	*                                                                *
	*  steps :                                                       *
	*      sort addresses                                            *
	*      if some of the addresses are falling                      *
	*           within the same group, omit all except one           *
	*      After this merge address groups if they overlap           *
	*                                                                *
	******************************************************************/
	 /* 1. copy to score_regs */
	memcpy(score_regs, &(regs->uregs[0]), (sizeof(unsigned long) * 13));
	score_regs[13] = regs->ARM_ORIG_r0;

	/* pr_alert("Extending svma list sorting done\n"); */
	/* Consider only if value held by register
	* is a valid dumpable memory,other than vmas areas
	* which are already dumped
	*/
	naddrvalue = shortlist_dumpable(score_regs, ARM_NREGS, addrvalue,
					psvma_list);

	/* We have extended the range (-4k, page referred, +4k)
	*  we only have made
	* sure that page referred by regs is not in already dumped area
	* we now have to make sure that extended range does not fall in already
	* dumped area
	* We are sure that "page referred" is not overlapping
	* with already dumped
	* memory
	*/

	trim_overlapping(naddrvalue, addrvalue, psvma_list);

	/* Now we have a list of areas which are not in list (svma_list)
	* already */

	/* Let us filter out address which do not have any VMA mapping */
	filter_nondumpable(naddrvalue, addrvalue, mm);

	/* There is one more thing which needs to be done
	*   Find adjacent areas within ranges available, merge them
	*/
	/* Merging adjacent areas */
	merge_adjacent_areas(naddrvalue, addrvalue);

	/* Let us extend the svma_list */
	for (i = 0; i < naddrvalue; i++) {
		/* Just create VMA entries for this don't dump */
		if (addrvalue[i].dumped) {
			if (add_to_svma(addrvalue[i].start, addrvalue[i].end,
				(VM_WRITE | VM_READ), NULL, 1, psvma_list)) {
				/* No data structures are alloced
				 * None to be cleaned, list will be
				 * freed by caller
				 */
				return ERROR_CONDITION;
			}
			nvmas++;
			pr_alert(
			"Including vma range 0x%08lx-0x%08lx\n",
				addrvalue[i].start, addrvalue[i].end);
		}
	}

	pr_info("VMAs dumped for ARM GP register pointers = %lu\n",
		nvmas);
	return nvmas;
}

#ifdef CONFIG_FUTEX_DLA
static void free_dla_list(void)
{
	struct dla_node *dla_node;
	struct dla_node *dla_other_node;

	list_for_each_entry_safe(dla_node, dla_other_node,
			&futex_dla_list, node) {
		if (current->tgid == dla_node->task->tgid) {
			put_task_struct(dla_node->task);
			list_del(&dla_node->node);
			kfree(dla_node);
		}
	}
}

static unsigned long extend_futex_dla(struct mm_struct *mm,
		struct list_head *psvma_list)
{
	struct pt_regs *regs;
	unsigned long stackptr = 0;
	unsigned long nvmas = 0;
	struct vm_area_struct *vma;
	struct dla_node *dla_node;

	list_for_each_entry(dla_node, &futex_dla_list, node) {
		if ((dla_node->task->tgid == current->tgid) &&
				(dla_node->task != current)) {
			regs = task_pt_regs(dla_node->task);
			/* Yeah ARM Linux */
			stackptr = rounddown(regs->ARM_sp, PAGE_SIZE)
				- PAGE_SIZE;

			for (vma = mm->mmap; vma != NULL;
					vma = (vma->vm_next)) {
				if (((vma->vm_start <= stackptr) &&
						(vma->vm_end > stackptr))) {
					if (add_to_svma(stackptr, vma->vm_end,
						vma->vm_flags, vma->vm_file, 1,
						psvma_list))
						return ERROR_CONDITION;
					nvmas++;
					break;
				}
			}
			if (NULL == vma)
				return ERROR_CONDITION;
		}
	}

	return nvmas;
}
#endif

static unsigned long create_svma_list(struct mm_struct *mm,
		struct pt_regs *regs, struct list_head *psvma_list)
{
	struct vm_area_struct *vma;
	struct file *file = NULL;
#ifdef MINIMAL_CORE_LIB_DATA_BSS
	struct file *exe_file = NULL;
#endif
	vm_flags_t flags;
	unsigned long nvmas = 0;
	unsigned long temp_nvmas = 0;

#ifdef MINIMAL_CORE_DEBUG
	struct svma_struct *svma;
	unsigned long ino = 0;
	unsigned long long pgoff = 0;
	dev_t dev = 0;
	const char *name = NULL;
#endif
	unsigned long stackptr = 0;
#ifdef CONFIG_FUTEX_DLA
	struct dla_node *dla_node;
	int dump_dla = 0;
#endif
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	struct open_file_info *file_node;
	int dump_file_rlimit = 0;
#endif

	/* Yeah ARM Linux */
	stackptr = rounddown(regs->ARM_sp, PAGE_SIZE) - PAGE_SIZE;

	/* pr_alert("Creating svma list %s\n",__func__); */
	for (vma = mm->mmap; vma != NULL; vma = (vma->vm_next)) {
		file = vma->vm_file;
		flags = vma->vm_flags;
#ifdef MINIMAL_CORE_DEBUG
		ino = pgoff = dev = 0;
		name = NULL;
		if (file) {
			struct inode *inode =
				vma->vm_file->f_path.dentry->d_inode;
			dev = inode->i_sb->s_dev;
			ino = inode->i_ino;
			pgoff = ((loff_t)vma->vm_pgoff) << PAGE_SHIFT;
			name = d_path(&file->f_path, svma_name_buf, 4096);
			if (IS_ERR(name))
				name = NULL;
		} else {
			name = arch_vma_name(vma);
			if (!name) {
				if (!(vma->vm_mm)) {
					name = "[vdso]";
				} else if (vma->vm_start <= current->mm->brk &&
					vma->vm_end >=
					current->mm->start_brk) {
					name = "[heap]";
				}
			}
		}

		pr_alert("0x%08lx - 0x%08lx %c%c%c%c 0x%08llx"
				, vma->vm_start, vma->vm_end,
				(vma->vm_flags & VM_READ) ? 'r' : '-',
				(vma->vm_flags & VM_WRITE) ? 'w' : '-',
				(vma->vm_flags & VM_EXEC) ? 'x' : '-',
				(vma->vm_flags & VM_MAYSHARE) ? 's' : 'p',
				pgoff);
		pr_alert(" 0x%02x:0x%02x %lu %s\n", MAJOR(dev), MINOR(dev),
				ino, name ? name : " ");
#endif

		if (vma->vm_flags & VM_DONTDUMP) {
			/* Just create VMA entries for this don't dump
			   * A kzalloc is enough no need for
			   * kmem_cache_alloc as this is only used temporarily
			   */
			if (add_to_svma(vma->vm_start, vma->vm_end,
				flags, file, 0,
				psvma_list)) {
				return ERROR_CONDITION;
			}
			nvmas++;
#ifdef MINIMAL_CORE_COMPLETE_MAINSTACK
		} else if (((vma->vm_start <= mm->start_stack) &&
				(vma->vm_end > mm->start_stack))) {
			/*Complete VMA
			  */
			if (add_to_svma(vma->vm_start, vma->vm_end,
				flags, file, 1,
				psvma_list)) {
				return -1;
			}
			stackptr = 0;
			nvmas++;
#else
		} else if (((vma->vm_start <= stackptr) &&
				(vma->vm_end > stackptr))) {
			if (add_to_svma(stackptr, vma->vm_end, flags, file, 1,
					psvma_list)) {
				return ERROR_CONDITION;
			}
			/* In case the ARM SP register is pointing to a wrong
			 * address, for example :- stack overflow, then we
			 * need to dump the whole stack, this stackptr will
			 * act as a flag too
			 * if stackptr is 0, then it means, stack has been
			 * added to svma list already
			 */
			stackptr = 0;
			nvmas++;
#endif
#ifdef MINIMAL_CORE_LIB_DATA_BSS
		} else if ((file && ((flags & VM_EXEC) && (!(flags & VM_WRITE))
				&& (!(flags & VM_MAYSHARE))))) {
			/* Just create VMA entries for this don't dump
			These is .text section VMA - useful
			for NT_FILE processing */
			if (add_to_svma(vma->vm_start, vma->vm_end,
				flags, file, 0,
				psvma_list)) {
				return -1;
			}
			nvmas++;
			exe_file = file;
		} else if ((file && ((flags & VM_WRITE)
				&& (!(flags & VM_MAYSHARE))))) {
			/*Complete VMA
			  */
			/*  if there is a private mmap'ed area then
			* we need to check for the type of file and then dump
			* This is done by checking with the exe_file
			* Logic : We know that from a .so we always get
			* exec first
			*       If we already have a exec, then exe_file
			*	will be set exe_file, and if this VMA
			*	having write permission is mapping to the same
			*	file which is having exec permission then we
			*	 will dump it otherwise not
			*/
			if (exe_file == file) {
#ifdef MINIMAL_CORE_DEBUG
				pr_alert("Lib Data 0x%08lx-0x%08lx\n",
						vma->vm_start, vma->vm_end);
#endif
				if (add_to_svma(vma->vm_start, vma->vm_end,
						flags, file, 1,
						psvma_list)) {
					return -1;
				}
				nvmas++;
			} else {
#ifdef MINIMAL_CORE_DEBUG
				pr_alert(
					"NonLib Data 0x%08lx-0x%08lx\n",
					vma->vm_start, vma->vm_end);
#endif
				if (add_to_svma(vma->vm_start, vma->vm_end,
						flags, file, 0,
						psvma_list)) {
					return -1;
				}
				nvmas++;
			}
#endif
#ifdef MINCORE_DUMP_DATA_VMA
		} else if ((vma->vm_start <= mm->start_data) &&
				(vma->vm_end >= mm->end_data))	{
			/*pr_alert("Dumping(small) data\n");
			*This is a complete VMA
			*/
			if (add_to_svma(vma->vm_start, vma->vm_end,
					flags, file, 1,
					psvma_list)) {
				return -1;
			}
			nvmas++;
#endif
#ifdef MINCORE_DUMP_BRK
		} else if ((vma->vm_start <= mm->start_brk) &&
				(vma->vm_end >= mm->brk)) {
			/* This is usually a part of heap */
			/* pr_alert("Dumping(small) brk\n"); */
			if (add_to_svma(rounddown(mm->start_brk, PAGE_SIZE),
						roundup((mm->brk), PAGE_SIZE),
						flags, file, 1, psvma_list)) {
				return -1;
			}
			nvmas++;
#endif
#ifdef MINCORE_DUMP_ARG
		} else if ((vma->vm_start <= mm->arg_start) &&
				(vma->vm_end >= mm->arg_end)) {
			/* part of stack (at the other end of stack) */
			/* pr_alert("Dumping(small) arg\n"); */
			if (add_to_svma(rounddown(mm->arg_start, PAGE_SIZE),
					roundup((mm->arg_end), PAGE_SIZE),
						flags, file, 1, psvma_list)) {
				return -1;
			}
			nvmas++;
#endif
#ifdef MINCORE_DUMP_ENV
		} else if ((vma->vm_start <= mm->env_start) &&
				(vma->vm_end >= mm->env_end)) {
			/* part of stack (at the other end of stack) */
			/* pr_alert("Dumping(small) env\n"); */
			if (add_to_svma(rounddown(mm->env_start, PAGE_SIZE),
					roundup((mm->env_end), PAGE_SIZE),
						flags, file, 1, psvma_list)) {
				return -1;
			}
			nvmas++;
#endif
		} else if (file && ((!(flags & VM_WRITE)) ||
					(flags & VM_MAYSHARE)))	{
			/* Just create VMA entries for this don't dump
			   These could be .text or .rodata section VMAs */
			if (add_to_svma(vma->vm_start, vma->vm_end,
					flags, file, 0,
					psvma_list)) {
				return ERROR_CONDITION;
			}
			nvmas++;
		}
	}

	/* Check whether current thread's stack has been dumped or not
	 * refer to explanation in stackptr above while adding to svma
	 */
	if (stackptr) {
		/* Complete VMA */
		vma = find_vma(mm, mm->start_stack);
		if (NULL == vma) {
			pr_alert("mincore: ERROR : No VMA for stack\n");
			return ERROR_CONDITION;
		}
		if (add_to_svma(vma->vm_start, vma->vm_end,
				vma->vm_flags, vma->vm_file, 1, psvma_list)) {
			return ERROR_CONDITION;
		}
		nvmas++;
	}

	vma = get_gate_vma(mm);
	/* Dump the gate vma */
	if (vma) {
		if (add_to_svma(vma->vm_start, vma->vm_end, vma->vm_flags,
					vma->vm_file, 1, psvma_list)) {
			return ERROR_CONDITION;
		}
		nvmas++;
	}

	/* The order is important, first other thread stacks
	 * WHEN ADDING ENCYRPTION, call extend_svma_list () ABOVE THIS
	 * and then and only then, we have to include heap pointed
	 * by ARM_gp regs of current thread
	 */
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	list_for_each_entry(file_node, &file_rlimit_list, node) {
		if (file_node->tsk->tgid == current->tgid) {
			dump_file_rlimit = 1;
			break;
		}
	}
	/* this is a futex deadlocked process,
	 * add other deadlocked thread stack
	 * vmas
	 */
	if (dump_file_rlimit) {
		temp_nvmas = extend_file_rlimit(mm, psvma_list);

		if (temp_nvmas == ERROR_CONDITION)
			return ERROR_CONDITION;
		nvmas += temp_nvmas;
		goto heap_add;
	}
#endif

#ifdef CONFIG_FUTEX_DLA
	/* check whether this task is included in deadlock list or not
	 * if included, then add deadlocked task's stack to dump
	 */
	list_for_each_entry(dla_node, &futex_dla_list, node) {
		if (dla_node->task->tgid == current->tgid) {
			dump_dla = 1;
			break;
		}
	}
	/* this is a futex deadlocked process,
	 * add other deadlocked thread stack
	 * vmas
	 */
	if (dump_dla) {
		temp_nvmas = extend_futex_dla(mm, psvma_list);

		if (temp_nvmas ==  ERROR_CONDITION)
			return  ERROR_CONDITION;
		nvmas += temp_nvmas;
	}
#endif
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
heap_add:
#endif
	/* Heap contents will be dumped only if encryption is enabled*/
	temp_nvmas = extend_svma_list(regs, mm, psvma_list);
	if (temp_nvmas == ERROR_CONDITION) {
		/* Handling svma node allocation failure case */
		return ERROR_CONDITION;
	}
	nvmas += temp_nvmas;

#ifdef MINIMAL_CORE_DEBUG
	pr_alert("sVMA list is as follows\n");

	list_for_each_entry(svma, psvma_list, node)	{
		pr_alert("%s 0x%08lx - 0x%08lx\n",
				svma->dumped ? "Dumped" :
				"Not Dumped", svma->vm_start, svma->vm_end);
	}
	pr_alert("Creating svma list completed total svmas = %ld\n",
			nvmas);
#endif

	return nvmas;
}

static void free_svma_list(struct list_head *psvma_list)
{
	struct svma_struct *svma;
	struct svma_struct *tmp_svma;
	list_for_each_entry_safe(svma, tmp_svma, psvma_list, node) {
		list_del(&svma->node);
		kfree(svma);
	}
}

static int minimal_dump_page(size_t *size, unsigned long addr,
		struct minimal_coredump_params *cprm)
{
	struct page *page;
	int stop = 0;

	page = get_dump_page(addr);
	if (page) {
		void *kaddr = kmap(page);
#ifdef MINIMAL_CORE_DEBUG_DETAIL
		pr_alert("Dumping page 0x%08lx\n", addr);
#endif
		stop = minimal_dump_write(cprm, kaddr, PAGE_SIZE);
		kunmap(page);
		page_cache_release(page);
	} else {
#ifdef MINIMAL_CORE_DEBUG_DETAIL
		pr_alert(
			"Not getting contents page 0x%08lx (lseek)\n", addr);
#endif
		stop = minimal_dump_seek(cprm, PAGE_SIZE);
	}

	return stop;

}

/*
   * Small core dumper
   *
   * This is a two-pass process; first we find the offsets of the bits,
   * and then they are actually written out.  In minimal corefile we need not
   * care about core limit as we avoid heap (except pages pointed by ARM GP),
   * and compress it too. The ulimit -c is applicable for full coredump file
   * only.
   */
static int elf_minimal_core_dump(struct minimal_coredump_params *cprm)
{
	int has_dumped = 0;
	mm_segment_t fs;
	unsigned int segs;
	size_t size = 0;
	struct elfhdr *elf = NULL;
	loff_t offset = 0, dataoff, foffset = 0;
	struct elf_note_info info;
	struct elf_phdr *phdr4note = NULL;
	struct elf_shdr *shdr4extnum = NULL;
	Elf_Half e_phnum = 0;
	struct svma_struct *svma;
	size_t sz = 0;
	LIST_HEAD(svma_list);
	int last_vma = 0;
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	struct open_file_info *file_node;
#endif

	cprm->minimal_core_state = MINIMAL_CORE_INIT_STATE;

	/* alloc memory for large data structures: too large to be on stack */
	elf = kmalloc(sizeof(*elf), GFP_KERNEL);
	if (!elf)
		goto out;

	/*
	* The number of segs are recored into ELF header as 16bit value
	* Please check DEFAULT_MAX_MAP_COUNT definition when you modify here
	*/
	/* pr_alert("Creating svma list\n"); */
	segs = create_svma_list(current->mm, cprm->regs, &svma_list);
	if ((unsigned int)-1 == segs) {
		/* Handling failure case */
		goto free_svma_out;
	}

	/* for notes section */
	segs++;
	/* minimal coredump will NEVER have segs more than 16K (PN_XNUM case),
	   * that is not what it is made for */

	e_phnum = (unsigned short)segs;
	/*
	* Collect all the non-memory information about the process for the
	* notes.  This also sets up the file header
	*/
	if (!minmal_core_fill_note(elf, e_phnum, &info, cprm->siginfo,
				cprm->regs))
		goto cleanup;

#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	list_for_each_entry(file_node, &file_rlimit_list, node) {
		if (file_node->overflow_tsk->tgid == current->tgid) {
			if (fill_open_files_info_note())
				goto cleanup;
			break;
		}
	}
#endif

	has_dumped = 1;
	current->flags |= PF_DUMPCORE;

	fs = get_fs();
	set_fs(KERNEL_DS);

	offset += sizeof(*elf); /* Elf header */
	offset += segs * sizeof(struct elf_phdr); /* Program headers */
	foffset = offset;


	/* Write notes phdr entry */
#ifdef CORE_DUMP_USE_REGSET
	sz = find_note_size(&info);
#else
	/* The below case is not necessary for us,
	   * as we have CORE_DUMP_USE_REGSET defined */
	for (i = 0; i < info.numnote; i++)
		sz += notesize(info.notes + i);

	sz += find_note_size(&info);
#endif


	phdr4note = kmalloc(sizeof(*phdr4note), GFP_KERNEL);
	if (!phdr4note)
		goto end_minimal_core;
	minimal_core_fill_phdr(phdr4note, (int)sz, offset);
	offset += sz;

	dataoff = offset = roundup(offset, ELF_EXEC_PAGESIZE);

	list_for_each_entry(svma, &svma_list, node) {
		if (svma->dumped)
			offset += (svma->vm_end - svma->vm_start);
	}

	offset = dataoff;

	size += sizeof(*elf);
	if (minimal_dump_write(cprm, elf, sizeof(*elf)))
		goto end_minimal_core;

	size += sizeof(*phdr4note);
	if (minimal_dump_write(cprm, phdr4note, sizeof(*phdr4note)))
		goto end_minimal_core;

	/* Write program headers for segments dump */
	list_for_each_entry(svma, &svma_list, node)	{
		struct elf_phdr phdr;

		if ((svma->dumped) || (svma->vm_file)) {
			phdr.p_type = PT_LOAD;
			phdr.p_offset = (unsigned int)offset;
			phdr.p_paddr = 0;
			phdr.p_filesz = (svma->dumped) ?
					(svma->vm_end - svma->vm_start) : 0;
			phdr.p_vaddr = svma->vm_start;
			phdr.p_memsz = svma->vm_end - svma->vm_start;
			offset += phdr.p_filesz;
			phdr.p_flags = svma->vm_flags & VM_READ ? PF_R : 0;
			if (svma->vm_flags & VM_WRITE)
				phdr.p_flags |= PF_W;
			if (svma->vm_flags & VM_EXEC)
				phdr.p_flags |= PF_X;
			phdr.p_align = ELF_EXEC_PAGESIZE;
			size += sizeof(phdr);
			if (minimal_dump_write(cprm, &phdr, sizeof(phdr)))
				goto end_minimal_core;
		}
	}

	if (write_note_info(&info, cprm, &foffset))
		goto end_minimal_core;

	if (minimal_dump_seek(cprm, dataoff - foffset))
		goto end_minimal_core;

	list_for_each_entry(svma, &svma_list, node)	{
		unsigned long addr;
		unsigned long end;

		end = svma->vm_end;
		if (svma->dumped)
			addr = svma->vm_start;
		else
			addr = end;

		if (list_is_last(&svma->node, &svma_list)) {
			last_vma = 1;
#ifdef MINIMAL_CORE_DEBUG
			pr_alert("last_vma detected\n");
#endif
			end -= PAGE_SIZE;
		}

#ifdef MINIMAL_CORE_DEBUG_DETAIL
		pr_alert("Dumping pages 0x%08lx-0x%08lx\n", addr,
				(addr+end));
#endif
		for (; addr < end; addr += PAGE_SIZE) {
			if (minimal_dump_page(&size, addr, cprm))
				goto end_minimal_core;
		}

		/* No need to handle error as it is a
		   * last_vma and last page anyways it is a
		   * FALLTHROUGH
		   */
		if (last_vma) {
#ifdef MINIMAL_CORE_DEBUG_DETAIL
			pr_alert(
				"Dumping last_vma last page 0x%08lx\n", addr);
#endif
			cprm->minimal_core_state = MINIMAL_CORE_FINI_STATE;
			minimal_dump_page(&size, addr, cprm);
		}
	}

end_minimal_core:
	set_fs(fs);

cleanup:
	minimal_core_free_note(&info);
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	if (file_rlimit.data) {
		vfree(file_rlimit.data);
		file_rlimit.data = NULL;
	}
#endif
	kfree(shdr4extnum);
	kfree(phdr4note);

free_svma_out:
	kfree(elf);
	free_svma_list(&svma_list);
out:

	return has_dumped;
}

static int mcore_build_name_list(void *arg, const char *name, int namelen,
				loff_t offset, u64 ino, unsigned int d_type)
{
	struct list_head *names = arg;
	struct mcore_name_list *entry;
	const char *tmp = NULL;

	/* Let us add only file names matching the pattern
	 * *.<MINCORE_NAME> to the list */
	/* filename should be atleast MINCORE_NAME length
	* if it has .mcore at the end of the filename then
	* it is a mincore file
	*/
	if (namelen <= (int)strlen(MINCORE_NAME))
		return 0;
	tmp = name + ((size_t)namelen - (size_t)strlen(MINCORE_NAME));
	/* strcmp will do, still using strncmp for guidelines */
	if (strncmp(MINCORE_NAME, tmp, strlen(MINCORE_NAME)))
		return 0;

	entry = kmalloc(sizeof(struct mcore_name_list), GFP_KERNEL);
	if (NULL == entry) {
		pr_alert("Can't allocate memory for mcore_name_list");
		return -ENOMEM;
	}

	entry->name = kmalloc((size_t)namelen+(size_t)1, GFP_KERNEL);
	if (NULL == entry->name) {
		kfree(entry);
		pr_alert("Can't allocate memory for filename\n");
		return -ENOMEM;
	}

	memcpy(entry->name, name, (size_t)namelen);
	entry->name[namelen] = '\0';

	list_add(&entry->list, names);

	return 0;
}

static void free_names(struct list_head *names)
{
	struct mcore_name_list *entry;

	while (!list_empty(names)) {
		entry = list_entry(names->next, struct mcore_name_list, list);
		list_del(&entry->list);
		kfree(entry->name);
		kfree(entry);
	}
}

static int cmp_mincore_list(void *priv, struct list_head *a,
		struct list_head *b)
{
	struct mcore_name_list *entry = NULL;
	unsigned long num_a, num_b;
	char num_str[ULONG_LEN + 1];
	char *tmp = NULL;

	entry = list_entry(a, struct mcore_name_list, list);

	tmp = strchr(entry->name, '.');
	/* Absolutely no need to handle this
	* filename not in expected format, let it be */
	if (NULL == tmp)
		return 0;

	if (((tmp - entry->name) > 0) && ((tmp - entry->name) <= ULONG_LEN))
		strncpy(num_str, entry->name, (size_t)(tmp - entry->name));
	else
		return 0;

	num_str[(tmp - entry->name)] = '\0';
	if (kstrtoul(num_str, 10, &num_a))
		return 0;

	entry = list_entry(b, struct mcore_name_list, list);

	tmp = strchr(entry->name, '.');
	/* Absolutely no need to handle this
	* filename not in expected format, let it be */
	if (NULL == tmp)
		return 0;

	if (((tmp - entry->name) > 0) && ((tmp - entry->name) <= ULONG_LEN))
		strncpy(num_str, entry->name, (size_t)(tmp - entry->name));
	else
		return 0;

	num_str[(tmp - entry->name)] = '\0';
	if (kstrtoul(num_str, 10, &num_b))
		return 0;

	if (num_a < num_b)
		return -1;
	else if (num_a > num_b)
		return 1;
	else
		return 0;
}

/*
* FILE NAMES / LINK NAMES / DIRECTORY NAMES STARTING WITH <MINCORE_NAME> AND
* HAVING A "." IN  THE NAME WILL BE CONSIDERED AS MINIMAL COREFILE
* OTHERWISE THERE ARE TOO MANY SCENARIOS NOT WORTH HANDLING IN KERNEL SPACE
* FOR EXAMPLE (WORTHLESS SCENARIOS TO GET UNIT TESTING 100% CORRECTNESS)
*   THERE IS A FILE STARTING WITH <MINCORE_NAME>, BUT THIS IS NOT A COREFILE
*   FILE NAME STARTS WITH <MINCORE_NAME>, BUT DOES NOT HAVE AN INDEX AT THE END
*
*/
static unsigned int find_max(struct list_head *names, unsigned long *max)
{
	struct mcore_name_list *entry;
	struct list_head *node;
	struct list_head *last = NULL;
	unsigned int nfiles = 0;

	if (list_empty(names)) {
		*max = 0;
	} else {
		char num_str[ULONG_LEN + 1];
		char *tmp = NULL;

		/* Let us sort the nodes */
		list_sort(NULL, names, cmp_mincore_list);
		last = names->prev;
		entry = list_entry(last, struct mcore_name_list, list);

		tmp = strchr(entry->name, '.');
		/* Absolutely no need to handle this
		* filename not in expected format, let it be */
		if (NULL == tmp)
			return 0;

		if (((tmp - entry->name) > 0) &&
				((tmp - entry->name) <= ULONG_LEN))
			strncpy(num_str, entry->name,
					(size_t)(tmp - entry->name));
		else
			return 0;

		num_str[(tmp - entry->name)] = '\0';

		if (kstrtoul(num_str, 10, max))
			return 0;

		list_for_each(node, names) {
			nfiles++;
		}
	}

	return nfiles;
}

#ifndef CONFIG_MINIMAL_CORE_RETAIN
static void minimal_core_delete(struct dentry *parent, char *minimal_core_name)
{
	struct dentry *dentry;

	mutex_lock(&parent->d_inode->i_mutex);
	dentry = lookup_one_len(minimal_core_name, parent,
			(int)strlen(minimal_core_name));

	if (IS_ERR(dentry)) {
		pr_alert("WARN : can't delete (no dentry) %s\n",
			minimal_core_name);
		goto out2;
	}

	if (dentry->d_inode == NULL) {
		pr_alert("WARN : can't delete (no inode)%s\n",
			minimal_core_name);
		goto out;
	}

	/* We will handle the case of Regular file only
	 * minimal core file is A REGULAR FILE.
	 */
	if ((dentry->d_inode->i_mode & S_IFMT) == S_IFREG)
		vfs_unlink(parent->d_inode, dentry);

out:
	dput(dentry);
out2:
	mutex_unlock(&parent->d_inode->i_mutex);
	return;
}

/* ndelete --> number of files to be deleted
* Algo : seperate the files names of minimal core in a seperate list
*		  Sort the list
*		  from the sorted list, delete the first ndelete files
*/
static void minimal_core_delete_xcess(struct dentry *parent,
		unsigned int ndelete, struct list_head *names)
{
	struct mcore_name_list *entry;
	struct list_head *node;

	list_for_each(node, names) {
		entry = list_entry(node, struct mcore_name_list, list);
		pr_alert("minimal core: Deleting %s\n", entry->name);
		minimal_core_delete(parent, entry->name);
		ndelete--;

		if (!ndelete)
			break;
	}

	return;
}
#endif

/* Returning error as integer makes things more simpler */
static int open_mincore_dir(struct file **dir_file)
{
	char *onelup_dir_name = NULL;
	const char *file_dir_name = NULL;
	char *tmp = NULL;
	int error = 0;
	int ret = 0;
	struct file *parent_dir = NULL;
	struct dentry *subdir = NULL;
	struct dentry *parent = NULL;

	*dir_file = filp_open(MINIMAL_CORE_LOCATION,
				O_RDONLY | O_DIRECTORY, 0);
	if (!IS_ERR(*dir_file))
		goto out;

	/* open of /mtd_rwcommon/error_log failed */
	/* Try to create the directory */
	/* Note: atleast the first level dir should be available,
	 * we will only attempt mkdir, not "mkdir -p"
	 */
	onelup_dir_name = kmalloc(strlen(MINIMAL_CORE_LOCATION) + 1,
					GFP_KERNEL);
	if (NULL == onelup_dir_name) {
		error = -ENOMEM;
		goto out;
	}

	strncpy(onelup_dir_name, MINIMAL_CORE_LOCATION,
		(size_t)strlen(MINIMAL_CORE_LOCATION) + 1);
	tmp = strrchr(onelup_dir_name, '/');
	if (tmp) {
		*tmp = '\0';
		 tmp = strrchr(onelup_dir_name, '/');
	}
	if (NULL == tmp) {
		pr_alert("FATAL : mincore : dirname wrong format\n");
		error = -EIO;
		goto close_dir;
	}

	*tmp = '\0';
	tmp++;
	file_dir_name = tmp;

	parent_dir = filp_open(onelup_dir_name, O_RDONLY | O_DIRECTORY, 0);
	if (IS_ERR(parent_dir)) {
		pr_alert(
			"FATAL: minimal core: Unable to open %s directory\n",
			onelup_dir_name);
		parent_dir = NULL;
		error = -EIO;
		goto close_dir;
	}

	parent = parent_dir->f_dentry;

	mutex_lock(&parent->d_inode->i_mutex);

	subdir = lookup_one_len(file_dir_name, parent,
			(int)strlen(file_dir_name));
	if (IS_ERR(subdir)) {
		pr_alert(
			"FATAL: minimal core: Can't lookup %s err-%ld\n",
			file_dir_name, PTR_ERR(subdir));
		error = -EIO;
		goto mkdir_error;
	}

	/* The following condition will almost always be true */
	if (likely((!subdir->d_inode))) {
		ret = vfs_mkdir(parent->d_inode, subdir,
			(S_IRWXU | S_IRWXG | S_IRWXO));
		if (ret < 0) {
			pr_alert(
				"FATAL : can't create %s directory %d\n",
				MINIMAL_CORE_LOCATION, ret);
			error = -EIO;
			/* FALLTHROUGH goto mkdir_error; */
		} else {
			/* If mkdir is successful,
			 then we will open the directory */
			*dir_file = filp_open(MINIMAL_CORE_LOCATION,
					O_RDONLY | O_DIRECTORY, 0);
			if (IS_ERR(dir_file))
				error = -EIO;
			/* FALLTHROUGH goto mkdir_error; */
		}
	} else {
		pr_emerg("FATAL : mincore directory cannot be accessed\n");
		error = -EACCES;
	}

mkdir_error:
	if (!IS_ERR(subdir))
		dput(subdir);
	mutex_unlock(&parent->d_inode->i_mutex);

close_dir:
	if (parent_dir)
		filp_close(parent_dir, NULL);
	kfree(onelup_dir_name);

out:
	return error;
}

void do_minimal_core(struct coredump_params *cprm)
{
	/* now it is /mtd_rwcommon/error_log/
	*		<index>.<tname>.<comm>.<pid>.<tgid>.mcore
	*  --> where <tname> will be current->comm
	*  --> where <comm> will be current->group_leader->comm
	*  --> where <index> will be from 0 to 4294967295 (unsigned long max)
	* NOT WORTH HANDLING SPECIAL SCENARIOS (w.r.to retain)
	*  IN WRAPAROUND SCENARIO -  FLASH ITSELF CANNOT BE
	* USED TILL WRAPAROUND
	* WRAPAROUND IWTH UINT_MAX means COREFILE CREATED 4G TIMES
	* THERE WON'T BE SUCH A CASE
	*/
	char small_corename[CORENAME_MAX_SIZE + 1];
	struct file *score_file = NULL;
	unsigned int flags = 0;
	struct file *dir_file = NULL;
	int retval = 0;
	LIST_HEAD(names);
	struct minimal_coredump_params mcprm;
	unsigned long max = 0;
	unsigned int nfiles = 0;
	const struct cred *old_cred = NULL;
	struct cred *cred;
#ifdef CONFIG_SECURITY_SMACK
	struct task_smack *tsp;
	struct smack_known *skp;
#endif

	cred = prepare_creds();
	if (!cred) {
		pr_emerg("WARN : mincore can't change credentials\n");
		goto skip_cred_change;
	}

	cred->fsuid = GLOBAL_ROOT_UID;
	cred->fsgid = GLOBAL_ROOT_GID;
	cred->euid = GLOBAL_ROOT_UID;
	cred->egid = GLOBAL_ROOT_GID;
#ifdef CONFIG_SECURITY_SMACK
	cap_raise(cred->cap_effective, CAP_MAC_ADMIN);

	skp = smk_import_entry(MINCORE_SMACK_LABEL,
			strlen(MINCORE_SMACK_LABEL));
	if (skp) {
		tsp = cred->security;
		tsp->smk_task = skp;
	} else
		pr_emerg("Mincore: Can't change smack label to %s",
				MINCORE_SMACK_LABEL);
#endif
	old_cred = override_creds(cred);
skip_cred_change:

	memset(&mcprm, 0, sizeof(mcprm));
	mcprm.siginfo = cprm->siginfo;
	mcprm.regs = cprm->regs;

	flags = O_CREAT | O_RDWR | O_NOFOLLOW | O_LARGEFILE | O_EXCL;

	/* NO NEED TO HANDLE FILE DELETION FAILURE CASE */
	/* WE WILL ONLY PRINT A WARNING, CAPTURING MINIMAL COREFILE NEEDS TO BE
	 * DONE EVEN IF DELETING OLD & EXCESS COREFILE FAILS
	 * In case if we fail to find max then file index will be 0
	 */
	retval = open_mincore_dir(&dir_file);
	if (retval < 0) {
		pr_alert(
			"FATAL: minimal core: Unable to open %s directory\n",
			MINIMAL_CORE_LOCATION);
		return;
	}

	mutex_lock(&mincore_file_mutex);
	retval = vfs_readdir(dir_file, mcore_build_name_list, &names);
	if (retval && (retval != -ENOENT)) {
		pr_alert(
			"FATAL : mincore: cant to browse %s directory %d\n",
			MINIMAL_CORE_LOCATION, retval);
		mutex_unlock(&mincore_file_mutex);
		goto dir_close;
	} else if (retval == -ENOENT) {
		pr_alert("INFO: %s directory empty\n",
			MINIMAL_CORE_LOCATION);
	}

	/* Now we have the list of files & dirs
	 * let us check for minimal core files
	 */
	nfiles = find_max(&names, &max);
	if (nfiles >= MINIMAL_CORE_TORETAIN) {
#ifdef CONFIG_MINIMAL_CORE_RETAIN
		pr_alert("INFO : mincore already %lu files are created\n",
			MINIMAL_CORE_TORETAIN);
		mutex_unlock(&mincore_file_mutex);
		goto dir_close;
#else
		minimal_core_delete_xcess(dir_file->f_dentry,
			(nfiles - MINIMAL_CORE_TORETAIN) + 1, &names);
#endif
	}

	/* We will not create files in b/w the holes, as we infer the corefile
	 * sequence from the name of the file
	 */

	/* This is it (max+1) will be the index, If we fail to create this file
	* then do not create the minimal corefile, otherwise if you have to try
	* (max+2), (max+3)... then it will finaly overlap and come to existing
	* file, and we will have to overwrite that. --> same behaviour
	*                                          as normal coredump
	*
	* Even if overlap scenario is handled, then what if there are gaps ?
	* min to max will always have contigous files. If we handle overlap
	* then we need to handle "contigous" numbered files. Otherwise
	* min  to max can have some holes and we will be deleting even if we
	* do not have > MINIMAL_CORE_TORETAIN
	* So just check whether file with (max + 1) can be created or not
	* If it can't be created then throw the error and return
	*/

	snprintf(small_corename, sizeof(small_corename),
			"%s%lu.%s.%s.%d.%d", MINIMAL_CORE_LOCATION,
			(max + 1), current->comm, current->group_leader->comm,
			current->pid, current->tgid);

	/* add target_version if present copied from coredump.c
	 * this is like 1+1 can't have any other logic
	 */
	if (strncmp(target_version, "", sizeof(""))) {
		strncat(small_corename, ".", sizeof(small_corename)
			- strlen(small_corename) - 1);
		strncat(small_corename, target_version, sizeof(small_corename)
			- strlen(small_corename) - 1);
		/* remove the extra '\n' character in small_corename */
		if (small_corename[strlen(small_corename) - 1] == '\n')
			small_corename[strlen(small_corename) - 1] = '\0';
	}
	strncat(small_corename, MINCORE_NAME, sizeof(small_corename)
			- strlen(small_corename) - 1);

	score_file = filp_open(small_corename, (int)flags, 0600);
	if (IS_ERR(score_file)) {
		score_file = NULL;
		pr_alert("ERROR : Could not create %s\n",
			small_corename);
		mutex_unlock(&mincore_file_mutex);
		goto dir_close;
	}

	mcprm.file = score_file;

	pr_info("***** Create small coredump file %s ******\n",
			small_corename);

#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	mutex_lock(&file_rlimit_mutex);
#endif
#ifdef CONFIG_FUTEX_DLA
	mutex_lock(&dla_mutex);
#endif
	retval = elf_minimal_core_dump(&mcprm);
#ifdef CONFIG_FUTEX_DLA
	free_dla_list();
	mutex_unlock(&dla_mutex);
#endif
#ifdef CONFIG_MINCORE_RLIMIT_NOFILE
	free_file_list();
	mutex_unlock(&file_rlimit_mutex);
#endif
	mutex_unlock(&mincore_file_mutex);

	pr_info("small Coredump: finished dumping small core %s\n",
			small_corename);

dir_close:
	filp_close(dir_file, NULL);
	free_names(&names);

	if (score_file)
		filp_close(score_file, NULL);

	if (cred) {
		revert_creds(old_cred);
		put_cred(cred);
	}

	return;
}

#ifdef CONFIG_MINCORE_RLIMIT_NOFILE

static unsigned long extend_file_rlimit(struct mm_struct *mm,
					struct list_head *psvma_list)
{
	struct pt_regs *regs;
	unsigned long stackptr = 0;
	unsigned long nvmas = 0;
	struct vm_area_struct *vma;
	struct open_file_info *file_node;

	list_for_each_entry(file_node, &file_rlimit_list, node) {
		if ((file_node->tsk->tgid == current->tgid) &&
				(file_node->tsk != current)) {
			regs = task_pt_regs(file_node->tsk);
			/* Yeah ARM Linux */
			stackptr = rounddown(regs->ARM_sp, PAGE_SIZE)
				- PAGE_SIZE;

			for (vma = mm->mmap; vma != NULL;
					vma = (vma->vm_next)) {
				if (((vma->vm_start <= stackptr) &&
						(vma->vm_end > stackptr))) {
					if (add_to_svma(stackptr, vma->vm_end,
						vma->vm_flags, vma->vm_file, 1,
								psvma_list))
						return ERROR_CONDITION;
					nvmas++;
					break;
				}
			}
			if (NULL == vma)
				return ERROR_CONDITION;
		}
	}

	return nvmas;
}

static void fill_note(struct memelfnote *note, const char *name, int type,
		unsigned int sz, void *data)
{
	note->name = name;
	note->type = type;
	note->datasz = sz;
	note->data = data;
	return;
}

/*
 * Format of NT_SABSP_FILE_INFO note:
 *
 * long count     -- how many files are opened
 * array of [COUNT] elements of
 *   fd
 *   f_mode
 *   f_pos
 *   pid
 * followed by COUNT filenames in ASCII: "filename" NUL "owner_name" NUL...
 */
static int fill_open_files_info_note(void)
{
	struct open_file_info *file_open;
	unsigned count = 0, size;
	char *data;
	char *buf;
	unsigned *count_ptr = NULL;
	struct fix_data {
		int fd;
		fmode_t f_mode;
		loff_t f_pos;
		pid_t owner_pid;
	} __packed;
	struct fix_data *fix_data_ptr;
	int i = 0;
	int error = 0;

	/* *Estimated* file count and total data size needed */
	list_for_each_entry(file_open, &file_rlimit_list, node) {
		if (file_open->overflow_tsk->tgid == current->tgid)
			count++;
	}

	size = sizeof(unsigned int); /* count */
	/* followed by array of fix data */
	size += count * (sizeof(struct fix_data));
	/* followed by string of filename, owner thread name */
	size += count * (sizeof(current->comm) + PATH_MAX);
	size = round_up(size, PAGE_SIZE);
	buf = vmalloc(size);
	if (!buf) {
		error = -ENOMEM;
		goto out;
	}
	count_ptr = (unsigned int *)buf;
	*count_ptr = count;
	fix_data_ptr = (struct fix_data *) (buf + sizeof(unsigned int));
	data = buf + sizeof(unsigned int) + (sizeof(struct fix_data) * count);

	list_for_each_entry(file_open, &file_rlimit_list, node) {
		if (file_open->overflow_tsk->tgid != current->tgid)
			continue;

		fix_data_ptr[i].fd = file_open->fd;
		fix_data_ptr[i].f_mode = file_open->f_mode;
		fix_data_ptr[i].owner_pid = file_open->owner_pid;
		fix_data_ptr[i].f_pos = file_open->f_pos;
		strcpy(data, file_open->name);
		data = data + strlen(file_open->name) + 1;
		strcpy(data, file_open->owner_name);
		data = data + strlen(file_open->owner_name) + 1;
		i++;
	}

	size = (unsigned int)(data - buf);
	memset(&file_rlimit, 0, sizeof(file_rlimit));
	fill_note(&file_rlimit, "CORE", NT_SABSP_FILE_INFO, size, buf);

out:
	return error;
}

/* This function will iterate through file_rlimit_list and
 * send SIGABRT, This should be called with file_rlimit_mutex held
 */
void file_rlimit_dump(void)
{
	/* The PF_EXITING is only an alarmist check
	 * It is not possible for RLIMIT_NOFILE scenario
	 * as current WILL NOT BE in EXITING state and
	 * also overflowing on file opening
	 * But as each function as a unit has to be properly
	 * handling all scenarios, for extensibility purposes.
	 */
	if (!(current->flags & PF_EXITING) &&
			(current->sighand)) {
		force_sig(SIGABRT, current);
	} else {
		free_file_list();
	}

}
#endif
