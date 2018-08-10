/*
 * Copyright (C) 2016 Mathias Tausig, FH Campus Wien
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v3. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "tiny-asn1.h"


uint32_t fetch_tlv_length(uint8_t *in, uint32_t inlen, uint32_t *data_offset)
{
  uint32_t x, z;

  *data_offset = 0;

  /* skip type and read len */
  if (inlen < 2) {
    return 0xFFFFFFFF;
  }
  ++in; ++(*data_offset);

  /* read len */
  x = *in++; ++(*data_offset);

  /* <128 means literal */
  if (x < 128) {
    return x+*data_offset;
  }
  x &= 0x7F; /* the lower 7 bits are the length of the length */
  inlen -= 2;

  /* len means len of len! */
  if (x == 0 || x > 4 || x > inlen) {
    return 0xFFFFFFFF;
  }

  *data_offset += x;
  z = 0;
  while (x--) {
    z = (z<<8) | ((uint32_t)*in);
    ++in;
  }
  /* Check if adding z+data_offset would cause an overflow in the return type */
  if(UINT32_MAX - (*data_offset) < z)
    return 0xFFFFFFFF;

  return z+*data_offset;
}

uint32_t fetch_data_length(uint8_t *in, uint32_t inlen)
{
  uint32_t x, z;

  /* skip type and read len */
  if (inlen < 2) {
    return 0xFFFFFFFF;
  }
  ++in;

  /* read len */
  x = *in++;

  /* <128 means literal */
  if (x < 128) {
    return x;
  }
  x &= 0x7F; /* the lower 7 bits are the length of the length */
  inlen -= 2;

  /* len means len of len! */
  if (x == 0 || x > 4 || x > inlen) {
    return 0xFFFFFFFF;
  }

  z = 0;
  while (x--) {
    z = (z<<8) | ((uint32_t)*in);
    ++in;
  }
  return z;
}

uint32_t get_length_encoding_length (uint32_t data_length)
{
  uint32_t len_length;
  if(data_length < 128)
  len_length = 1;
  else {
    len_length = 1; // Start with one byte, which indicates the number of length bytes following
    uint32_t length = data_length;
    do {
      length = length >> 8;
      ++len_length;
    }while(length>0);
  }
  return len_length;
}

uint32_t get_der_encoded_length(asn1_tree* asn1)
{
  uint32_t len_length = get_length_encoding_length(asn1->length);
  return (1 + asn1->length + len_length);
}

uint32_t get_der_encoded_length_recursive(asn1_tree* asn1)
{
  if(asn1 == NULL)
    return 0xFFFFFFFF;

  uint32_t data_length = get_data_length_recursive(asn1);
  if(data_length == 0xFFFFFFFF)
    return 0xFFFFFFFF;
  return (1 + get_length_encoding_length(data_length) + data_length);
}

uint32_t get_data_length_recursive(asn1_tree* asn1)
{
  if(asn1 == NULL)
    return 0xFFFFFFFF;

  if((asn1->type & 0x20) != 0) {
    //We have a constructed type
    uint32_t data_length = 0;
    asn1_tree* child = asn1->child;
    while(child != NULL) {
      data_length += get_der_encoded_length_recursive(child);
      child = child->next;
    }
    return data_length;
  } else {
    //Not a constructed type. Just return the length of this item
    return asn1->length;
  }
}

/*
* Initialize an empty ASN.1 list
*
*/
void list_init(asn1_tree* list)
{
  list->type = 0;
  list->length = 0;
  list->data = NULL;
  list->prev = NULL;
  list->next = NULL;
  list->child = NULL;
  list->parent = NULL;
}

