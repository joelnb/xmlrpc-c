/* Copyright (C) 2001 by First Peer, Inc. All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission. 
**  
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE. */

#ifndef HAVE_WIN32_CONFIG_H
#include "xmlrpc_config.h"
#else
#include "xmlrpc_win32_config.h"
#endif

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#define  XMLRPC_WANT_INTERNAL_DECLARATIONS
#include "xmlrpc.h"
#include "xmlrpc_xmlparser.h"


/*=========================================================================
**  Data Format
**=========================================================================
**  All XML-RPC documents contain a single methodCall or methodResponse
**  element.
**
**  methodCall     methodName, params
**  methodResponse (params|fault)
**  params         param*
**  param          value
**  fault          value
**  value          (i4|int|boolean|string|double|dateTime.iso8601|base64|
**                  struct|array)
**  array          data
**  data           value*
**  struct         member*
**  member         name, value
**
**  Contain CDATA: methodName, i4, int, boolean, string, double,
**                 dateTime.iso8601, base64, name
**
**  We attempt to validate the structure of the XML document carefully.
**  We also try *very* hard to handle malicious data gracefully, and without
**  leaking memory.
**
**  The CHECK_NAME and CHECK_CHILD_COUNT macros examine an XML element, and
**  invoke XMLRPC_FAIL if something looks wrong.
*/

#define CHECK_NAME(env,elem,name) \
    do \
        if (strcmp((name), xml_element_name(elem)) != 0) \
            XMLRPC_FAIL2(env, XMLRPC_PARSE_ERROR, \
			 "Expected element of type <%s>, found <%s>", \
                         (name), xml_element_name(elem)); \
    while (0)

#define CHECK_CHILD_COUNT(env,elem,count) \
    do \
        if (xml_element_children_size(elem) != (count)) \
            XMLRPC_FAIL3(env, XMLRPC_PARSE_ERROR, \
			 "Expected <%s> to have %d children, found %d", \
                         xml_element_name(elem), (count), \
                         xml_element_children_size(elem)); \
    while (0)


/*=========================================================================
**  Number-Parsing Functions
**=========================================================================
**  These functions mirror atoi, atof, etc., but provide better
**  error-handling.  These routines may reset errno to zero.
*/

static xmlrpc_int32
xmlrpc_atoi (xmlrpc_env *env, char *str, size_t strlen,
	     xmlrpc_int32 min, xmlrpc_int32 max)
{
    long i;
    char *end;

    XMLRPC_ASSERT_ENV_OK(env);
    XMLRPC_ASSERT_PTR_OK(str);

    /* Suppress compiler warnings. */
    i = 0;

    /* Check for leading white space. */
    if (isspace(str[0]))
	XMLRPC_FAIL1(env, XMLRPC_PARSE_ERROR,
		     "\"%s\" must not contain whitespace", str);

    /* Convert the value. */
    end = str + strlen;
    errno = 0;
    i = strtol(str, &end, 10);

    /* Look for ERANGE. */
    if (errno != 0)
	/* XXX - Do all operating systems have thread-safe strerror? */
	XMLRPC_FAIL3(env, XMLRPC_PARSE_ERROR,
		     "error parsing \"%s\": %s (%d)",
		     str, strerror(errno), errno);

    /* Look for out-of-range errors which didn't produce ERANGE. */
    if (i < min || i > max)
	XMLRPC_FAIL3(env, XMLRPC_PARSE_ERROR,
		     "\"%s\" must be in range %d to %d", str, min, max);

    /* Check for unused characters. */
    if (end != str + strlen)
	XMLRPC_FAIL1(env, XMLRPC_PARSE_ERROR,
		     "\"%s\" contained trailing data", str);

 cleanup:
    errno = 0;
    if (env->fault_occurred)
	return 0;
    return (xmlrpc_int32) i;
}

