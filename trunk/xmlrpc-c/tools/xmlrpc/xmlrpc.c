/* Make an XML-RPC call.

   User specifies details of the call on the command line.

   We print the result on Standard Output.

   Example:

     $ xmlrpc http://localhost:8080/RPC2 sample.add i/3 i/5
     Result:
       Integer: 8

     $ xmlrpc localhost:8080 sample.add i/3 i/5
     Result:
       Integer: 8

   This is just the beginnings of this program.  It should be extended
   to deal with all types of parameters and results.

   An example of a good syntax for parameters would be:

     $ xmlrpc http://www.oreillynet.com/meerkat/xml-rpc/server.php \
         meerkat.getItems \
         struct/{search:linux,descriptions:i/76,time_period:12hour}
     Result:
       Array:
         Struct:
           title: String: DatabaseJournal: OpenEdge-Based Finance ...
           link: String: http://linuxtoday.com/news_story.php3?ltsn=...
           description: String: "Finance application with embedded ...
         Struct:
           title: ...
           link: ...
           description: ...

*/

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <xmlrpc.h>
#include <xmlrpc_client.h>

#include "config.h"  /* information about this build environment */
#include "casprintf.h"
#include "mallocvar.h"
#include "cmdline_parser.h"

#define NAME "xmlrpc command line program"
#define VERSION "1.0"

struct cmdlineInfo {
    const char *  url;
    const char *  methodName;
    unsigned int  paramCount;
    const char ** params;
        /* Array of parameters, in order.  Has 'paramCount' entries. */
    const char *  transport;
        /* Name of XML transport he wants to use.  NULL if he has no 
           preference.
        */
};



static void 
die_if_fault_occurred (xmlrpc_env * const envP) {
    if (envP->fault_occurred) {
        fprintf(stderr, "Error: %s (%d)\n",
                envP->fault_string, envP->fault_code);
        exit(1);
    }
}



static void GNU_PRINTF_ATTR(2,3)
setError(xmlrpc_env * const envP, const char format[], ...) {
    va_list args;
    char * faultString;

    va_start(args, format);

    vasprintf(&faultString, format, args);
    va_end(args);

    xmlrpc_env_set_fault(envP, XMLRPC_INTERNAL_ERROR, faultString);

    free(faultString);
}
      


static void
parseCommandLine(xmlrpc_env *         const envP,
                 int                  const argc,
                 const char **        const argv,
                 struct cmdlineInfo * const cmdlineP) {

    cmdlineParser cp = cmd_createOptionParser();

    const char * error;

    cmd_defineOption(cp, "transport", OPTTYPE_STRING);

    cmd_processOptions(cp, argc, argv, &error);

    if (error) {
        setError(envP, "Command syntax error.  %s", error);
        strfree(error);
    } else {
        cmdlineP->transport = cmd_getOptionValueString(cp, "transport");

        if (cmd_argumentCount(cp) < 2)
            setError(envP, "Not enough arguments.  Need at least a URL and "
                     "method name.");
        else {
            unsigned int i;

            cmdlineP->url        = cmd_getArgument(cp, 0);
            cmdlineP->methodName = cmd_getArgument(cp, 1);
            cmdlineP->paramCount = cmd_argumentCount(cp) - 2;
            MALLOCARRAY(cmdlineP->params, cmdlineP->paramCount);
            for (i = 0; i < cmdlineP->paramCount; ++i)
                cmdlineP->params[i] = cmd_getArgument(cp, i+2);
        }
    }
    cmd_destroyOptionParser(cp);
}



static void
freeCmdline(struct cmdlineInfo const cmdline) {

    unsigned int i;
    
    strfree(cmdline.url);
    strfree(cmdline.methodName);
    if (cmdline.transport)
        strfree(cmdline.transport);
    for (i = 0; i < cmdline.paramCount; ++i)
        strfree(cmdline.params[i]);
}



