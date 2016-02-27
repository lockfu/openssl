/*
 * Written by Rob Stradling (rob@comodo.com), Stephen Henson (steve@openssl.org)
 * and Adam Eijdenberg (adam.eijdenberg@gmail.com) for the OpenSSL project 2016.
 */
/* ====================================================================
 * Copyright (c) 2014 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

#ifdef OPENSSL_NO_CT
# error "CT disabled"
#endif

#include <openssl/ct.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/tls1.h>
#include <openssl/x509.h>

#include "ct_locl.h"

SCT *SCT_new(void)
{
    SCT *sct = OPENSSL_zalloc(sizeof(SCT));

    if (sct == NULL) {
        CTerr(CT_F_SCT_NEW, ERR_R_MALLOC_FAILURE);
        return NULL;
    }

    sct->entry_type = CT_LOG_ENTRY_TYPE_NOT_SET;
    sct->version = SCT_VERSION_NOT_SET;
    return sct;
}

void SCT_free(SCT *sct)
{
    if (sct == NULL)
        return;

    OPENSSL_free(sct->log_id);
    OPENSSL_free(sct->ext);
    OPENSSL_free(sct->sig);
    OPENSSL_free(sct->sct);
    OPENSSL_free(sct);
}

int SCT_set_version(SCT *sct, sct_version_t version)
{
    if (version != SCT_VERSION_V1) {
        CTerr(CT_F_SCT_SET_VERSION, CT_R_UNSUPPORTED_VERSION);
        return 0;
    }
    sct->version = version;
    return 1;
}

int SCT_set_log_entry_type(SCT *sct, ct_log_entry_type_t entry_type)
{
    switch (entry_type) {
    case CT_LOG_ENTRY_TYPE_X509:
    case CT_LOG_ENTRY_TYPE_PRECERT:
        sct->entry_type = entry_type;
        return 1;
    default:
        CTerr(CT_F_SCT_SET_LOG_ENTRY_TYPE, CT_R_UNSUPPORTED_ENTRY_TYPE);
        return 0;
    }
}

int SCT_set0_log_id(SCT *sct, unsigned char *log_id, size_t log_id_len)
{
    if (sct->version == SCT_VERSION_V1 && log_id_len != CT_V1_HASHLEN) {
        CTerr(CT_F_SCT_SET0_LOG_ID, CT_R_INVALID_LOG_ID_LENGTH);
        return 0;
    }

    OPENSSL_free(sct->log_id);
    sct->log_id = log_id;
    sct->log_id_len = log_id_len;
    return 1;
}

int SCT_set1_log_id(SCT *sct, const unsigned char *log_id, size_t log_id_len)
{
    if (sct->version == SCT_VERSION_V1 && log_id_len != CT_V1_HASHLEN) {
        CTerr(CT_F_SCT_SET1_LOG_ID, CT_R_INVALID_LOG_ID_LENGTH);
        return 0;
    }

    OPENSSL_free(sct->log_id);
    sct->log_id = NULL;
    sct->log_id_len = 0;

    if (log_id != NULL && log_id_len > 0) {
        sct->log_id = OPENSSL_memdup(log_id, log_id_len);
        if (sct->log_id == NULL) {
            CTerr(CT_F_SCT_SET1_LOG_ID, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        sct->log_id_len = log_id_len;
    }
    return 1;
}


void SCT_set_timestamp(SCT *sct, uint64_t timestamp)
{
    sct->timestamp = timestamp;
}

int SCT_set_signature_nid(SCT *sct, int nid)
{
  switch (nid) {
    case NID_sha256WithRSAEncryption:
        sct->hash_alg = TLSEXT_hash_sha256;
        sct->sig_alg = TLSEXT_signature_rsa;
        return 1;
    case NID_ecdsa_with_SHA256:
        sct->hash_alg = TLSEXT_hash_sha256;
        sct->sig_alg = TLSEXT_signature_ecdsa;
        return 1;
    default:
        CTerr(CT_F_SCT_SET_SIGNATURE_NID, CT_R_UNRECOGNIZED_SIGNATURE_NID);
        return 0;
    }
}

void SCT_set0_extensions(SCT *sct, unsigned char *ext, size_t ext_len)
{
    OPENSSL_free(sct->ext);
    sct->ext = ext;
    sct->ext_len = ext_len;
}

int SCT_set1_extensions(SCT *sct, const unsigned char *ext, size_t ext_len)
{
    OPENSSL_free(sct->ext);
    sct->ext = NULL;
    sct->ext_len = 0;

    if (ext != NULL && ext_len > 0) {
        sct->ext = OPENSSL_memdup(ext, ext_len);
        if (sct->ext == NULL) {
            CTerr(CT_F_SCT_SET1_EXTENSIONS, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        sct->ext_len = ext_len;
    }
    return 1;
}

void SCT_set0_signature(SCT *sct, unsigned char *sig, size_t sig_len)
{
    OPENSSL_free(sct->sig);
    sct->sig = sig;
    sct->sig_len = sig_len;
}

int SCT_set1_signature(SCT *sct, const unsigned char *sig, size_t sig_len)
{
    OPENSSL_free(sct->sig);
    sct->sig = NULL;
    sct->sig_len = 0;

    if (sig != NULL && sig_len > 0) {
        sct->sig = OPENSSL_memdup(sig, sig_len);
        if (sct->sig == NULL) {
            CTerr(CT_F_SCT_SET1_SIGNATURE, ERR_R_MALLOC_FAILURE);
            return 0;
        }
        sct->sig_len = sig_len;
    }
    return 1;
}

sct_version_t SCT_get_version(const SCT *sct)
{
    return sct->version;
}

ct_log_entry_type_t SCT_get_log_entry_type(const SCT *sct)
{
    return sct->entry_type;
}

size_t SCT_get0_log_id(const SCT *sct, unsigned char **log_id)
{
    *log_id = sct->log_id;
    return sct->log_id_len;
}

uint64_t SCT_get_timestamp(const SCT *sct)
{
    return sct->timestamp;
}

int SCT_get_signature_nid(const SCT *sct)
{
    if (sct->version == SCT_VERSION_V1) {
        if (sct->hash_alg == TLSEXT_hash_sha256) {
            switch (sct->sig_alg) {
            case TLSEXT_signature_ecdsa:
                return NID_ecdsa_with_SHA256;
            case TLSEXT_signature_rsa:
                return NID_sha256WithRSAEncryption;
            default:
                return NID_undef;
            }
        }
    }
    return NID_undef;
}

size_t SCT_get0_extensions(const SCT *sct, unsigned char **ext)
{
    *ext = sct->ext;
    return sct->ext_len;
}

size_t SCT_get0_signature(const SCT *sct, unsigned char **sig)
{
    *sig = sct->sig;
    return sct->sig_len;
}

int SCT_is_complete(const SCT *sct)
{
    switch (sct->version) {
    case SCT_VERSION_NOT_SET:
        return 0;
    case SCT_VERSION_V1:
        return sct->log_id != NULL && SCT_signature_is_complete(sct);
    default:
        return sct->sct != NULL; /* Just need cached encoding */
    }
}

int SCT_signature_is_complete(const SCT *sct)
{
    return SCT_get_signature_nid(sct) != NID_undef &&
        sct->sig != NULL && sct->sig_len > 0;
}