static double
xmlrpc_atod (xmlrpc_env *env, char *str, size_t strlen)
{
    double d;
    char *end;

    XMLRPC_ASSERT_ENV_OK(env);
    XMLRPC_ASSERT_PTR_OK(str);

    /* Suppress compiler warnings. */
    d = 0.0;

    /* Check for leading white space. */
    if (isspace(str[0]))
	XMLRPC_FAIL1(env, XMLRPC_PARSE_ERROR,
		     "\"%s\" must not contain whitespace", str);

    /* Convert the value. */
    end = str + strlen;
    errno = 0;
    d = strtod(str, &end);

    /* Look for ERANGE. */
    if (errno != 0)
	/* XXX - Do all operating systems have thread-safe strerror? */
	XMLRPC_FAIL3(env, XMLRPC_PARSE_ERROR,
		     "error parsing \"%s\": %s (%d)",
		     str, strerror(errno), errno);

    /* Check for unused characters. */
    if (end != str + strlen)
	XMLRPC_FAIL1(env, XMLRPC_PARSE_ERROR,
		     "\"%s\" contained trailing data", str);

 cleanup:
    errno = 0;
    if (env->fault_occurred)
	return 0.0;
    return d;
}


/*=========================================================================
**  make_string
**=========================================================================
**  Make an XML-RPC string.
**
** SECURITY: We validate our UTF-8 first.  This incurs a performance
** penalty, but ensures that we will never pass maliciously malformed
** UTF-8 data back up to the user layer, where it could wreak untold
** damange. Don't comment out this check unless you know *exactly* what
** you're doing.  (Win32 developers who remove this check are *begging*
** to wind up on BugTraq, because many of the Win32 filesystem routines
** rely on an insecure UTF-8 decoder.)
**
** XXX - This validation is redundant if the user chooses to convert
** UTF-8 data into a wchar_t string.
*/

static xmlrpc_value *
make_string(xmlrpc_env *env, char *cdata, size_t cdata_size)
{
#ifdef HAVE_UNICODE_WCHAR
    xmlrpc_validate_utf8(env, cdata, cdata_size);
#endif

    if (env->fault_occurred)
	return NULL;
    return xmlrpc_build_value(env, "s#", cdata, cdata_size);
}


/*=========================================================================
**  convert_value
**=========================================================================
**  Convert an XML element representing a value into an xmlrpc_value.
*/

static xmlrpc_value *
convert_array (xmlrpc_env *env, unsigned *depth, xml_element *elem);
static xmlrpc_value *
convert_struct (xmlrpc_env *env, unsigned *depth, xml_element *elem);

