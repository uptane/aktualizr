/*
 * Copyright (C) 2016 Mathias Tausig, FH Campus Wien
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v3. See the file LICENSE in the top level
 * directory for more details.
 */

#ifndef __TINY_ASN1_H__
#define __TINY_ASN1_H__

#ifdef __cplusplus
extern "C" {
#endif
	  
#include <stdint.h>

/* Define the header bytes of primitive ASN.1 types */

/** Header byte of the ASN.1 type INTEGER */
#define ASN1_TYPE_INTEGER 0x02
/** Header byte of the ASN.1 type BIT STRING */
#define ASN1_TYPE_BIT_STRING 0x03
/** Header byte of the ASN.1 type OCTET STRING */
#define ASN1_TYPE_OCTET_STRING 0x04
/** Header byte of the ASN.1 type NULL */
#define ASN1_TYPE_NULL 0x05
/** Header byte of the ASN.1 type OBJECT IDENTIFIER */
#define ASN1_TYPE_OBJECT_IDENTIFIER 0x06
/** Header byte of the ASN.1 type SEQUENCE */
#define ASN1_TYPE_SEQUENCE 0x30
/** Header byte of the ASN.1 type SET */
#define ASN1_TYPE_SET 0x31
/** Header byte of the ASN.1 type UTF8String */
#define ASN1_TYPE_UTF8_STRING 0x12
/** Header byte of the ASN.1 type PrintableString */
#define ASN1_TYPE_PRINTABLE_STRING 0x19
/** Header byte of the ASN.1 type T61String */
#define ASN1_TYPE_T61_STRING 0x20
/** Header byte of the ASN.1 type IA5String */
#define ASN1_TYPE_IA5_STRING 0x22
/** Header byte of the ASN.1 type UTCTime */
#define ASN1_TYPE_UTCTIME 0x23
/** Header byte of the ASN.1 type GeneralizedTime */
#define ASN1_TYPE_GENERALIZEDTIME 0x24

typedef struct asn1_tree asn1_tree;

/** Struct holding some decoded ASN.1 data */
struct asn1_tree {
  /** The tag of this ASN.1 element */
  uint8_t type;
  /** The length of \p data */
  uint32_t length;
  /** The data of this ASN.1 element */
  uint8_t* data;

  /** Link to the parent element. NULL, if no parent is available */
  asn1_tree* parent;
  /** Link to the first child, if this element is a SEQUENCE. NULL if there are no children. */
  asn1_tree* child;
  /** Link to the next sibling, if this element is part of a SEQUENCE. NULL if none available. */
  asn1_tree* next;
  /** Link to the previous sibling, if this element is part of a SEQUENCE. NULL if none available. */
  asn1_tree* prev;
};


/**
 * \brief Calculate the number of objects within some encoded data
 *
 * Use the result of this function to create the neccesary object to feed into ::der_decode
 *
 * \param in The DER encoded data to parse
 * \param inlen Length of the data available at \p in
 *
 * \return The number of objects found in \p in. Something negative in case of an error
 */
int32_t der_object_count(uint8_t* in, uint32_t inlen);

/**
 * \brief Gets the length of the whole TLV block at the beginning of \p in
 *
 * \param in The DER encoded data to parse
 * \param inlen Length of the data available at \p in
 * \param[out] data_offset The offset where the value part of the TLV block starts.
 *
 * \return Returns the total length of the first TLV block, 0xFFFFFFFF on error
 */
uint32_t fetch_tlv_length(uint8_t *in, uint32_t inlen, uint32_t *data_offset);

/**
 * \brief Calculates the length part of the whole TLV block at the beginning of \p in
 *
 * \param in The DER encoded data to parse
 * \param inlen Length of the data available at \p in
 *
 * \return Returns the length of the value of the first TLV block , 0xFFFFFFFF on error
 */
uint32_t fetch_data_length(uint8_t *in, uint32_t inlen);

/**
 * \brief Initializes an asn1_tree structure
 *
 * Sets the tag and the length to 0 and the data and the children to NULL
 */
void list_init(asn1_tree* list);

/**
 * \brief Encodes a certain length using DER
 *
 * Example:
 *  length = 1 -> encoded = {0x01}
 *  length = 128 -> encoded = {0x81, 0x80}
 *  length = 256 -> encoded = {0x82, 0x01, 0x00}
 *
 * \param length The length to encoded
 * \param encoded Memory to store the encoded length
 * \param encoded_length The ammount of memory available at \p encoded
 *
 * \return The number of bytes written, something negative in case of an error
 */
int32_t der_encode_length(uint32_t length, uint8_t* encoded, uint32_t encoded_length);

/**
 * \brief Calculates the number of bytes needed to encode some data of a certain length
 *
 * Example: data_length = 1 -> 1
 *          data_length = 127 -> 1
 *          data_length = 128 -> 2
 *          data_length = 256 -> 3
 *          data_length = 65535 -> 3
 *          data_length = 65536 -> 4
 *
 * \param data_length The length of the data that is about to be encoded
 * \return The number of bytes needed to encode the length of a data of length \p data_length
 */
uint32_t get_length_encoding_length (uint32_t data_length);

/**
 * \brief Calculates the length of the encoded data.
 *
 * Trusts the length values stored in the struct, doesn't parse the children
 *
 * \param asn1 The data strcuture whose encoded length is to be determined
 * \return The length of the DER encoded elements of \p asn1
 *
 */
uint32_t get_der_encoded_length(asn1_tree* asn1);

/**
 * \brief Calculates the length of the encoded data.
 *
 * Doesn't trust the values stored in the struct but parses all children recursively
 *
 * \param asn1 The data strcuture whose encoded length is to be determined
 * /return The length of the DER encoded elements of \p asn1
 *
 */
uint32_t get_der_encoded_length_recursive(asn1_tree* asn1);

/**
 * \brief Calculates the length data when encoding this structure
 *
 * Doesn't trust the values stored in asn1->data, but parses all children recursively
 *
 * \param asn1 The data strcuture whose data length is to be determined
 * \return The length of the DER encoded data part of \p asn1
 *
 */
uint32_t get_data_length_recursive(asn1_tree* asn1);

/**
 * \brief Try to decode some arbitrary DER encoded data
 *
 * \param in The data to be parsed
 * \param inlen Length of the data available at \p in
 * \param out Reference to the root object of the decoded data
 * \param out_objects Reference to a list of objects where decoded child can be stored to. The size of this list can be calculated with ::der_object_count
 * \param out_objects_count The number of items available at \p out_objects
 *
 * \return A negative value in case of an error
 */
int32_t der_decode(uint8_t *in, uint32_t inlen, asn1_tree *out, asn1_tree* out_objects, unsigned int out_objects_count);

/**
 * \brief Encodes the elements stored in an asn1_tree structure using DER
 *
 * \param asn1 The structure to encoded
 * \param encoded Memory where the encoded data is to be stored. Must be large enough.
 * \param encoded_length The ammount of memory available at \p encoded
 *
 * \return The number of bytes written, something negative in case of an error
 */
int32_t der_encode(asn1_tree* asn1, uint8_t* encoded, uint32_t encoded_length);

/**
 * \brief Adds a child element to a asn1_tree
 * 
 * \param asn1 The asn1 element that \p child will be added to
 * \param child The child to add to \p asn1
 *
 * \return 0 on success, a negative value on error
 */
int8_t add_child(asn1_tree* asn1, asn1_tree* child);

/**
 * \brief Encodes some integer value
 *
 * Example:
 * 1 -> 0x01
 * 127 -> 0x7f
 * 128 -> 0x00 0x80
 * 256 -> 0x01 0x00
 *
 * \param value The integer to encode
 * \param encoded Buffer where the encoded data is stored
 * \param encoded_length Length of the buffer at \p encoded
 *
 * \return The number of bytes written on success, something negative in case of an error
 */
int32_t encode_integer(uint32_t value, uint8_t* encoded, uint8_t encoded_length);

/**
 * \brief Decodes some integer value
 *
 * Examples: See the encode_integer function
 *
 * \param encoded Buffer where the encoded data is stored
 * \param encoded_length Length of the buffer at \p encoded
 * \param decoded The decoded value
 *
 * \return Something negative, if the decoding failed.
 */
int32_t decode_unsigned_integer(uint8_t* encoded, uint8_t encoded_length, uint32_t* decoded);

#ifdef __cplusplus
}
#endif


#endif