static void
computeUrl(const char *  const urlArg,
           const char ** const urlP) {

    if (strstr(urlArg, "://") != 0) {
        *urlP = strdup(urlArg);
    } else {
        casprintf(urlP, "http://%s/RPC2", urlArg);
    }        
}



static void
computeParameter(xmlrpc_env *    const envP,
                 const char *    const paramArg,
                 xmlrpc_value ** const paramPP) {

    if (strncmp(paramArg, "s/", 2) == 0) {
        /* It's "s/..." -- a string */
        *paramPP = xmlrpc_build_value(envP, "s", &paramArg[2]);
    } else if (strncmp(paramArg, "i/", 2) == 0) {
        /* It's "i/..." -- an integer */
        const char * integerString = &paramArg[2];

        if (strlen(integerString) < 1)
            setError(envP, "Integer argument has nothing after the 'i/'");
        else {
            long value;
            char * tailptr;

            value = strtol(integerString, &tailptr, 10);

            if (*tailptr != '\0')
                setError(envP, 
                         "Integer argument has non-digit crap in it: '%s'",
                         tailptr);
            else
                *paramPP = xmlrpc_build_value(envP, "i", value);
        }
    } else {
        /* It's not in normal type/value format, so we take it to be
           the shortcut string notation 
        */
        *paramPP = xmlrpc_build_value(envP, "s", paramArg);
    }

}



static void
computeParamArray(xmlrpc_env *    const envP,
                  unsigned int    const paramCount,
                  const char **   const params,
                  xmlrpc_value ** const paramArrayPP) {
    
    unsigned int i;

    xmlrpc_value * paramArrayP;

    paramArrayP = xmlrpc_build_value(envP, "()");

    for (i = 0; i < paramCount && !envP->fault_occurred; ++i) {
        xmlrpc_value * paramP;

        computeParameter(envP, params[i], &paramP);

        if (!envP->fault_occurred) {
            xmlrpc_array_append_item(envP, paramArrayP, paramP);

            xmlrpc_DECREF(paramP);
        }
    }
    *paramArrayPP = paramArrayP;
}



/* Forward declaration for recursion */
static void
dumpValue(const char *   const prefix,
          xmlrpc_value * const valueP);



static void
dumpInt(const char *   const prefix,
        xmlrpc_value * const valueP) {

    xmlrpc_env env;
    xmlrpc_int value;
    
    xmlrpc_env_init(&env);

    xmlrpc_parse_value(&env, valueP, "i", &value);
    
    if (env.fault_occurred)
        printf("Unable to parse integer in result.  %s\n",
               env.fault_string);
    else
        printf("%sInteger: %d\n", prefix, value);

    xmlrpc_env_clean(&env);
}



static void
dumpBool(const char *   const prefix,
         xmlrpc_value * const valueP) {

    xmlrpc_env env;
    xmlrpc_bool value;
    
    xmlrpc_env_init(&env);

    xmlrpc_parse_value(&env, valueP, "b", &value);
    
    if (env.fault_occurred)
        printf("Unable to parse boolean in result.  %s\n",
               env.fault_string);
    else
        printf("%sBoolean: %s\n", prefix, value ? "TRUE" : "FALSE");

    xmlrpc_env_clean(&env);
}




static void
dumpDouble(const char *   const prefix,
           xmlrpc_value * const valueP) {

    xmlrpc_env env;
    xmlrpc_double value;
    
    xmlrpc_env_init(&env);

    xmlrpc_parse_value(&env, valueP, "d", &value);
    
    if (env.fault_occurred)
        printf("Unable to parse floating point number in result.  %s\n",
               env.fault_string);
    else
        printf("%sFloating Point: %f\n", prefix, value);

    xmlrpc_env_clean(&env);
}



static void
dumpDatetime(const char *   const prefix,
             xmlrpc_value * const valueP) {

    printf("%sDon't know how to print datetime value result %p.\n", 
           prefix, valueP);
}