static xmlrpc_value *
convert_value (xmlrpc_env *env, unsigned *depth, xml_element *elem)
{
    xml_element *child;
    int child_count;
    char *cdata, *child_name;
    size_t cdata_size, ascii_len;
    xmlrpc_mem_block *decoded;
    unsigned char *ascii_data;
    xmlrpc_value *retval;
    xmlrpc_int32 i;
    double d;

    XMLRPC_ASSERT_ENV_OK(env);
    XMLRPC_ASSERT(elem != NULL);

    /* Error-handling precoditions.
    ** If we haven't changed any of these from their default state, we're
    ** allowed to tail-call xmlrpc_build_value. */
    retval = NULL;
    decoded = NULL;

    /* Make sure we haven't recursed too deeply. */
    if (*depth > xmlrpc_limit_get(XMLRPC_NESTING_LIMIT_ID))
	XMLRPC_FAIL(env, XMLRPC_PARSE_ERROR,
		    "Nested data structure too deep.");

    /* Validate our structure, and see whether we have a child element. */
    CHECK_NAME(env, elem, "value");
    child_count = xml_element_children_size(elem);

    if (child_count == 0) {
	/* We have no type element, so treat the value as a string. */
	cdata = xml_element_cdata(elem);
	cdata_size = xml_element_cdata_size(elem);
	return make_string(env, cdata, cdata_size);
    } else {
	/* We should have a type tag inside our value tag. */
	CHECK_CHILD_COUNT(env, elem, 1);
	child = xml_element_children(elem)[0];
	
	/* Parse our value-containing element. */
	child_name = xml_element_name(child);
	if (strcmp(child_name, "struct") == 0) {
	    return convert_struct(env, depth, child);
	} else if (strcmp(child_name, "array") == 0) {
	    CHECK_CHILD_COUNT(env, child, 1);
	    return convert_array(env, depth, child);
	} else {
	    CHECK_CHILD_COUNT(env, child, 0);
	    cdata = xml_element_cdata(child);
	    cdata_size = xml_element_cdata_size(child);
	    if (strcmp(child_name, "i4") == 0 ||
		strcmp(child_name, "int") == 0)
	    {
		i = xmlrpc_atoi(env, cdata, strlen(cdata),
				XMLRPC_INT32_MIN, XMLRPC_INT32_MAX);
		XMLRPC_FAIL_IF_FAULT(env);
		return xmlrpc_build_value(env, "i", i);
	    } else if (strcmp(child_name, "string") == 0) {
		return make_string(env, cdata, cdata_size);
	    } else if (strcmp(child_name, "boolean") == 0) {
		i = xmlrpc_atoi(env, cdata, strlen(cdata), 0, 1);
		XMLRPC_FAIL_IF_FAULT(env);
		return xmlrpc_build_value(env, "b", (xmlrpc_bool) i);
	    } else if (strcmp(child_name, "double") == 0) {
		d = xmlrpc_atod(env, cdata, strlen(cdata));
		XMLRPC_FAIL_IF_FAULT(env);
		return xmlrpc_build_value(env, "d", d);
	    } else if (strcmp(child_name, "dateTime.iso8601") == 0) {
		return xmlrpc_build_value(env, "8", cdata);
	    } else if (strcmp(child_name, "base64") == 0) {
		/* No more tail calls once we do this! */
		decoded = xmlrpc_base64_decode(env, cdata, cdata_size);
		XMLRPC_FAIL_IF_FAULT(env);
		ascii_data = XMLRPC_TYPED_MEM_BLOCK_CONTENTS(unsigned char,
							     decoded);
		ascii_len = XMLRPC_TYPED_MEM_BLOCK_SIZE(unsigned char,
							decoded);
		retval = xmlrpc_build_value(env, "6", ascii_data, ascii_len);
		XMLRPC_FAIL_IF_FAULT(env);		
	    } else {
		XMLRPC_FAIL1(env, XMLRPC_PARSE_ERROR,
			     "Unknown value type <%s>", child_name);
	    }
	}
    }

 cleanup:
    if (decoded)
	xmlrpc_mem_block_free(decoded);
    if (env->fault_occurred) {
	if (retval)
	    xmlrpc_DECREF(retval);
	return NULL;
    }
    return retval;
}


/*=========================================================================
**  convert_array
**=========================================================================
**  Convert an XML element representing an array into an xmlrpc_value.
*/

static xmlrpc_value *
convert_array (xmlrpc_env *env, unsigned *depth, xml_element *elem)
{
    xml_element *data, **values, *value;
    xmlrpc_value *array, *item;
    int size, i;

    XMLRPC_ASSERT_ENV_OK(env);
    XMLRPC_ASSERT(elem != NULL);

    /* Set up our error-handling preconditions. */
    array = item = NULL;
    (*depth)++;

    /* Allocate an array to hold our values. */
    array = xmlrpc_build_value(env, "()");
    XMLRPC_FAIL_IF_FAULT(env);

    /* We don't need to check our element name--our callers do that. */
    CHECK_CHILD_COUNT(env, elem, 1);
    data = xml_element_children(elem)[0];
    CHECK_NAME(env, data, "data");
    
    /* Iterate over our children. */
    values = xml_element_children(data);
    size = xml_element_children_size(data);
    for (i = 0; i < size; i++) {
	value = values[i];
	item = convert_value(env, depth, value);
	XMLRPC_FAIL_IF_FAULT(env);

	xmlrpc_array_append_item(env, array, item);
	xmlrpc_DECREF(item);
	item = NULL;
	XMLRPC_FAIL_IF_FAULT(env);
    }

 cleanup:
    (*depth)--;
    if (item)
	xmlrpc_DECREF(item);
    if (env->fault_occurred) {
	if (array)
	    xmlrpc_DECREF(array);
	return NULL;
    }
    return array;
}


