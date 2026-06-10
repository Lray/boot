#ifndef H_IMAGE_H
#define H_IMAGE_H

#include <stdint.h>
#include <stdbool.h>

#include "bootutil/bootutil_macros.h"
#include "bootutil/fault_injection_hardening.h"

#ifndef COMPRESSIONFLAGS
#define COMPRESSIONFLAGS 0
#endif

#ifndef IMAGE_MAGIC
#define IMAGE_MAGIC 0x96f3b83dU
#endif

STRUCT_PACKED image_version
{
    uint8_t iv_major;
    uint8_t iv_minor;
    uint16_t iv_revision;
    uint32_t iv_build_num;
};

/** Image header.  All fields are in little endian byte order. */
STRUCT_PACKED image_header
{
    uint32_t ih_magic;
    uint32_t ih_load_addr;
    uint16_t ih_hdr_size;         /* Size of image header (bytes). */
    uint16_t ih_protect_tlv_size; /* Size of protected TLV area (bytes). */
    uint32_t ih_img_size;         /* Does not include header. */
    uint32_t ih_flags;            /* IMAGE_F_[...]. */
    struct image_version ih_ver;
    uint32_t _pad1;
};

/*
 * Image header flags.
 */
#define IMAGE_F_NON_BOOTABLE 0x00000010 /* Split image app. */

#define IMAGE_TLV_INFO_MAGIC 0x6907
#define IMAGE_TLV_PROT_INFO_MAGIC 0x6908

STRUCT_PACKED image_tlv_info
{
    uint16_t it_magic;
    uint16_t it_tlv_tot;
};

/* Image trailer TLV format. All fields in little endian. */
STRUCT_PACKED image_tlv
{
    uint16_t it_type; /* IMAGE_TLV_[...]. */
    uint16_t it_len;  /* Data length (not including TLV header). */
};

/*
 * Image trailer TLV types.
 */
#define IMAGE_TLV_KEYHASH 0x01     /* hash of the public key */
#define IMAGE_TLV_PUBKEY 0x02      /* public key */
#define IMAGE_TLV_SHA256 0x10      /* SHA256 of image hdr and body */
#define IMAGE_TLV_SHA384 0x11      /* SHA384 of image hdr and body */
#define IMAGE_TLV_SHA512 0x12      /* SHA512 of image hdr and body */
#define IMAGE_TLV_RSA2048_PSS 0x20 /* RSA2048 of hash output */
#define IMAGE_TLV_ECDSA224 0x21    /* ECDSA of hash output - unsupported */
#define IMAGE_TLV_ECDSA_SIG 0x22   /* ECDSA of hash output */
#define IMAGE_TLV_RSA3072_PSS 0x23 /* RSA3072 of hash output */
#define IMAGE_TLV_ED25519 0x24     /* Ed25519 of hash output */
#define IMAGE_TLV_ANY 0xffff       /* wildcard for TLV iteration */
#define IMAGE_HASH_LEN 32          /* Size of SHA256 TLV hash */
#ifndef IMAGE_HASH_SIZE
#define IMAGE_HASH_SIZE IMAGE_HASH_LEN
#endif

struct image_tlv_iter
{
    const struct image_header *hdr;
    const struct flash_area *fap;
    uint16_t type;
    bool prot;
    uint32_t prot_end;
    uint32_t tlv_off;
    uint32_t tlv_end;
};

struct boot_loader_state;

int bootutil_tlv_iter_begin(struct image_tlv_iter *it,
                            const struct image_header *hdr,
                            const struct flash_area *fap,
                            uint16_t type, bool prot);
int bootutil_tlv_iter_next(struct image_tlv_iter *it,
                           uint32_t *off, uint16_t *len, uint16_t *type);
int bootutil_tlv_iter_is_prot(struct image_tlv_iter *it, uint32_t off);
int bootutil_find_key(uint8_t *keyhash, uint8_t keyhash_len);

int bootutil_img_hash(struct boot_loader_state *state,
                      struct image_header *hdr, const struct flash_area *fap,
                      uint8_t *tmp_buf, uint32_t tmp_buf_sz,
                      uint8_t *hash_result, uint8_t *seed, int seed_len);

fih_ret bootutil_img_validate(struct boot_loader_state *state,
                              struct image_header *hdr,
                              const struct flash_area *fap,
                              uint8_t *tmp_buf, uint32_t tmp_buf_sz,
                              uint8_t *seed, int seed_len,
                              uint8_t *out_hash);

#endif /* H_IMAGE_H */