static void
dumpString(const char *   const prefix,
           xmlrpc_value * const valueP) {

    xmlrpc_env env;
    const char * value;
    
    xmlrpc_env_init(&env);

    xmlrpc_parse_value(&env, valueP, "s", &value);
    
    if (env.fault_occurred)
        printf("Unable to parse string in result.  %s\n",
               env.fault_string);
    else
        printf("%sString: '%s'\n", prefix, value);

    xmlrpc_env_clean(&env);
}



static void
dumpBase64(const char *   const prefix,
           xmlrpc_value * const valueP) {

    xmlrpc_env env;
    const unsigned char * value;
    size_t length;
    
    xmlrpc_env_init(&env);

    xmlrpc_parse_value(&env, valueP, "6", &value, &length);
    
    if (env.fault_occurred)
        printf("Unable to parse base64 bit strnig in result.  %s\n",
               env.fault_string);
    else {
        unsigned int i;

        printf("%sBit string: ", prefix);
        for (i = 0; i < length; ++i)
            printf("%02x", value[i]);
    }
    xmlrpc_env_clean(&env);
}



static void
dumpArray(const char *   const prefix,
          xmlrpc_value * const arrayP) {

    xmlrpc_env env;
    unsigned int arraySize;

    xmlrpc_env_init(&env);

    XMLRPC_ASSERT_ARRAY_OK(arrayP);

    arraySize = xmlrpc_array_size(&env, arrayP);
    if (env.fault_occurred)
        printf("Unable to get array size.  %s\n", env.fault_string);
    else {
        int const spaceCount = strlen(prefix);

        unsigned int i;
        const char * blankPrefix;

        printf("%sArray of %u items:\n", prefix, arraySize);

        casprintf(&blankPrefix, "%*s", spaceCount, "");

        for (i = 0; i < arraySize; ++i) {
            xmlrpc_value * valueP;

            xmlrpc_array_read_item(&env, arrayP, i, &valueP);

            if (env.fault_occurred)
                printf("Unable to get array item %u\n", i);
            else {
                const char * prefix2;

                casprintf(&prefix2, "%s  Index %2u ", blankPrefix, i);
                dumpValue(prefix2, valueP);
                strfree(prefix2);

                xmlrpc_DECREF(valueP);
            }
        }
        strfree(blankPrefix);
    }
    xmlrpc_env_clean(&env);
}



static void
dumpStructMember(const char *   const prefix,
                 xmlrpc_value * const structP,
                 unsigned int   const index) {

    xmlrpc_env env;

    xmlrpc_value * keyP;
    xmlrpc_value * valueP;
    
    xmlrpc_env_init(&env);

    xmlrpc_struct_read_member(&env, structP, index, &keyP, &valueP);

    if (env.fault_occurred)
        printf("Unable to get struct member %u\n", index);
    else {
        int const blankCount = strlen(prefix);
        const char * prefix2;
        const char * blankPrefix;

        casprintf(&prefix2, "%s  Key:   ", prefix);
        dumpValue(prefix2, keyP);
        strfree(prefix2);

        casprintf(&blankPrefix, "%*s", blankCount, "");
        
        casprintf(&prefix2, "%s  Value: ", blankPrefix);
        dumpValue(prefix2, valueP);
        strfree(prefix2);

        strfree(blankPrefix);

        xmlrpc_DECREF(keyP);
        xmlrpc_DECREF(valueP);
    }
    xmlrpc_env_clean(&env);
}



static void
dumpStruct(const char *   const prefix,
           xmlrpc_value * const structP) {

    xmlrpc_env env;
    unsigned int structSize;

    xmlrpc_env_init(&env);

    structSize = xmlrpc_struct_size(&env, structP);
    if (env.fault_occurred)
        printf("Unable to get struct size.  %s\n", env.fault_string);
    else {
        unsigned int i;

        printf("%sStruct of %u members:\n", prefix, structSize);

        for (i = 0; i < structSize; ++i) {
            int const blankCount = strlen(prefix);
            const char * prefix1;

            if (index == 0)
                prefix1 = strdup(prefix);
            else
                casprintf(&prefix1, "%*s", blankCount, "");
            
            dumpStructMember(prefix1, structP, i);

            strfree(prefix1);
        }
    }
    xmlrpc_env_clean(&env);
}