/*=========================================================================
**  convert_struct
**=========================================================================
**  Convert an XML element representing a struct into an xmlrpc_value.
*/

static xmlrpc_value *
convert_struct (xmlrpc_env *env, unsigned *depth, xml_element *elem)
{
    xmlrpc_value *strct, *key, *value;
    xml_element **members, *member, *name_elem, *value_elem;
    int size, i;
    char *cdata;
    size_t cdata_size;

    XMLRPC_ASSERT_ENV_OK(env);
    XMLRPC_ASSERT(elem != NULL);

    /* Set up our error-handling preconditions. */
    strct = key = value = NULL;
    (*depth)++;

    /* Allocate an array to hold our members. */
    strct = xmlrpc_struct_new(env);
    XMLRPC_FAIL_IF_FAULT(env);

    /* Iterate over our children, extracting key/value pairs. */
    /* We don't need to check our element name--our callers do that. */
    members = xml_element_children(elem);
    size = xml_element_children_size(elem);
    for (i = 0; i < size; i++) {
	member = members[i];
	CHECK_NAME(env, member, "member");
	CHECK_CHILD_COUNT(env, member, 2);

	/* Get our key. */
	name_elem = xml_element_children(member)[0];
	CHECK_NAME(env, name_elem, "name");
	CHECK_CHILD_COUNT(env, name_elem, 0);
	cdata = xml_element_cdata(name_elem);
	cdata_size = xml_element_cdata_size(name_elem);
	key = make_string(env, cdata, cdata_size);
	XMLRPC_FAIL_IF_FAULT(env);

	/* Get our value. */
	value_elem = xml_element_children(member)[1];
	value = convert_value(env, depth, value_elem);
	XMLRPC_FAIL_IF_FAULT(env);

	/* Add the key/value pair to our struct. */
	xmlrpc_struct_set_value_v(env, strct, key, value);
	XMLRPC_FAIL_IF_FAULT(env);

	/* Release our references & memory, and restore our invariants. */
	xmlrpc_DECREF(key);
	key = NULL;
	xmlrpc_DECREF(value);
	value = NULL;
    }
    
 cleanup:
    (*depth)--;
    if (key)
	xmlrpc_DECREF(key);
    if (value)
	xmlrpc_DECREF(value);
    if (env->fault_occurred) {
	if (strct)
	    xmlrpc_DECREF(strct);
	return NULL;
    }
    return strct;
}


/*=========================================================================
**  convert_params
**=========================================================================
**  Convert an XML element representing a list of params into an
**  xmlrpc_value (of type array).
*/

static xmlrpc_value *
convert_params (xmlrpc_env *env, unsigned *depth, xml_element *elem)
{
    xmlrpc_value *array, *item;
    int size, i;
    xml_element **params, *param, *value;

    XMLRPC_ASSERT_ENV_OK(env);
    XMLRPC_ASSERT(elem != NULL);

    /* Set up our error-handling preconditions. */
    array = item = NULL;

    /* Allocate an array to hold our parameters. */
    array = xmlrpc_build_value(env, "()");
    XMLRPC_FAIL_IF_FAULT(env);

    /* We're responsible for checking our own element name. */
    CHECK_NAME(env, elem, "params");    

    /* Iterate over our children. */
    size = xml_element_children_size(elem);
    params = xml_element_children(elem);
    for (i = 0; i < size; i++) {
	param = params[i];
	CHECK_NAME(env, param, "param");
	CHECK_CHILD_COUNT(env, param, 1);

	value = xml_element_children(param)[0];
	item = convert_value(env, depth, value);
	XMLRPC_FAIL_IF_FAULT(env);

	xmlrpc_array_append_item(env, array, item);
	xmlrpc_DECREF(item);
	item = NULL;
	XMLRPC_FAIL_IF_FAULT(env);
    }

 cleanup:
    if (env->fault_occurred) {
	if (array)
	    xmlrpc_DECREF(array);
	if (item)
	    xmlrpc_DECREF(item);
	return NULL;
    }
    return array;
}