int32_t der_object_count(uint8_t* in, uint32_t inlen)
{
  //Check the input parameters
  if(in == NULL || inlen <= 0)
    return -1;

  //Check if we have enough data
  uint32_t in_data_offset;
  uint32_t in_encoded_length = fetch_tlv_length(in, inlen, &in_data_offset);
  if(in_encoded_length == 0xFFFFFFFF)
  // Error while calculationg in_encoded_length
    return -1;
  if(inlen < in_encoded_length)
  //Not enough data in in
    return -1;

  //If more data is passed, than is encoded (inlen >  (in_encoded_length + in_data_offset)), that data is ignored

  //uint8_t* in_data = (in + in_data_offset);
  if(in_encoded_length < in_data_offset)
    /* Check prevents integer overflow. This should not really happen */
    return -1;
  uint32_t in_data_length = in_encoded_length - in_data_offset;

  int object_count = 1;
  // out->type = *in;
  // out-> length = in_encoded_length;
  // out->data = (in+in_data_offset);

  // An ASN.1 type is contructed, if bit 6 (from 1..8) is 1
  if((*in & 0x20) != 0) {
    //Constructed type
    uint32_t children_length = 0;
    while(children_length < in_data_length) {
      uint8_t* child = (in+in_data_offset);
      uint32_t child_max_length = in_encoded_length - in_data_offset;
      if(in_encoded_length < in_data_offset)
	/* Check prevents integer overflow. This should not really happen */
	return -1;

      uint32_t child_data_offset;
      uint32_t child_length = fetch_tlv_length(child, child_max_length, &child_data_offset);
      if(child_length == 0xFFFFFFFF)
      // Error in the length encoding of the child
	return -1;
      if((child_length + children_length) > in_data_length)
      //The child is too long for our data
	return -1;
      //We got a valid child -> Calculate its number of objects and alter the offset
      int child_objects = der_object_count(child, child_length);
      if(child_objects == -1)
	return -1;
      object_count += child_objects;
      if(UINT32_MAX - child_length < children_length)
	/* Adding to child_length would cause an integer overflow. We must abort. */
	return -1;
      children_length += child_length;
      in_data_offset += child_length;
    }
  }

  return object_count;
}

int32_t der_decode(uint8_t *in, uint32_t inlen, asn1_tree *out, asn1_tree* out_objects, unsigned int out_objects_count)
{
  //Check the input parameters
  if(in == NULL || out == NULL || inlen <= 0)
    return -1;
  //Initialize the output element
  list_init(out);
  //Set the tag of this element
  out->type = *in;
  //Set the length and data of this element
  uint32_t in_data_offset;
  uint32_t in_encoded_length = fetch_tlv_length(in, inlen, &in_data_offset);
  if(in_encoded_length == 0xFFFFFFFF) {
    // Error while calculationg in_encoded_length
    return -2;
  }
  if(inlen < in_encoded_length) {
    //Not enough data in in
    return -3;
  }
  //If more data is passed, than is encoded (inlen >  (in_encoded_length + in_data_offset)), that data is ignored
  uint8_t* in_data = (in + in_data_offset);
  uint32_t in_data_length = in_encoded_length - in_data_offset;
  out->length = in_data_length;
  out->data = in_data;

  // An ASN.1 type is contructed, if bit 6 (from 1..8) is 1
  if((*in & 0x20) != 0) {
    //We have a constructed type
    //Now, we need to check if we have child objects available
    if(out_objects == NULL || out_objects_count <= 0) {
      return -1;
    }

    uint32_t children_length = 0;
    while(children_length < in_data_length) {
      uint8_t* child = (in+in_data_offset);
      uint32_t child_max_length = in_encoded_length - in_data_offset;
      uint32_t child_data_offset;
      uint32_t child_length = fetch_tlv_length(child, child_max_length, &child_data_offset);
      if(child_length == 0xFFFFFFFF)
      // Error in the length encoding of the child
      return -4;
      if((child_length + children_length) > in_data_length)
      //The child is too long for our data
      return -5;
      //We got a valid child -> Decode it and alter the offset
      uint32_t child_objects = der_object_count(child, child_length);
      if(child_objects == 0xFFFFFFFF || child_objects > out_objects_count) {
        return -6;
      }

      asn1_tree* child_object = out_objects;
      out_objects++;
      --out_objects_count;
      if(der_decode(child, child_length, child_object, out_objects, out_objects_count) < 0) {
        //Decoding went wrong
        return -7;
      }
      //Move the list of remaining child objects by the number of decoded objects (-1, because we have already moved the list a few lines above)
      out_objects += (child_objects-1);
      out_objects_count -= (child_objects-1);
      //attach the child to out. Attach it as child, or if that is already present as the next element of one of the child_objects
      child_object->parent = out;
      if(out->child == NULL) {
        out->child = child_object;
      } else {
        //There is already at least one child present
        asn1_tree* last_child = out->child;
        while (last_child->next != NULL) {
          last_child = last_child->next;
        }
        last_child->next = child_object;
        last_child->next->prev = last_child;
      }
      //set the total length and offset
      children_length += child_length;
      in_data_offset += child_length;
    }
  }

  return 1;
}