static void
dumpCPtr(const char *   const prefix,
         xmlrpc_value * const valueP) {

    xmlrpc_env env;
    const char * value;

    xmlrpc_env_init(&env);

    xmlrpc_parse_value(&env, valueP, "p", &value);
        
    if (env.fault_occurred)
        printf("Unable to parse C pointer in result.  %s\n",
               env.fault_string);
    else
        printf("%sC pointer: '%p'\n", prefix, value);

    xmlrpc_env_clean(&env);
}



static void
dumpUnknown(const char *   const prefix,
            xmlrpc_value * const valueP) {

    printf("%sDon't recognize the type of the result: %u.\n", 
           prefix, xmlrpc_value_type(valueP));
    printf("%sCan't print it.\n", prefix);
}



static void
dumpValue(const char *   const prefix,
          xmlrpc_value * const valueP) {

    switch (xmlrpc_value_type(valueP)) {
    case XMLRPC_TYPE_INT:
        dumpInt(prefix, valueP);
        break;
    case XMLRPC_TYPE_BOOL: 
        dumpBool(prefix, valueP);
        break;
    case XMLRPC_TYPE_DOUBLE: 
        dumpDouble(prefix, valueP);
        break;
    case XMLRPC_TYPE_DATETIME:
        dumpDatetime(prefix, valueP);
        break;
    case XMLRPC_TYPE_STRING: 
        dumpString(prefix, valueP);
        break;
    case XMLRPC_TYPE_BASE64:
        dumpBase64(prefix, valueP);
        break;
    case XMLRPC_TYPE_ARRAY: 
        dumpArray(prefix, valueP);
        break;
    case XMLRPC_TYPE_STRUCT:
        dumpStruct(prefix, valueP);
        break;
    case XMLRPC_TYPE_C_PTR:
        dumpCPtr(prefix, valueP);
        break;
    default:
        dumpUnknown(prefix, valueP);
    }
}



static void
dumpResult(xmlrpc_value * const resultP) {

    printf("Result:\n\n");

    dumpValue("", resultP);
}



static void
doCall(xmlrpc_env *    const envP,
       const char *    const transport,
       const char *    const url,
       const char *    const methodName,
       xmlrpc_value *  const paramArrayP,
       xmlrpc_value ** const resultPP) {
    
    struct xmlrpc_clientparms clientparms;

    XMLRPC_ASSERT(xmlrpc_value_type(paramArrayP) == XMLRPC_TYPE_ARRAY);

    clientparms.transport = transport;

    xmlrpc_client_init2(envP, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, 
                        &clientparms, XMLRPC_CPSIZE(transport));
    if (!envP->fault_occurred) {
        *resultPP = xmlrpc_client_call_params(
            envP, url, methodName, paramArrayP);
    
        xmlrpc_client_cleanup();
    }
}



int 
main(int           const argc, 
     const char ** const argv) {

    struct cmdlineInfo cmdline;
    xmlrpc_env env;
    xmlrpc_value * paramArrayP;
    xmlrpc_value * resultP;
    const char * url;

    xmlrpc_env_init(&env);

    parseCommandLine(&env, argc, argv, &cmdline);
    die_if_fault_occurred(&env);

    computeUrl(cmdline.url, &url);

    computeParamArray(&env, cmdline.paramCount, cmdline.params, &paramArrayP);
    die_if_fault_occurred(&env);

    doCall(&env, cmdline.transport, url, cmdline.methodName, paramArrayP, 
           &resultP);
    die_if_fault_occurred(&env);

    dumpResult(resultP);
    
    strfree(url);

    xmlrpc_DECREF(resultP);

    freeCmdline(cmdline);

    xmlrpc_env_clean(&env);
    
    return 0;
}