/*=========================================================================
**  xmlrpc_parse_call
**=========================================================================
**  Given some XML text, attempt to parse it as an XML-RPC call. Return
**  a newly allocated xmlrpc_call structure (or NULL, if an error occurs).
**  The two output variables will contain either valid values (which
**  must free() and xmlrpc_DECREF(), respectively) or NULLs (if an error
**  occurs).
*/

void xmlrpc_parse_call (xmlrpc_env *env,
			char *xml_data,
			size_t xml_len,
			char **out_method_name,
			xmlrpc_value **out_param_array)
{
    xml_element *call_elem, *name_elem, *params_elem;
    char *cdata;
    unsigned depth;

    XMLRPC_ASSERT_ENV_OK(env);
    XMLRPC_ASSERT(xml_data != NULL);
    XMLRPC_ASSERT(out_method_name != NULL && out_param_array != NULL);

    /* Set up our error-handling preconditions. */
    *out_method_name = NULL;
    *out_param_array = NULL;
    call_elem = NULL;

    /* SECURITY: Last-ditch attempt to make sure our content length is legal.
    ** XXX - This check occurs too late to prevent an attacker from creating
    ** an enormous memory block in RAM, so you should try to enforce it
    ** *before* reading any data off the network. */
    if (xml_len > xmlrpc_limit_get(XMLRPC_XML_SIZE_LIMIT_ID))
	XMLRPC_FAIL(env, XMLRPC_LIMIT_EXCEEDED_ERROR,
		    "XML-RPC request too large");

    /* Parse our XML data. */
    call_elem = xml_parse(env, xml_data, xml_len);
    XMLRPC_FAIL_IF_FAULT(env);

    /* Pick apart and verify our structure. */
    CHECK_NAME(env, call_elem, "methodCall");
    CHECK_CHILD_COUNT(env, call_elem, 2);
    name_elem = xml_element_children(call_elem)[0];
    params_elem = xml_element_children(call_elem)[1];

    /* Extract the method name.
    ** SECURITY: We make sure the method name is valid UTF-8. */
    CHECK_NAME(env, name_elem, "methodName");
    CHECK_CHILD_COUNT(env, name_elem, 0);
    cdata = xml_element_cdata(name_elem);
#ifdef HAVE_UNICODE_WCHAR
    xmlrpc_validate_utf8(env, cdata, strlen(cdata));
    XMLRPC_FAIL_IF_FAULT(env);
#endif /* HAVE_UNICODE_WCHAR */
    *out_method_name = (char*) malloc(strlen(cdata) + 1);
    XMLRPC_FAIL_IF_NULL(*out_method_name, env, XMLRPC_INTERNAL_ERROR,
			"Could not allocate memory for method name");
    strcpy(*out_method_name, cdata);
    
    /* Convert our parameters. */
    depth = 0;
    *out_param_array = convert_params(env, &depth, params_elem);
    XMLRPC_ASSERT(depth == 0);
    XMLRPC_FAIL_IF_FAULT(env);

 cleanup:
    if (call_elem)
	xml_element_free(call_elem);
    if (env->fault_occurred) {
	if (*out_method_name)
	    free(*out_method_name);
	if (*out_param_array)
	    xmlrpc_DECREF(*out_param_array);
	*out_method_name = NULL;
	*out_param_array = NULL;
    }
}


/*=========================================================================
**  xmlrpc_parse_response
**=========================================================================
**  Given some XML text, attempt to parse it as an XML-RPC response.
**  If the response is a regular, valid response, return a new reference
**  to the appropriate value. If the response is a fault, or an error
**  occurs during processing, return NULL and set up env appropriately.
*/

