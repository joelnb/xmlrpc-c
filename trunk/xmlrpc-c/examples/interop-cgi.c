/* A CGI which implements all of the test functions need for an interop
** endpoint. */

/* Get PACKAGE, VERSION and XMLRPC_HOST_TYPE from the source tree.
** The config headers won't be available to regular applications, so you'll
** need to define these macros yourself if you build outside the tree. */
#ifndef HAVE_WIN32_CONFIG_H
#include "xmlrpc_config.h"
#else
#include "xmlrpc_win32_config.h"
#endif

#include <xmlrpc.h>
#include <xmlrpc_cgi.h>


/*=========================================================================
**  Toolkit Identification
**=========================================================================
*/

static xmlrpc_value *
whichToolkit (xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
    /* Parse our argument array. */
    xmlrpc_parse_value(env, param_array, "()");
    if (env->fault_occurred)
	return NULL;

    /* Assemble our result. */
    return xmlrpc_build_value(env, "{s:s,s:s,s:s,s:s}",
			      "toolkitDocsUrl",
			      "http://xmlrpc-c.sourceforge.net/",
			      "toolkitName", PACKAGE,
			      "toolkitVersion", VERSION"+",
			      "toolkitOperatingSystem", XMLRPC_HOST_TYPE);
}

static char whichToolkit_help[] =
"Identify the toolkit used to implement this server.  The operating system "
"information is based on where the toolkit was compiled, not where it's "
"currently running.";


/*=========================================================================
**  Echo Tests
**=========================================================================
**  We're lazy--we only implement one actual echo method, but we hook it
**  up to lots of different names.
*/

static xmlrpc_value *
echoValue (xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
    xmlrpc_value *val;

    /* Parse our argument array. */
    xmlrpc_parse_value(env, param_array, "(V)", &val);
    if (env->fault_occurred)
	return NULL;

    /* Create a new reference (because both our parameter list and our
    ** return value will be DECREF'd when we return). */
    xmlrpc_INCREF(val);

    /* Return our result. */
    return val;
}

static char echoValue_help[] =
"Echo an arbitrary XML-RPC value of any type.";

static char echoString_help[] =
"Echo an arbitrary XML-RPC string.";

static char echoInteger_help[] =
"Echo an arbitrary XML-RPC integer.";

static char echoFloat_help[] =
"Echo an arbitrary XML-RPC float.";

static char echoStruct_help[] =
"Echo an arbitrary XML-RPC struct.";

static char echoDate_help[] =
"Echo an arbitrary XML-RPC date/time value.";

static char echoBase64_help[] =
"Echo an arbitrary XML-RPC Base64 value.";

static char echoStringArray_help[] =
"Echo an array of arbitrary XML-RPC strings.";

static char echoIntegerArray_help[] =
"Echo an array of arbitrary XML-RPC integers.";

static char echoFloatArray_help[] =
"Echo an array of arbitrary XML-RPC floats.";

static char echoStructArray_help[] =
"Echo an array of arbitrary XML-RPC structs.";


/*=========================================================================
**  Server Setup
**=========================================================================
**  Set up and run our server.
*/

int main (int argc, char **argv)
{
    /* Process our request. */
    xmlrpc_cgi_init(XMLRPC_CGI_NO_FLAGS);

    /* Add a method to identify our toolkit. */
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.whichToolkit",
				&whichToolkit, NULL,
				"S:", whichToolkit_help);

    /* Add a whole bunch of test methods. */
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoValue",
				&echoValue, NULL,
				"?", echoValue_help);
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoString",
				&echoValue, NULL,
				"s:s", echoString_help);
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoInteger",
				&echoValue, NULL,
				"i:i", echoInteger_help);
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoFloat",
				&echoValue, NULL,
				"d:d", echoFloat_help);
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoStruct",
				&echoValue, NULL,
				"S:S", echoStruct_help);
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoDate",
				&echoValue, NULL,
				"8:8", echoDate_help);
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoBase64",
				&echoValue, NULL,
				"6:6", echoBase64_help);
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoStringArray",
				&echoValue, NULL,
				"A:A", echoStringArray_help);
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoIntegerArray",
				&echoValue, NULL,
				"A:A", echoIntegerArray_help);
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoFloatArray",
				&echoValue, NULL,
				"A:A", echoFloatArray_help);
    xmlrpc_cgi_add_method_w_doc("interopEchoTests.echoStructArray",
				&echoValue, NULL,
				"A:A", echoStructArray_help);

    xmlrpc_cgi_process_call();
    xmlrpc_cgi_cleanup();

    return 0;
}