int32_t der_encode_length(uint32_t length, uint8_t* encoded, uint32_t encoded_length)
{
  //Check the input parameters
  if(encoded == 0 || encoded_length == 0)
    return -1;
  //Check if we have enough memory available to store the output
  uint32_t length_needed = get_length_encoding_length(length);
  if(length_needed > encoded_length)
    return -1;
  if(length_needed == 1) {
    //simple case, just store the length
    *encoded = (uint8_t)length;
  } else {
    //store the number of length bytes
    *encoded = 0x80 + (length_needed - 1);
    //store the length
    encoded += (length_needed-1);
    while(length > 0) {
      *encoded = length % 256;
      length = length / 256;
      encoded--;
    }
  }

  return length_needed;
}

int32_t der_encode(asn1_tree* asn1, uint8_t* encoded, uint32_t encoded_length)
{
  if(asn1 == NULL)
    return -1;
  //check if we have enough space
  uint32_t length_needed = get_der_encoded_length_recursive(asn1);
  if(length_needed > encoded_length)
    return -1;
  //store the tag
  *encoded = asn1->type;
  //store the length
  encoded++;
  encoded_length--;
  uint32_t data_length = get_data_length_recursive(asn1);
  if(data_length == 0xFFFFFFFF)
    return -1;
  int32_t length_encoded_length = der_encode_length(data_length, encoded, encoded_length);
  if(length_encoded_length < 0)
    //error while writing the length;
    return -1;
  //set encoded to the start of the data area
  encoded += length_encoded_length;
  encoded_length -= length_encoded_length;
  //Store the data
  if((asn1->type & 0x20) != 0) {
    //We have a constructed type
    asn1_tree* child = asn1->child;
    while(child != NULL) {
      int32_t child_encoded_length = der_encode(child, encoded, encoded_length);
      if(child_encoded_length < 0)
        //something went wrong while encoding the child
        return -1;
      //advance the pointer
      encoded += child_encoded_length;
      encoded_length -= child_encoded_length;
      child = child->next;
    }
  } else {
    //We have a primitive type
    //Just copy the data
    if(asn1->length > 0)
      memcpy(encoded, asn1->data, asn1->length);
  }

  return length_needed;
}

int8_t add_child(asn1_tree* asn1, asn1_tree* child)
{
  //Check the input values
  if(asn1 == NULL || child == NULL)
    return -1;

  //Check if we already have a child
  if(asn1->child == NULL) {
    asn1->child = child;
    child->parent = asn1;
    return 0;
  } else {
    asn1_tree* last_child = asn1->child;
    while(last_child->next != NULL)
      last_child = last_child->next;
    last_child->next = child;
    child->prev = last_child;
    child->parent = asn1;
    return 0;
  }
}

int32_t encode_integer(uint32_t value, uint8_t* encoded, uint8_t encoded_length)
{
  //Check the input variables
  if(encoded == NULL || encoded_length == 0)
    return -1;

  uint8_t reverse_encoded[5];
  uint8_t encoded_bytes = 0;

  //Special case: value=0
  if(value == 0) {
    encoded[0] = 0;
    return 1;
  }
  //Calculate the encoding in reverse order
  while(value > 0) {
    reverse_encoded[encoded_bytes] = value % 256;
    value = value / 256;
    ++encoded_bytes;
  }
  uint8_t bytes_needed = encoded_bytes;
  uint8_t padding_needed = 0;
  if(reverse_encoded[encoded_bytes-1] > 0x7f) {
    padding_needed = 1;
    ++bytes_needed;
  }
  if(bytes_needed > encoded_length)
    return -2;

  if(padding_needed) {
    encoded[0] = 0x00;
    encoded++;
  }
  for(uint8_t i=0;i<encoded_bytes;++i)
    encoded[i] = reverse_encoded[encoded_bytes-i-1];

  return bytes_needed;  
}

int32_t decode_unsigned_integer(uint8_t* encoded, uint8_t encoded_length, uint32_t* decoded)
{
  /* Check the input values */
  if(encoded == NULL || encoded_length == 0)
    return -1;

  /* We are returning an uint32_t -> we can only handle up to 4 bytes (5, if the first is zero) */
  if( ((encoded[0] == 0) && encoded_length > 5) || ((encoded[0] != 0) && encoded_length > 4) )
    return -1;
  
  /* If the most significant bit of the encoded data ist 1, the number is negative -> error, since we are only decoding an unsigned value here */
  if(encoded[0] & 0x80)
    return -1;

  *decoded = 0;
  while(encoded_length > 0) {
    *decoded *= 256;
    *decoded += *encoded;
    ++encoded;
    --encoded_length;
  }

  return 0;    
}