xmlrpc_value *xmlrpc_parse_response (xmlrpc_env *env,
                                     char *xml_data,
                                     size_t xml_len)
{
    xml_element *response, *child, *value;
    unsigned depth;
    xmlrpc_value *params, *retval, *fault;
    int retval_incremented;

    xmlrpc_value *fault_code_value, *fault_str_value;
    xmlrpc_int32 fault_code;
    char *fault_str;

    XMLRPC_ASSERT_ENV_OK(env);
    XMLRPC_ASSERT(xml_data != NULL);

    /* Set up our error-handling preconditions. */
    response = NULL;
    params = fault = NULL;
    retval_incremented = 0;

    /* SECURITY: Last-ditch attempt to make sure our content length is legal.
    ** XXX - This check occurs too late to prevent an attacker from creating
    ** an enormous memory block in RAM, so you should try to enforce it
    ** *before* reading any data off the network. */
    if (xml_len > xmlrpc_limit_get(XMLRPC_XML_SIZE_LIMIT_ID))
	XMLRPC_FAIL(env, XMLRPC_LIMIT_EXCEEDED_ERROR,
		    "XML-RPC response too large");

    /* SECURITY: Set up our recursion depth counter. */
    depth = 0;

    /* Parse our XML data. */
    response = xml_parse(env, xml_data, xml_len);
    XMLRPC_FAIL_IF_FAULT(env);

    /* Pick apart and verify our structure. */
    CHECK_NAME(env, response, "methodResponse");
    CHECK_CHILD_COUNT(env, response, 1);
    child = xml_element_children(response)[0];

    /* Parse the response itself. */
    if (strcmp("params", xml_element_name(child)) == 0) {

	/* Convert our parameter list. */
	params = convert_params(env, &depth, child);
	XMLRPC_FAIL_IF_FAULT(env);

	/* Extract the return value, and jiggle our reference counts. */
	xmlrpc_parse_value(env, params, "(V)", &retval);
	XMLRPC_FAIL_IF_FAULT(env);
	xmlrpc_INCREF(retval);
	retval_incremented = 1;

    } else if (strcmp("fault", xml_element_name(child)) == 0) {
	
	/* Convert our fault structure. */
	CHECK_CHILD_COUNT(env, child, 1);
	value = xml_element_children(child)[0];
	fault = convert_value(env, &depth, value);
	XMLRPC_FAIL_IF_FAULT(env);
	XMLRPC_TYPE_CHECK(env, fault, XMLRPC_TYPE_STRUCT);

	/* Get our fault code. */
	fault_code_value = xmlrpc_struct_get_value(env, fault, "faultCode");
	XMLRPC_FAIL_IF_FAULT(env);
	xmlrpc_parse_value(env, fault_code_value, "i", &fault_code);
	XMLRPC_FAIL_IF_FAULT(env);

	/* Get our fault string. */
	fault_str_value = xmlrpc_struct_get_value(env, fault, "faultString");
	XMLRPC_FAIL_IF_FAULT(env);
	xmlrpc_parse_value(env, fault_str_value, "s", &fault_str);
	XMLRPC_FAIL_IF_FAULT(env);

	/* Return our fault. */
	XMLRPC_FAIL(env, fault_code, fault_str);

    } else {
	XMLRPC_FAIL(env, XMLRPC_PARSE_ERROR,
		    "Expected <params> or <fault> in <methodResponse>");
    }

    /* Sanity-check our depth-counting code. */
    XMLRPC_ASSERT(depth == 0);
    
 cleanup:
    if (response)
	xml_element_free(response);
    if (params)
	xmlrpc_DECREF(params);
    if (fault)
	xmlrpc_DECREF(fault);

    if (env->fault_occurred) {
	if (retval_incremented)
	    xmlrpc_DECREF(retval);
	return NULL;
    }
    return retval;
}

